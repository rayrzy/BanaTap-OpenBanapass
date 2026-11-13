#include "banapass.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXPORT __declspec(dllexport)

// Set by the build (see meson.build): project version plus git revision
#ifndef BANATAP_VERSION
#define BANATAP_VERSION "dev"
#endif

// Global state
std::atomic<bool> waitingForTouch(false);
std::atomic<bool> touchFinished(false);
std::atomic<bool> pollRunning(false);
u64 touchData;
callbackTouch touchCallback;

// Same as OpenBanapass: the values from card.ini are kept as written (up to
// 63 characters, the size of the read buffer), not cut to 20 / 32 characters.
char accessCode[64] = "00000000000000000000";
char chipId[64]     = "00000000000000000000000000000000";
u8 cardData[168];

int cardInsertKey = 'C';

// ---------------------------------------------------------------------------
// Logging: OutputDebugString (DebugView) and, optionally, banatap.log next to
// the DLL. Card identifiers (chipId / accessCode) are never written to the log.
// ---------------------------------------------------------------------------
static char logPath[MAX_PATH] = ".\\banatap.log";
static bool logToFile         = true;  // [BanaConfig] LogToFile=0 disables the file
static bool logExceptions     = true;  // [BanaConfig] LogExceptions=0 disables crash notes
static bool logApiCalls       = true;  // [BanaConfig] LogApiCalls=0 disables the first-call trace
static bool strictApi         = false; // [BanaConfig] StrictApi=1 rejects invalid arguments with -100
static const DWORD LOG_MAX_BYTES = 256 * 1024;
static std::atomic_flag logLock = ATOMIC_FLAG_INIT;

void bnLog(const char* fmt, ...) {
    char body[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    body[sizeof(body) - 1] = 0;

    char dbg[448];
    snprintf(dbg, sizeof(dbg), "[BanaTap] %s\n", body);
    OutputDebugStringA(dbg);

    if (!logToFile) return;

    // Bounded wait: a thread that crashed while logging must not hang the others
    bool locked = false;
    for (int i = 0; i < 50 && !locked; i++) {
        locked = !logLock.test_and_set();
        if (!locked) Sleep(1);
    }
    if (!locked) return;

    FILE* f = fopen(logPath, "ab");
    if (f) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, body);
        fclose(f);
    }
    logLock.clear();
}

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
static bool onlyFocused    = true;  // [BanaConfig] OnlyWhenFocused=1
static bool autoOnce       = false; // [BanaConfig] AutoReadMode=once
static DWORD autoCooldown  = 0;     // [BanaConfig] AutoReadCooldownMs
static int toggleKey       = 0;     // [BanaConfig] ToggleAutoKey

// Runtime state (not reloaded from config)
static bool sessionTapped  = false; // a card has been sent since start / re-arm
static bool hasTapped      = false;
static DWORD lastTapTick   = 0;

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
                    if (strlen(buf) + strlen("banatap.log") < MAX_PATH) {
                        strcpy(logPath, buf);
                        strcat(logPath, "banatap.log");
                    }
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
    WritePrivateProfileStringA("BanaConfig", "OnlyWhenFocused", "1", configPath);
    WritePrivateProfileStringA("BanaConfig", "AutoReadMode", "always", configPath);
    WritePrivateProfileStringA("BanaConfig", "AutoReadCooldownMs", "0", configPath);
    WritePrivateProfileStringA("BanaConfig", "ToggleAutoKey", "", configPath);
    WritePrivateProfileStringA("BanaConfig", "LogToFile", "1", configPath);
    WritePrivateProfileStringA("BanaConfig", "LogExceptions", "1", configPath);
    WritePrivateProfileStringA("BanaConfig", "LogApiCalls", "1", configPath);
    WritePrivateProfileStringA("BanaConfig", "StrictApi", "0", configPath);
}

static bool equalsNoCase(const char* a, const char* b);

