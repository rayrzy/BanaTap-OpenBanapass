#include "banapass.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXPORT __declspec(dllexport)

// Global state
std::atomic<bool> waitingForTouch(false);
std::atomic<bool> touchFinished(false);
std::atomic<bool> pollRunning(false);
u64 touchData;
callbackTouch touchCallback;

char accessCode[21] = "00000000000000000000";
char chipId[33]     = "00000000000000000000000000000000";
u8 cardData[168];

int cardInsertKey = 'C';

static const u8 cardDataTemplate[168] = {
    0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2E, 0x58, 0x32,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x5C, 0x97, 0x44,
    0xF0, 0x88, 0x04, 0x00, 0x43, 0x26, 0x2C, 0x33, 0x00, 0x04, 0x06, 0x10, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x43, 0x36, 0x00, 0x00,
    0xFA, 0xE9, 0x69, 0x00, 0xF6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---------------------------------------------------------------------------
// Settings / card.ini (stored next to the DLL, not in the process CWD)
// ---------------------------------------------------------------------------
static char iniPath[MAX_PATH]    = ".\\card.ini";
static char configPath[MAX_PATH] = ".\\BanaConfig.ini";

static bool autoTap        = false; // [settings] autoTap=1
static DWORD autoTapDelay  = 1500;  // [settings] autoTapDelayMs
static bool verifyPatch    = false; // [settings] verifyPatch=1

static void resolveIniPath() {
    HMODULE self = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&resolveIniPath, &self)) {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(self, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            char* slash = strrchr(buf, '\\');
            if (slash) {
                *(slash + 1) = 0;
                if (strlen(buf) + strlen("BanaConfig.ini") < MAX_PATH) {
                    strcpy(iniPath, buf);
                    strcat(iniPath, "card.ini");
                    strcpy(configPath, buf);
                    strcat(configPath, "BanaConfig.ini");
                }
            }
        }
    }
}

static BOOL fileExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void seedRandom() {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    srand((unsigned int)(time(0) ^ GetCurrentProcessId() ^ (unsigned int)qpc.LowPart ^ GetTickCount()));
}

static void makeAccessCode(char* out) { // 20 decimal digits
    for (int i = 0; i < 20; i++) out[i] = (char)('0' + rand() % 10);
    out[20] = 0;
}

static void makeChipId(char* out) { // 32 hex digits
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 32; i++) out[i] = hex[rand() % 16];
    out[32] = 0;
}

