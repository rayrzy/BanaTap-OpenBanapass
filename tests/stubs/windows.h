// Minimal stand-ins for the Windows headers so the sources can be
// syntax-checked and exercised on Linux (see tests/README.md). Not a full SDK.
#pragma once
#include <stddef.h>
typedef void* HMODULE; typedef unsigned long DWORD; typedef int BOOL; typedef void* LPVOID; typedef const char* LPCSTR;
typedef void* HANDLE; typedef unsigned long long ULONG_PTR;
typedef union _LI{struct{DWORD LowPart;long HighPart;};long long QuadPart;} LARGE_INTEGER;
#define WINAPI
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define PAGE_EXECUTE_READWRITE 0x40
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define TRUE 1
#define FALSE 0
#define __declspec(x)
DWORD GetFileAttributesA(const char*); BOOL QueryPerformanceCounter(LARGE_INTEGER*); DWORD GetCurrentProcessId(); DWORD GetTickCount();
BOOL WritePrivateProfileStringA(const char*,const char*,const char*,const char*);
DWORD GetPrivateProfileStringA(const char*,const char*,const char*,char*,DWORD,const char*);
unsigned GetPrivateProfileIntA(const char*,const char*,int,const char*);
BOOL GetModuleHandleExA(DWORD,LPCSTR,HMODULE*); DWORD GetModuleFileNameA(HMODULE,char*,DWORD);
HMODULE GetModuleHandle(const char*); HMODULE GetModuleHandleA(const char*);
BOOL VirtualProtect(void*,size_t,DWORD,DWORD*); void OutputDebugStringA(const char*);
BOOL FlushInstructionCache(HANDLE,const void*,size_t); HANDLE GetCurrentProcess();
short GetAsyncKeyState(int); void Sleep(DWORD); BOOL DisableThreadLibraryCalls(HMODULE);
HANDLE CreateThread(void*,size_t,DWORD(*)(LPVOID),LPVOID,DWORD,DWORD*);

typedef unsigned short WORD; typedef unsigned char BYTE; typedef short SHORT;
typedef struct {DWORD dwLowDateTime; DWORD dwHighDateTime;} FILETIME;
typedef struct {DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh; DWORD nFileSizeLow;} WIN32_FILE_ATTRIBUTE_DATA;
typedef enum {GetFileExInfoStandard} GET_FILEEX_INFO_LEVELS;
#define MB_OK 0
BOOL GetFileAttributesExA(const char*, GET_FILEEX_INFO_LEVELS, void*); long CompareFileTime(const FILETIME*, const FILETIME*);
HMODULE LoadLibraryA(const char*); void* GetProcAddress(HMODULE, const char*); BOOL MessageBeep(unsigned);

typedef void* HWND; HWND GetForegroundWindow(); DWORD GetWindowThreadProcessId(HWND, DWORD*);
#define MB_ICONASTERISK 0x40
#define MB_ICONEXCLAMATION 0x30
typedef void* PVOID; typedef long LONG;
typedef struct {unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds;} SYSTEMTIME;
void GetLocalTime(SYSTEMTIME*); BOOL DeleteFileA(const char*); DWORD GetLastError(); BOOL CloseHandle(HANDLE);
typedef struct {void* BaseAddress; void* AllocationBase; DWORD AllocationProtect; size_t RegionSize; DWORD State; DWORD Protect; DWORD Type;} MEMORY_BASIC_INFORMATION;
size_t VirtualQuery(const void*, MEMORY_BASIC_INFORMATION*, size_t);
#define MEM_COMMIT 0x1000
#define PAGE_GUARD 0x100
#define PAGE_NOACCESS 0x01
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_WRITECOPY 0x80
typedef struct {DWORD ExceptionCode; DWORD ExceptionFlags; void* ExceptionRecord; void* ExceptionAddress;} EXCEPTION_RECORD;
typedef struct {EXCEPTION_RECORD* ExceptionRecord; void* ContextRecord;} EXCEPTION_POINTERS;
typedef LONG (WINAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS*);
PVOID AddVectoredExceptionHandler(unsigned long, PVECTORED_EXCEPTION_HANDLER); unsigned long RemoveVectoredExceptionHandler(PVOID);
