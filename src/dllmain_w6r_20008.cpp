#include "banapass.h"

#define PATCH_OFFSET 0x9A391B

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        initBanapass();
        patchCardDataCheck(PATCH_OFFSET);
        CreateThread(NULL, 0, cardPollThread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        shutdownBanapass();
    }
    return TRUE;
}
