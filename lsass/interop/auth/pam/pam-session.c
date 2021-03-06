/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 * -*- mode: c, c-basic-offset: 4 -*- */

/*
 * Copyright © BeyondTrust Software 2004 - 2019
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * BEYONDTRUST MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING TERMS AS
 * WELL. IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT WITH
 * BEYONDTRUST, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE TERMS OF THAT
 * SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE APACHE LICENSE,
 * NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU HAVE QUESTIONS, OR WISH TO REQUEST
 * A COPY OF THE ALTERNATE LICENSING TERMS OFFERED BY BEYONDTRUST, PLEASE CONTACT
 * BEYONDTRUST AT beyondtrust.com/contact
 */

/*
 * Copyright (C) BeyondTrust Software. All rights reserved.
 *
 * Module Name:
 *
 *        pam-session.c
 *
 * Abstract:
 *
 *        BeyondTrust Security and Authentication Subsystem (LSASS)
 *
 *        Pluggable Authentication Module
 *
 *        Logon Session API
 *
 * Authors: Krishna Ganugapati (krishnag@likewisesoftware.com)
 *          Sriram Nambakam (snambakam@likewisesoftware.com)
 */
#include "pam-lsass.h"

int
pam_sm_open_session(
    pam_handle_t* pamh,
    int           flags,
    int           argc,
    const char**  argv
    )
{
    DWORD dwError = 0;
    PPAMCONTEXT pPamContext = NULL;
    HANDLE hLsaConnection = (HANDLE)NULL;
    PSTR pszLoginId = NULL;
    PLSA_PAM_CONFIG pConfig = NULL;

#ifdef HAVE_PAM_PUTENV
    PSTR pszSmartCardReaderNull = NULL;
    PSTR pszSmartCardReader = NULL;
    PSTR pszSmartCardReaderEnv = NULL;
#endif /* HAVE_PAM_PUTENV */

    LSA_LOG_PAM_DEBUG("pam_sm_open_session::begin");

    dwError = LsaPamGetContext(
                    pamh,
                    flags,
                    argc,
                    argv,
                    &pPamContext);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaPamGetLoginId(
                    pamh,
                    pPamContext,
                    &pszLoginId,
                    TRUE);
    BAIL_ON_LSA_ERROR(dwError);

    if (LsaShouldIgnoreUser(pszLoginId))
    {
        LSA_LOG_PAM_DEBUG("Bypassing lsassd for local account");
        dwError = LW_ERROR_IGNORE_THIS_USER;
        BAIL_ON_LSA_ERROR(dwError);
    }


    dwError = LsaPamGetConfig(&pConfig);
    BAIL_ON_LSA_ERROR(dwError);

    LsaPamSetLogLevel(pConfig->dwLogLevel);


#ifdef HAVE_PAM_PUTENV
    dwError = pam_get_data(
        pamh,
        PAM_LSASS_SMART_CARD_READER,
        (PAM_GET_DATA_TYPE)&pszSmartCardReader);
    /* pszSmartCardReader will be freed when the module is closed. */
    if (dwError == PAM_SUCCESS && pszSmartCardReader != NULL)
    {
        // CK_SLOT_INFO slotDescription is not NULL terminated.
        dwError = LwStrndup(pszSmartCardReader, LW_CK_SLOT_DESCRIPTION_LEN, &pszSmartCardReaderNull);
        BAIL_ON_LSA_ERROR(dwError);

        dwError = LwAllocateStringPrintf(
            &pszSmartCardReaderEnv,
            "LW_SMART_CARD_READER=%s",
            pszSmartCardReaderNull);
        BAIL_ON_LSA_ERROR(dwError);

        dwError = pam_putenv(
            pamh,
            pszSmartCardReaderEnv);
        BAIL_ON_LSA_ERROR(dwError);

        LwFreeString(pszSmartCardReaderNull);
        pszSmartCardReaderNull = NULL;
    }
#endif /* HAVE_PAM_PUTENV */

    dwError = LsaOpenServer(&hLsaConnection);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaOpenSession(
                    hLsaConnection,
                    pszLoginId);
    BAIL_ON_LSA_ERROR(dwError);


    if (pPamContext &&
        pConfig->bLsaPamDisplayMOTD)
    {
        dwError = LsaPamDisplayMOTD(
                        pamh,
                        pPamContext);
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaPamNotifyUserLogon(
                    pszLoginId);
    if (dwError == LW_ERROR_LOAD_LIBRARY_FAILED ||
        dwError == LW_ERROR_LOOKUP_SYMBOL_FAILED )
    {
        dwError = 0;
    }
    BAIL_ON_LSA_ERROR(dwError);

cleanup:

    if (hLsaConnection != (HANDLE)NULL) {
        LsaCloseServer(hLsaConnection);
    }

    if (pConfig)
    {
        LsaPamFreeConfig(pConfig);
    }

    LW_SAFE_FREE_STRING(pszLoginId);

#ifdef HAVE_PAM_PUTENV
    LW_SAFE_FREE_STRING(pszSmartCardReaderEnv);
#endif /* HAVE_PAM_PUTENV */

    LSA_LOG_PAM_DEBUG("pam_sm_open_session::end");

    return LsaPamOpenPamFilterOpenSession(
                            LsaPamMapErrorCode(dwError, pPamContext));

error:

    if ((dwError == LW_ERROR_NO_SUCH_USER) || (dwError == LW_ERROR_NOT_HANDLED) ||
        (dwError == LW_ERROR_IGNORE_THIS_USER))
    {
        LSA_LOG_PAM_WARNING("pam_sm_open_session failed [login:%s][error code: %u]", 
                            LSA_SAFE_LOG_STRING(pszLoginId),
                            dwError);
    }
    else
    {
        LSA_LOG_PAM_ERROR("pam_sm_open_session failed [login:%s][error code: %u]", 
                          LSA_SAFE_LOG_STRING(pszLoginId),
                          dwError);
    }

    goto cleanup;
}

DWORD
LsaPamDisplayMOTD(
    pam_handle_t* pamh,
    PPAMCONTEXT    pPamContext
    )
{
    DWORD dwError = 0;

    INT iReadCount = 0;
    BOOLEAN bFileExists = FALSE;
    CHAR szMOTD[MOTD_MAX_SIZE+1];
    FILE* phMOTD = NULL;

    LSA_LOG_PAM_DEBUG("LsaPamDisplayMOTD::begin");

    memset(szMOTD, 0, sizeof(szMOTD));

    dwError = LsaCheckFileExists(MOTD_FILE, &bFileExists);
    BAIL_ON_LSA_ERROR(dwError);

    if (!bFileExists)
    {
        LSA_LOG_PAM_INFO("MOTD file not found: %s", MOTD_FILE);
        goto cleanup;
    }

    phMOTD = fopen(MOTD_FILE, "r");
    if (phMOTD == NULL)
    {
        dwError = LwMapErrnoToLwError(errno);
        LSA_LOG_PAM_INFO("Unable to open MOTD file for reading: %s", MOTD_FILE);
        BAIL_ON_LSA_ERROR(dwError);
    }

    iReadCount = fread((PVOID)szMOTD, 1, MOTD_MAX_SIZE, phMOTD);

    if (iReadCount > 0)
    {
        dwError = (DWORD) LsaPamConverse(
                            pamh,
                            szMOTD,
                            PAM_TEXT_INFO,
                            NULL);
    }

cleanup:

    if (phMOTD != NULL)
    {
        fclose(phMOTD);
    }

    LSA_LOG_PAM_DEBUG("LsaPamDisplayMOTD::end");

    return LW_ERROR_SUCCESS;

error:

    LSA_LOG_PAM_ERROR("Error: Failed to set MOTD. [error code: %u]", dwError);

    goto cleanup;
}


int
pam_sm_close_session(
    pam_handle_t* pamh,
    int           flags,
    int           argc,
    const char**  argv
    )
{
    DWORD dwError = 0;
    PPAMCONTEXT pPamContext = NULL;
    PSTR pszLoginId = NULL;
    HANDLE hLsaConnection = (HANDLE)NULL;
    PLSA_PAM_CONFIG pConfig = NULL;

    dwError = LsaPamGetConfig(&pConfig);
    BAIL_ON_LSA_ERROR(dwError);

    LsaPamSetLogLevel(pConfig->dwLogLevel);

    LSA_LOG_PAM_DEBUG("pam_sm_close_session::begin");

    dwError = LsaPamGetContext(
                    pamh,
                    flags,
                    argc,
                    argv,
                    &pPamContext);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaPamGetLoginId(
                    pamh,
                    pPamContext,
                    &pszLoginId,
                    FALSE);
    BAIL_ON_LSA_ERROR(dwError);

    if (pszLoginId == NULL)
    {
        dwError = LW_ERROR_NO_SUCH_USER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    if (LsaShouldIgnoreUser(pszLoginId))
    {
        LSA_LOG_PAM_DEBUG("Bypassing lsassd for local account");
        dwError = LW_ERROR_IGNORE_THIS_USER;
        BAIL_ON_LSA_ERROR(dwError);
    }

    dwError = LsaOpenServer(&hLsaConnection);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaCloseSession(
                            hLsaConnection,
                            pszLoginId);
    BAIL_ON_LSA_ERROR(dwError);

    dwError = LsaPamNotifyUserLogoff(
                    pszLoginId);
    if (dwError == LW_ERROR_LOAD_LIBRARY_FAILED ||
        dwError == LW_ERROR_LOOKUP_SYMBOL_FAILED )
    {
        dwError = 0;
    }
    BAIL_ON_LSA_ERROR(dwError);

cleanup:

    if (hLsaConnection != (HANDLE)NULL) {
        LsaCloseServer(hLsaConnection);
    }

    if (pConfig)
    {
        LsaPamFreeConfig(pConfig);
    }

    LW_SAFE_FREE_STRING(pszLoginId);

    LSA_LOG_PAM_DEBUG("pam_sm_close_session::end");

    return LsaPamOpenPamFilterCloseSession(
                            LsaPamMapErrorCode(dwError, pPamContext));

error:

    if ((dwError == LW_ERROR_NO_SUCH_USER) || (dwError == LW_ERROR_NOT_HANDLED) ||
        (dwError == LW_ERROR_IGNORE_THIS_USER))
    {
        LSA_LOG_PAM_WARNING("pam_sm_close_session error [error code:%u]", dwError);
    }
    else
    {
        LSA_LOG_PAM_ERROR("pam_sm_close_session error [error code:%u]", dwError);
    }

    goto cleanup;
}