static bool validAccessCode(const char* s) {
    if (strlen(s) != 20) return false;
    for (int i = 0; i < 20; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static bool validChipId(const char* s) {
    if (strlen(s) != 32) return false;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static void createCard() {
    char buf[64] = {0};
    seedRandom();
    makeAccessCode(buf);
    WritePrivateProfileStringA("card", "accessCode", buf, iniPath);
    makeChipId(buf);
    WritePrivateProfileStringA("card", "chipId", buf, iniPath);
}

// BanaConfig.ini holds the behaviour options. Section/keys (ReadKey, AutoRead)
// follow the layout used by other OpenBanapass builds; delay and verify
// options are BanaTap additions.
static void createConfig() {
    WritePrivateProfileStringA("BanaConfig", "ReadKey", "C", configPath);
    WritePrivateProfileStringA("BanaConfig", "AutoRead", "0", configPath);
    WritePrivateProfileStringA("BanaConfig", "AutoReadDelayMs", "1500", configPath);
    WritePrivateProfileStringA("BanaConfig", "VerifyPatch", "0", configPath);
}

// "C" / "c" -> 'C'; "67" or "0x43" -> virtual-key code. Returns 0 if invalid.
static int parseKey(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;
    if (s[1] == 0 || s[1] == ' ' || s[1] == '\t') {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        return (unsigned char)c;
    }
    char* end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || v < 1 || v > 255) return 0;
    return (int)v;
}

static void loadSettings() {
    char keyStr[16] = {0};

    // Legacy location: [settings] in card.ini (kept for backward compatibility)
    GetPrivateProfileStringA("settings", "key", "C", keyStr, sizeof(keyStr), iniPath);
    int k = parseKey(keyStr);
    if (k) cardInsertKey = k;
    autoTap      = GetPrivateProfileIntA("settings", "autoTap", 0, iniPath) != 0;
    autoTapDelay = (DWORD)GetPrivateProfileIntA("settings", "autoTapDelayMs", 1500, iniPath);
    verifyPatch  = GetPrivateProfileIntA("settings", "verifyPatch", 0, iniPath) != 0;

    // BanaConfig.ini overrides the legacy values
    if (!fileExists(configPath)) createConfig();

    keyStr[0] = 0;
    GetPrivateProfileStringA("BanaConfig", "ReadKey", "", keyStr, sizeof(keyStr), configPath);
    k = parseKey(keyStr);
    if (k) cardInsertKey = k;

    int v = GetPrivateProfileIntA("BanaConfig", "AutoRead", -1, configPath);
    if (v != -1) autoTap = (v != 0);
    v = GetPrivateProfileIntA("BanaConfig", "AutoReadDelayMs", -1, configPath);
    if (v >= 0) autoTapDelay = (DWORD)v;
    v = GetPrivateProfileIntA("BanaConfig", "VerifyPatch", -1, configPath);
    if (v != -1) verifyPatch = (v != 0);
}

void reloadCard() {
    char ac[64] = {0};
    char ci[64] = {0};
    GetPrivateProfileStringA("card", "accessCode", "", ac, sizeof(ac), iniPath);
    GetPrivateProfileStringA("card", "chipId", "", ci, sizeof(ci), iniPath);

    // Repair invalid values instead of sending garbage to the server
    bool dirty = false;
    if (!validAccessCode(ac)) { seedRandom(); makeAccessCode(ac); dirty = true; }
    if (!validChipId(ci))     { seedRandom(); makeChipId(ci);     dirty = true; }
    if (dirty) {
        WritePrivateProfileStringA("card", "accessCode", ac, iniPath);
        WritePrivateProfileStringA("card", "chipId", ci, iniPath);
    }
    memcpy(accessCode, ac, sizeof(accessCode)); accessCode[20] = 0;
    memcpy(chipId, ci, sizeof(chipId));         chipId[32] = 0;
}

void initBanapass() {
    resolveIniPath();
    if (!fileExists(iniPath)) createCard();
    loadSettings();
    memcpy(cardData, cardDataTemplate, 168);
    reloadCard();
    pollRunning = true;
}

void shutdownBanapass() {
    pollRunning = false; // thread exits on its next iteration
}

// Patch the game's card data clearing function call with NOPs.
// Without this patch, the game clears card data when using virtual card emulation,
// resulting in empty card data being sent to the server.
void patchCardDataCheck(u64 patchOffset) {
    HMODULE gameModule = GetModuleHandle(NULL);
    if (!gameModule) return;

    u8* patchAddr = (u8*)gameModule + patchOffset;
    DWORD oldProtect;

    if (!VirtualProtect(patchAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        OutputDebugStringA("[BanaTap] VirtualProtect failed, card data patch not applied\n");
        return;
    }

    bool isCall = (patchAddr[0] == 0xE8);
    bool isNop  = (patchAddr[0] == 0x90 && patchAddr[1] == 0x90 && patchAddr[2] == 0x90 &&
                   patchAddr[3] == 0x90 && patchAddr[4] == 0x90);

    char msg[96];
    sprintf(msg, "[BanaTap] patch @+0x%llX first byte = 0x%02X\n",
            (unsigned long long)patchOffset, patchAddr[0]);
    OutputDebugStringA(msg);

    if (verifyPatch && !isCall && !isNop) {
        OutputDebugStringA("[BanaTap] unexpected bytes at patch offset (wrong game revision?), skipped\n");
    } else {
        memset(patchAddr, 0x90, 5);
        FlushInstructionCache(GetCurrentProcess(), patchAddr, 5);
    }
    VirtualProtect(patchAddr, 5, oldProtect, &oldProtect);
}

DWORD WINAPI cardPollThread(LPVOID lpParam) {
    DWORD waitSince = 0;
    bool wasWaiting = false;

    while (pollRunning) {
        bool waiting = waitingForTouch;
        if (waiting && !wasWaiting) waitSince = GetTickCount();
        wasWaiting = waiting;

        bool keyDown = (GetAsyncKeyState(cardInsertKey) & 0x8000) != 0;
        bool autoDue = autoTap && waiting && (GetTickCount() - waitSince >= autoTapDelay);

        if (waiting && (keyDown || autoDue)) {
            callbackTouch cb = touchCallback;
            u64 data = touchData;
            if (cb) {
                reloadCard();
                memcpy(cardData + 0x2C, chipId, 33);
                memcpy(cardData + 0x50, accessCode, 21);
                cb(0, 0, cardData, data);
                waitingForTouch = false;
                touchFinished   = true;
            }
        }
        Sleep(16);
    }
    return 0;
}

extern "C" {

EXPORT u64 BngRwDevReset() { return 0; }
EXPORT u64 BngRwReadMifare() { return 0xFFFFFF9C; }
EXPORT void BngRwFin() { return; }
EXPORT u64 BngRwGetFwVersion() { return 0; }
EXPORT u64 BngRwGetStationID() { return 0; }
EXPORT u64 BngRwGetRetryCount() { return 0; }
EXPORT i32 BngRwIsCmdExec() { return -1; }
EXPORT u64 BngRwReqAction() { return 1; }
EXPORT u64 BngRwReqAiccAuth() { return 1; }
EXPORT u64 BngRwReqBeep() { return 1; }
EXPORT u64 BngRwReqFwCleanup() { return 1; }
EXPORT u64 BngRwReqFwVersionUp() { return 1; }
EXPORT i32 BngRwReqLatchID() { return 1; }
EXPORT u64 BngRwReqLed() { return 1; }
EXPORT i32 BngRwReqSendMail() { return 1; }
EXPORT i32 BngRwReqSendUrl() { return 1; }
EXPORT i32 BngRwReqSendUrlTo() { return 1; }
EXPORT u64 BngRwReqSetLedPower() { return 0; }
EXPORT i32 BngRwReqCancel() { return 1; }
EXPORT const char* BngRwGetVersion() { return "Ver 1.6.0"; }
EXPORT u64 BngRwAttach(i32 a1, char* a2, i32 a3, i32 a4, void* callback, u64 a6) { return 1; }
EXPORT i32 BngRwInit() { return 0; }

// Aliases using the names found in other OpenBanapass builds / the original
// Banapassport API, so games that resolve these by name still find them.
// Arguments are ignored (Win64 calling convention: caller cleans up).
EXPORT u64 BngRwExReadMifareAllBlock() { return 0xFFFFFF9C; }
EXPORT u64 BngRwGetTotalRetryCount() { return 0; }
EXPORT i32 BngRwReqSendMailTo() { return 1; }
EXPORT u64 BngRwReqFwVersionup() { return 1; }

EXPORT i32 BngRwReqWaitTouch(u32 a1, i32 a2, u32 a3, callbackTouch callback, u64 a5) {
    if (touchFinished) {
        touchFinished = false;
        return 1;
    }
    touchCallback   = callback;
    touchData       = a5;
    waitingForTouch = true;
    return -1;
}
}
