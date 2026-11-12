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
static bool beepOnTap      = false; // [BanaConfig] BeepOnTap=1
static WORD padMask        = 0;     // [BanaConfig] PadButton=START / LB+A ...

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
    WritePrivateProfileStringA("BanaConfig", "PadButton", "", configPath);
    WritePrivateProfileStringA("BanaConfig", "BeepOnTap", "0", configPath);
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

// ---- tolerant config parsing ----------------------------------------------

// Cut an inline comment (";" or "#" at the start or after whitespace) and trim.
static void cleanValue(char* s) {
    for (char* p = s; *p; p++) {
        if ((*p == ';' || *p == '#') && (p == s || p[-1] == ' ' || p[-1] == '\t')) {
            *p = 0;
            break;
        }
    }
    char* start = s;
    while (*start == ' ' || *start == '\t') start++;
    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                       start[len - 1] == '\r' || start[len - 1] == '\n')) {
        start[--len] = 0;
    }
    if (start != s) memmove(s, start, len + 1);
}

static bool readCfg(const char* section, const char* key, const char* file, char* out, DWORD size) {
    GetPrivateProfileStringA(section, key, "", out, size, file);
    cleanValue(out);
    return out[0] != 0;
}

static bool equalsNoCase(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return false;
    }
    return *a == *b;
}

static bool parseBool(const char* s, bool* out) {
    if (equalsNoCase(s, "1") || equalsNoCase(s, "true") || equalsNoCase(s, "yes") || equalsNoCase(s, "on")) {
        *out = true;
        return true;
    }
    if (equalsNoCase(s, "0") || equalsNoCase(s, "false") || equalsNoCase(s, "no") || equalsNoCase(s, "off")) {
        *out = false;
        return true;
    }
    return false;
}

static bool parseInt(const char* s, long* out) {
    char* end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || *end != 0) return false;
    *out = v;
    return true;
}

// XInput button names -> wButtons mask. "LB+A" combines buttons.
// Returns false (and leaves mask untouched) if any name is unknown.
static bool parsePadMask(const char* s, WORD* mask) {
    static const struct { const char* name; WORD bit; } table[] = {
        {"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008},
        {"START", 0x0010}, {"BACK", 0x0020}, {"SELECT", 0x0020},
        {"L3", 0x0040}, {"LTHUMB", 0x0040}, {"R3", 0x0080}, {"RTHUMB", 0x0080},
        {"LB", 0x0100}, {"LSHOULDER", 0x0100}, {"RB", 0x0200}, {"RSHOULDER", 0x0200},
        {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000},
    };
    WORD result = 0;
    char buf[64];
    if (strlen(s) >= sizeof(buf)) return false;
    strcpy(buf, s);
    char* tok = buf;
    while (tok) {
        char* plus = strchr(tok, '+');
        if (plus) *plus = 0;
        cleanValue(tok);
        bool found = false;
        for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
            if (equalsNoCase(tok, table[i].name)) {
                result |= table[i].bit;
                found = true;
                break;
            }
        }
        if (!found) return false;
        tok = plus ? plus + 1 : NULL;
    }
    *mask = result;
    return result != 0;
}

static void applyFile(const char* section, const char* file, const char* keyKey,
                      const char* autoKey, const char* delayKey, const char* verifyKey) {
    char val[64];
    bool b;
    long n;

    if (keyKey && readCfg(section, keyKey, file, val, sizeof(val))) {
        int k = parseKey(val);
        if (k) cardInsertKey = k;
    }
    if (readCfg(section, autoKey, file, val, sizeof(val)) && parseBool(val, &b)) autoTap = b;
    if (readCfg(section, delayKey, file, val, sizeof(val)) && parseInt(val, &n) && n >= 0) autoTapDelay = (DWORD)n;
    if (readCfg(section, verifyKey, file, val, sizeof(val)) && parseBool(val, &b)) verifyPatch = b;
}