// "C" / "c" -> 'C'; "F9" -> VK_F9; "SPACE", "INSERT", ... -> named key;
// "67" or "0x43" -> virtual-key code. Returns 0 if invalid.
static int parseKey(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;
    if (s[1] == 0 || s[1] == ' ' || s[1] == '\t') {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        return (unsigned char)c;
    }
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '0' && s[1] <= '9') {
        char* end = NULL;
        long n = strtol(s + 1, &end, 10);
        if (end != s + 1 && *end == 0 && n >= 1 && n <= 24) return 0x70 + (int)n - 1;
    }
    static const struct { const char* name; int vk; } names[] = {
        {"SPACE", 0x20}, {"ENTER", 0x0D}, {"TAB", 0x09}, {"ESC", 0x1B},
        {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"HOME", 0x24}, {"END", 0x23},
        {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22},
        {"LEFT", 0x25}, {"UP", 0x26}, {"RIGHT", 0x27}, {"DOWN", 0x28},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (equalsNoCase(s, names[i].name)) return names[i].vk;
    }
    char* end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || *end != 0 || v < 1 || v > 255) return 0;
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
    onlyFocused   = true;
    autoOnce      = false;
    autoCooldown  = 0;
    toggleKey     = 0;
    logToFile     = true;
    logExceptions = true;
    logApiCalls   = true;
    strictApi     = false;

    // Legacy location: [settings] in card.ini (kept for backward compatibility)
    applyFile("settings", iniPath, "key", "autoTap", "autoTapDelayMs", "verifyPatch");

    // BanaConfig.ini overrides the legacy values
    if (!fileExists(configPath)) createConfig();
    applyFile("BanaConfig", configPath, "ReadKey", "AutoRead", "AutoReadDelayMs", "VerifyPatch");

    char val[64];
    bool b;
    if (readCfg("BanaConfig", "BeepOnTap", configPath, val, sizeof(val)) && parseBool(val, &b)) beepOnTap = b;
    if (readCfg("BanaConfig", "OnlyWhenFocused", configPath, val, sizeof(val)) && parseBool(val, &b)) onlyFocused = b;
    if (readCfg("BanaConfig", "AutoReadMode", configPath, val, sizeof(val))) {
        if (equalsNoCase(val, "once")) autoOnce = true;
        else if (equalsNoCase(val, "always")) autoOnce = false;
        else bnLog("AutoReadMode must be 'always' or 'once', keeping 'always'");
    }
    long cd;
    if (readCfg("BanaConfig", "AutoReadCooldownMs", configPath, val, sizeof(val)) && parseInt(val, &cd) && cd >= 0) autoCooldown = (DWORD)cd;
    if (readCfg("BanaConfig", "ToggleAutoKey", configPath, val, sizeof(val))) toggleKey = parseKey(val);
    if (readCfg("BanaConfig", "LogToFile", configPath, val, sizeof(val)) && parseBool(val, &b)) logToFile = b;
    if (readCfg("BanaConfig", "LogExceptions", configPath, val, sizeof(val)) && parseBool(val, &b)) logExceptions = b;
    if (readCfg("BanaConfig", "LogApiCalls", configPath, val, sizeof(val)) && parseBool(val, &b)) logApiCalls = b;
    if (readCfg("BanaConfig", "StrictApi", configPath, val, sizeof(val)) && parseBool(val, &b)) strictApi = b;
    if (readCfg("BanaConfig", "PadButton", configPath, val, sizeof(val))) {
        WORD m = 0;
        if (parsePadMask(val, &m)) {
            padMask = m;
        } else {
            bnLog("PadButton value not recognised, gamepad tap disabled");
        }
    }
}

// Reads chipId/accessCode from card.ini. The file is never modified here:
// a value you put there is the identity the server sees, so it is not
// "repaired" or replaced behind your back.
// Returns false (and sends nothing) if either value is missing.
// Values with an unexpected format are still used exactly as written (no
// truncation, like OpenBanapass) and a warning is logged.
bool reloadCard() {
    static bool warnedFormat = false;
    static DWORD lastMissingWarn = 0;
    char ac[64] = {0};
    char ci[64] = {0};
    readCfg("card", "accessCode", iniPath, ac, sizeof(ac));
    readCfg("card", "chipId", iniPath, ci, sizeof(ci));

    if (!ac[0] || !ci[0]) {
        DWORD now = GetTickCount();
        if (lastMissingWarn == 0 || now - lastMissingWarn > 5000) {
            lastMissingWarn = now ? now : 1;
            bnLog("card.ini has no chipId/accessCode under [card]; card not sent");
        }
        return false;
    }

    bool formatOk = validAccessCode(ac) && validChipId(ci);
    if (!formatOk && !warnedFormat) {
        warnedFormat = true;
        bnLog("chipId (32 hex) / accessCode (20 digits) look unusual; sending them as written");
    }
    if (formatOk) warnedFormat = false;

    memset(accessCode, 0, sizeof(accessCode));
    memset(chipId, 0, sizeof(chipId));
    memcpy(accessCode, ac, strlen(ac) + 1);
    memcpy(chipId, ci, strlen(ci) + 1);
    return true;
}

