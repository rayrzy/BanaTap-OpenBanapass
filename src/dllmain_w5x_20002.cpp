#include "banapass.h"
#include "MinHook.h"

#define PATCH_OFFSET 0x793A68

// W5X 2.00.02 Function Address Offset
#define ADDR_BngRwAttach       0xB4D6E0
#define ADDR_BngRwInit         0xB4CDA0
#define ADDR_BngRwFin          0xB4CE30
#define ADDR_BngRwGetVersion   0xB4D6D0
#define ADDR_BngRwIsCmdExec    0xB4CF00
#define ADDR_BngRwReqLed       0xB4D400
#define ADDR_BngRwReqAction    0xB4D5F0
#define ADDR_BngRwReqBeep      0xB4D4F0
#define ADDR_BngRwReqCancel    0xB4CF70
#define ADDR_BngRwReqSendUrlTo 0xB4D150
#define ADDR_BngRwReqWaitTouch 0xB4D040

static uintptr_t imageBase;

static int hooksOk, hooksFailed;

static void hook(const char* name, uintptr_t addr, void* detour) {
    MH_STATUS st = MH_CreateHook((void*)addr, detour, NULL);
    if (st == MH_OK) {
        hooksOk++;
        return;
    }
    hooksFailed++;
    bnLog("hook %s at +0x%lX failed: %s", name, (unsigned long)(addr - imageBase), MH_StatusToString(st));
}

static void installHooks() {
    imageBase = (uintptr_t)GetModuleHandleA(NULL);
    
    if (MH_Initialize() != MH_OK) {
        bnLog("MH_Initialize failed");
        return;
    }
    
    hook("BngRwAttach", imageBase + ADDR_BngRwAttach, (void*)BngRwAttach);
    hook("BngRwInit", imageBase + ADDR_BngRwInit, (void*)BngRwInit);
    hook("BngRwFin", imageBase + ADDR_BngRwFin, (void*)BngRwFin);
    hook("BngRwGetVersion", imageBase + ADDR_BngRwGetVersion, (void*)BngRwGetVersion);
    hook("BngRwIsCmdExec", imageBase + ADDR_BngRwIsCmdExec, (void*)BngRwIsCmdExec);
    hook("BngRwReqLed", imageBase + ADDR_BngRwReqLed, (void*)BngRwReqLed);
    hook("BngRwReqAction", imageBase + ADDR_BngRwReqAction, (void*)BngRwReqAction);
    hook("BngRwReqBeep", imageBase + ADDR_BngRwReqBeep, (void*)BngRwReqBeep);
    hook("BngRwReqCancel", imageBase + ADDR_BngRwReqCancel, (void*)BngRwReqCancel);
    hook("BngRwReqSendUrlTo", imageBase + ADDR_BngRwReqSendUrlTo, (void*)BngRwReqSendUrlTo);
    hook("BngRwReqWaitTouch", imageBase + ADDR_BngRwReqWaitTouch, (void*)BngRwReqWaitTouch);
    
    MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK)
        bnLog("MH_EnableHook failed: %s", MH_StatusToString(st));
    else
        bnLog("hooks enabled: %d ok, %d failed", hooksOk, hooksFailed);
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        initBanapass();
        installHooks();
        patchCardDataCheck(PATCH_OFFSET);
        HANDLE pollThread = CreateThread(NULL, 0, cardPollThread, NULL, 0, NULL);
        if (pollThread)
            CloseHandle(pollThread);
        else
            bnLog("CreateThread for the poll thread failed (error %lu)", (unsigned long)GetLastError());
    }
    else if (reason == DLL_PROCESS_DETACH) {
        shutdownBanapass();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