static void loadSettings() {
    // Defaults first, so removing a key from the file restores its default
    cardInsertKey = 'C';
    autoTap       = false;
    autoTapDelay  = 1500;
    verifyPatch   = false;
    beepOnTap     = false;
    padMask       = 0;

    // Legacy location: [settings] in card.ini (kept for backward compatibility)
    applyFile("settings", iniPath, "key", "autoTap", "autoTapDelayMs", "verifyPatch");

    // BanaConfig.ini overrides the legacy values
    if (!fileExists(configPath)) createConfig();
    applyFile("BanaConfig", configPath, "ReadKey", "AutoRead", "AutoReadDelayMs", "VerifyPatch");

    char val[64];
    bool b;
    if (readCfg("BanaConfig", "BeepOnTap", configPath, val, sizeof(val)) && parseBool(val, &b)) beepOnTap = b;
    if (readCfg("BanaConfig", "PadButton", configPath, val, sizeof(val))) {
        WORD m = 0;
        if (parsePadMask(val, &m)) {
            padMask = m;
        } else {
            OutputDebugStringA("[BanaTap] PadButton value not recognised, gamepad tap disabled\n");
        }
    }
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

// ---- gamepad (XInput, loaded on demand so there is no hard dependency) ------

struct PadState {
    DWORD packet;
    WORD buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT thumbLX, thumbLY, thumbRX, thumbRY;
};
typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, PadState*);

static XInputGetStateFn xinputGetState = NULL;
static bool xinputTried = false;

static bool padPressed() {
    if (!padMask) return false;
    if (!xinputTried) {
        xinputTried = true;
        const char* dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (int i = 0; i < 3 && !xinputGetState; i++) {
            HMODULE m = LoadLibraryA(dlls[i]);
            if (m) xinputGetState = (XInputGetStateFn)GetProcAddress(m, "XInputGetState");
        }
        if (!xinputGetState) OutputDebugStringA("[BanaTap] XInput not available, gamepad tap disabled\n");
    }
    if (!xinputGetState) return false;
    for (DWORD i = 0; i < 4; i++) {
        PadState st;
        memset(&st, 0, sizeof(st));
        if (xinputGetState(i, &st) == 0 && (st.buttons & padMask) == padMask) return true;
    }
    return false;
}

// ---- config file change detection --------------------------------------------

static void fileTime(const char* path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    memset(&d, 0, sizeof(d));
    memset(out, 0, sizeof(*out));
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &d)) *out = d.ftLastWriteTime;
}

DWORD WINAPI cardPollThread(LPVOID lpParam) {
    DWORD waitSince = 0;
    bool wasWaiting = false;

    FILETIME cfgTime, iniTime;
    fileTime(configPath, &cfgTime);
    fileTime(iniPath, &iniTime);
    DWORD lastCheck = GetTickCount();

    while (pollRunning) {
        // Pick up edits to BanaConfig.ini / card.ini [settings] without a restart
        DWORD now = GetTickCount();
        if (now - lastCheck >= 1000) {
            lastCheck = now;
            FILETIME c, i;
            fileTime(configPath, &c);
            fileTime(iniPath, &i);
            if (CompareFileTime(&c, &cfgTime) != 0 || CompareFileTime(&i, &iniTime) != 0) {
                loadSettings();
                fileTime(configPath, &cfgTime);
                fileTime(iniPath, &iniTime);
                OutputDebugStringA("[BanaTap] config reloaded\n");
            }
        }

        bool waiting = waitingForTouch;
        if (waiting && !wasWaiting) waitSince = GetTickCount();
        wasWaiting = waiting;

        bool keyDown = (GetAsyncKeyState(cardInsertKey) & 0x8000) != 0;
        bool padDown = waiting && padPressed();
        bool autoDue = autoTap && waiting && (GetTickCount() - waitSince >= autoTapDelay);

        if (waiting && (keyDown || padDown || autoDue)) {
            callbackTouch cb = touchCallback;
            u64 data = touchData;
            if (cb) {
                reloadCard();
                memcpy(cardData + 0x2C, chipId, 33);
                memcpy(cardData + 0x50, accessCode, 21);
                cb(0, 0, cardData, data);
                waitingForTouch = false;
                touchFinished   = true;
                if (beepOnTap) MessageBeep(MB_OK);
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