// Passive vectored exception handler: notes serious first-chance exceptions
// (with module and offset) in the log, then lets the normal handling continue.
// The game may handle some of these itself, so a line here is a lead, not
// proof of a crash. At most 20 entries per run.
static PVOID vehHandle = NULL;
static std::atomic<int> vehLogged(0);

static LONG WINAPI exceptionLogger(EXCEPTION_POINTERS* ep) {
    if (!logExceptions || !ep || !ep->ExceptionRecord) return 0; // EXCEPTION_CONTINUE_SEARCH
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    bool serious = code == 0xC0000005 || code == 0xC0000006 || code == 0xC000001D ||
                   code == 0xC0000094 || code == 0xC0000096 || code == 0xC00000FD;
    if (!serious) return 0;
    if (vehLogged.fetch_add(1) >= 20) return 0;

    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = NULL;
    char path[MAX_PATH] = "?";
    const char* name = path;
    unsigned long offset = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod) && mod) {
        if (GetModuleFileNameA(mod, path, MAX_PATH) > 0) {
            const char* slash = strrchr(path, '\\');
            name = slash ? slash + 1 : path;
        }
        offset = (unsigned long)((char*)addr - (char*)mod);
    }
    bnLog("exception 0x%08lX at %p (%s +0x%lX), first chance", (unsigned long)code, addr, name, offset);
    return 0;
}

void initBanapass() {
    resolveIniPath();

    // Keep the log small: start over if it has grown past the limit
    WIN32_FILE_ATTRIBUTE_DATA logInfo;
    if (GetFileAttributesExA(logPath, GetFileExInfoStandard, &logInfo) &&
        (logInfo.nFileSizeHigh != 0 || logInfo.nFileSizeLow > LOG_MAX_BYTES)) {
        DeleteFileA(logPath);
    }
    bnLog("---- BanaTap %s started (game base %p) ----", BANATAP_VERSION, (void*)GetModuleHandle(NULL));
    if (!fileExists(iniPath)) {
        createCard();
        bnLog("WARNING: card.ini not found, generated a NEW random card identity (new account on the server)");
    }
    loadSettings();
    memcpy(cardData, cardDataTemplate, 168);
    reloadCard();
    pollRunning = true;
    if (!vehHandle) vehHandle = AddVectoredExceptionHandler(1, exceptionLogger);
}

void shutdownBanapass() {
    pollRunning = false; // thread exits on its next iteration
    if (vehHandle) {
        RemoveVectoredExceptionHandler(vehHandle);
        vehHandle = NULL;
    }
    bnLog("---- BanaTap shutdown ----");
}

