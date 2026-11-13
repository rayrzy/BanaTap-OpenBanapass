#include "banapass.h"

#define PATCH_OFFSET 0x9D7C2B

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        initBanapass();
        patchCardDataCheck(PATCH_OFFSET);
        HANDLE pollThread = CreateThread(NULL, 0, cardPollThread, NULL, 0, NULL);
        if (pollThread)
            CloseHandle(pollThread);
        else
            bnLog("CreateThread for the poll thread failed (error %lu)", (unsigned long)GetLastError());
    }
    else if (reason == DLL_PROCESS_DETACH) {
        shutdownBanapass();
    }
    return TRUE;
}
