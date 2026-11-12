#ifndef BANAPASS_H
#define BANAPASS_H

#include "helpers.h"
#include <atomic>

// Callback type
typedef void (*callbackTouch)(i32, i32, u8*, u64);

// Shared state (touched by game thread and poll thread)
extern std::atomic<bool> waitingForTouch;
extern std::atomic<bool> touchFinished;
extern std::atomic<bool> pollRunning;
extern u64 touchData;
extern callbackTouch touchCallback;

extern char accessCode[21];
extern char chipId[33];
extern u8 cardData[168];

extern int cardInsertKey;

// Core functions
void initBanapass();
void shutdownBanapass();
void reloadCard();
void patchCardDataCheck(u64 patchOffset);

// Key monitor / auto-tap thread
DWORD WINAPI cardPollThread(LPVOID lpParam);

// BngRw function declarations
extern "C" {
u64 BngRwDevReset();
u64 BngRwReadMifare();
void BngRwFin();
u64 BngRwGetFwVersion();
u64 BngRwGetStationID();
u64 BngRwGetRetryCount();
i32 BngRwIsCmdExec();
u64 BngRwReqAction();
u64 BngRwReqAiccAuth();
u64 BngRwReqBeep();
u64 BngRwReqFwCleanup();
u64 BngRwReqFwVersionUp();
i32 BngRwReqLatchID();
u64 BngRwReqLed();
i32 BngRwReqSendMail();
i32 BngRwReqSendUrl();
i32 BngRwReqSendUrlTo();
u64 BngRwReqSetLedPower();
i32 BngRwReqCancel();
u64 BngRwExReadMifareAllBlock();
u64 BngRwGetTotalRetryCount();
i32 BngRwReqSendMailTo();
u64 BngRwReqFwVersionup();
const char* BngRwGetVersion();
u64 BngRwAttach(i32 a1, char* a2, i32 a3, i32 a4, void* callback, u64 a6);
i32 BngRwInit();
i32 BngRwReqWaitTouch(u32 a1, i32 a2, u32 a3, callbackTouch callback, u64 a5);
}

#endif