// Patch the game's card data clearing function call with NOPs.
// Without this patch, the game clears card data when using virtual card emulation,
// resulting in empty card data being sent to the server.
void patchCardDataCheck(u64 patchOffset) {
    HMODULE gameModule = GetModuleHandle(NULL);
    if (!gameModule) {
        bnLog("patch skipped: GetModuleHandle(NULL) failed");
        return;
    }

    u8* patchAddr = (u8*)gameModule + patchOffset;
    DWORD oldProtect;

    if (!VirtualProtect(patchAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        bnLog("patch not applied: VirtualProtect failed (error %lu)", (unsigned long)GetLastError());
        return;
    }

    bool isCall = (patchAddr[0] == 0xE8);
    bool isNop  = (patchAddr[0] == 0x90 && patchAddr[1] == 0x90 && patchAddr[2] == 0x90 &&
                   patchAddr[3] == 0x90 && patchAddr[4] == 0x90);

    bnLog("patch site +0x%lX: first byte 0x%02X (%s)", (unsigned long)patchOffset, patchAddr[0],
          isCall ? "call" : (isNop ? "already NOP" : "UNEXPECTED"));

    if (verifyPatch && !isCall && !isNop) {
        bnLog("patch skipped: unexpected bytes (wrong game revision?) and VerifyPatch=1");
    } else {
        memset(patchAddr, 0x90, 5);
        FlushInstructionCache(GetCurrentProcess(), patchAddr, 5);
        bnLog("patch applied");
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
        if (!xinputGetState) bnLog("XInput not available, gamepad tap disabled");
    }
    if (!xinputGetState) return false;
    for (DWORD i = 0; i < 4; i++) {
        PadState st;
        memset(&st, 0, sizeof(st));
        if (xinputGetState(i, &st) == 0 && (st.buttons & padMask) == padMask) return true;
    }
    return false;
}

// Is p a committed, executable address? Guards against calling a stale or
// garbage callback pointer handed over by the game.
static bool isExecutable(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// True if the foreground window belongs to this (game) process.
static bool gameFocused() {
    HWND h = GetForegroundWindow();
    if (!h) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
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
    bool toggleWasDown = false;
    DWORD lastFocusHint = 0;
    DWORD lastBadCb = 0;

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
                bnLog("config reloaded");
            }
        }

        bool waiting = waitingForTouch;
        if (waiting && !wasWaiting) {
            waitSince = GetTickCount();
            bnLog("game is waiting for a card");
        }
        wasWaiting = waiting;

        bool focused = !onlyFocused || gameFocused();

        // Runtime on/off switch for auto tap. Turning it on also re-arms it
        // (clears the "already tapped" state and the cooldown).
        bool toggleDown = toggleKey && focused && (GetAsyncKeyState(toggleKey) & 0x8000) != 0;
        if (toggleDown && !toggleWasDown) {
            autoTap = !autoTap;
            if (autoTap) {
                sessionTapped = false;
                hasTapped     = false;
            }
            bnLog(autoTap ? "auto tap ON" : "auto tap OFF");
            if (beepOnTap) MessageBeep(autoTap ? MB_ICONASTERISK : MB_ICONEXCLAMATION);
        }
        toggleWasDown = toggleDown;

        bool rawKey  = (GetAsyncKeyState(cardInsertKey) & 0x8000) != 0;
        bool keyDown = focused && rawKey;
        if (waiting && rawKey && !focused) {
            DWORD t = GetTickCount();
            if (lastFocusHint == 0 || t - lastFocusHint > 5000) {
                lastFocusHint = t ? t : 1;
                bnLog("tap key pressed but the game window is not in the foreground (OnlyWhenFocused=1)");
            }
        }
        bool padDown = waiting && focused && padPressed();

        bool autoDue = autoTap && focused && waiting &&
                       (GetTickCount() - waitSince >= autoTapDelay) &&
                       !(autoOnce && sessionTapped) &&
                       !(hasTapped && GetTickCount() - lastTapTick < autoCooldown);

        if (waiting && (keyDown || padDown || autoDue)) {
            callbackTouch cb = touchCallback;
            u64 data = touchData;
            if (cb && !isExecutable((const void*)cb)) {
                DWORD t = GetTickCount();
                if (lastBadCb == 0 || t - lastBadCb > 5000) {
                    lastBadCb = t ? t : 1;
                    bnLog("callback pointer %p is not executable memory, request dropped", (void*)cb);
                }
                waitingForTouch = false;
            } else if (cb && reloadCard()) {
                // Same as OpenBanapass: start from a fresh template, then copy
                // accessCode (offset 0x50) first and chipId (offset 0x2C) second,
                // each with its terminating NUL and no cut to 20 / 32 characters.
                memcpy(cardData, cardDataTemplate, sizeof(cardData));
                memcpy(cardData + 0x50, accessCode, strlen(accessCode) + 1);
                memcpy(cardData + 0x2C, chipId, strlen(chipId) + 1);
                cb(0, 0, cardData, data);
                bnLog("card sent (%s)", keyDown ? "key" : (padDown ? "gamepad" : "auto"));
                waitingForTouch = false;
                touchFinished   = true;
                sessionTapped   = true;
                hasTapped       = true;
                lastTapTick     = GetTickCount();
                if (beepOnTap) MessageBeep(MB_OK);
            }
        }
        Sleep(16);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// BngRw API
//
// Argument checks follow the reader API used by other OpenBanapass builds:
// reader numbers above 7 and missing string arguments are rejected with
// -100. They are only enforced with [BanaConfig] StrictApi=1; by default the
// stubs accept everything, which is what BanaTap always did. (Another build
// rejects BngRwReqLatchID for reader numbers *below* 8, the opposite of its
// sibling functions; that looks like an inverted check and is not copied.)
//
// BngRwExReadMifareAllBlock returns 0xFFFFFF9C as a 64-bit value, exactly as
// the reference implementation does. It is deliberately not sign-extended.
// ---------------------------------------------------------------------------
#define BNGRW_ERR_ARG ((i32)-100)
#define BNGRW_MAX_READER 7u

// Logs the first call of each export (names only, never arguments)
#define API_TRACE(name)                                              \
    do {                                                             \
        static std::atomic<bool> seen(false);                        \
        if (logApiCalls && !seen.exchange(true))                     \
            bnLog("api: %s called by the game", name);               \
    } while (0)

extern "C" {

EXPORT u64 BngRwDevReset() { API_TRACE("BngRwDevReset"); return 0; }
EXPORT u64 BngRwReadMifare() { API_TRACE("BngRwReadMifare"); return 0xFFFFFF9C; }
EXPORT void BngRwFin() { API_TRACE("BngRwFin"); return; }
EXPORT u64 BngRwGetFwVersion() { API_TRACE("BngRwGetFwVersion"); return 0; }
EXPORT u64 BngRwGetStationID() { API_TRACE("BngRwGetStationID"); return 0; }
EXPORT u64 BngRwGetRetryCount() { API_TRACE("BngRwGetRetryCount"); return 0; }
EXPORT i32 BngRwIsCmdExec() { API_TRACE("BngRwIsCmdExec"); return -1; }
EXPORT u64 BngRwReqAction() { API_TRACE("BngRwReqAction"); return 1; }
EXPORT u64 BngRwReqAiccAuth() { API_TRACE("BngRwReqAiccAuth"); return 1; }
EXPORT u64 BngRwReqBeep() { API_TRACE("BngRwReqBeep"); return 1; }
EXPORT u64 BngRwReqFwCleanup() { API_TRACE("BngRwReqFwCleanup"); return 1; }
EXPORT u64 BngRwReqFwVersionUp() { API_TRACE("BngRwReqFwVersionUp"); return 1; }
EXPORT u64 BngRwReqLed() { API_TRACE("BngRwReqLed"); return 1; }
EXPORT i32 BngRwReqSendMail() { API_TRACE("BngRwReqSendMail"); return 1; }
EXPORT i32 BngRwReqSendUrl() { API_TRACE("BngRwReqSendUrl"); return 1; }
EXPORT u64 BngRwReqSetLedPower() { API_TRACE("BngRwReqSetLedPower"); return 0; }
EXPORT const char* BngRwGetVersion() { API_TRACE("BngRwGetVersion"); return "Ver 1.6.0"; }
EXPORT u64 BngRwAttach(i32 a1, char* a2, i32 a3, i32 a4, void* callback, u64 a6) { API_TRACE("BngRwAttach"); return 1; }
EXPORT i32 BngRwInit() { API_TRACE("BngRwInit"); return 0; }

// Names used by other OpenBanapass builds / the original Banapassport API, so
// games that resolve these by name still find them.
EXPORT u64 BngRwExReadMifareAllBlock() { API_TRACE("BngRwExReadMifareAllBlock"); return 0xFFFFFF9C; }
EXPORT u64 BngRwGetTotalRetryCount() { API_TRACE("BngRwGetTotalRetryCount"); return 0; }
EXPORT u64 BngRwReqFwVersionup() { API_TRACE("BngRwReqFwVersionup"); return 1; }

EXPORT i32 BngRwReqCancel(u32 reader) {
    API_TRACE("BngRwReqCancel");
    if (strictApi && reader > BNGRW_MAX_READER) return BNGRW_ERR_ARG;
    return 1;
}

EXPORT i32 BngRwReqLatchID(u32 reader, u64 b, u64 c) {
    API_TRACE("BngRwReqLatchID");
    if (strictApi && reader > BNGRW_MAX_READER) return BNGRW_ERR_ARG;
    return 1;
}

EXPORT i32 BngRwReqSendMailTo(u32 reader, i32 b, u32 c, i32* d, char* to, char* f, char* g, char* h, u64 i, u64 j) {
    API_TRACE("BngRwReqSendMailTo");
    if (strictApi && (reader > BNGRW_MAX_READER || !to)) return BNGRW_ERR_ARG;
    return 1;
}

EXPORT i32 BngRwReqSendUrlTo(u32 reader, i32 b, u32 c, i32* d, char* url, char* f, u64 g, u64 h) {
    API_TRACE("BngRwReqSendUrlTo");
    if (strictApi && (reader > BNGRW_MAX_READER || !url)) return BNGRW_ERR_ARG;
    return 1;
}

EXPORT i32 BngRwReqWaitTouch(u32 a1, i32 a2, u32 a3, callbackTouch callback, u64 a5) {
    API_TRACE("BngRwReqWaitTouch");
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
