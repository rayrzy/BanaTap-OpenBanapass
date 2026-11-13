// Minimal MinHook stand-in for syntax checks only.
#pragma once
typedef enum {MH_UNKNOWN=-1, MH_OK=0, MH_ERROR_X} MH_STATUS;
#define MH_ALL_HOOKS ((void*)0)
MH_STATUS MH_Initialize(); MH_STATUS MH_Uninitialize(); MH_STATUS MH_CreateHook(void*,void*,void**); MH_STATUS MH_EnableHook(void*); MH_STATUS MH_DisableHook(void*);
const char* MH_StatusToString(MH_STATUS);
