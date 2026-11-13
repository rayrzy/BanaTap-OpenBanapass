// Win32 emulation (ini files, clock, keyboard, focus, exception handler) so the
// real src/banapass.cpp can run on Linux in the logic tests. Linux only.
// Minimal Win32 emulation so the real banapass.cpp can run on Linux for logic tests
#include <windows.h>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <atomic>

std::atomic<int> g_keys[256];
std::atomic<bool> g_focused(true);
std::atomic<int> g_beeps(0);
std::string g_log;

static std::string lower(std::string s){for(auto&c:s)c=tolower(c);return s;}
static std::string trim(std::string s){size_t a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return"";size_t b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}

typedef std::map<std::string,std::map<std::string,std::pair<std::string,std::string>>> Ini; // lowersec -> lowerkey -> (origkey,val)
static bool loadIni(const char*path,Ini&ini,std::vector<std::string>*order=nullptr){
  std::ifstream f(path); if(!f) return false; std::string line,sec;
  while(std::getline(f,line)){ std::string t=trim(line); if(t.empty()||t[0]==';')continue;
    if(t[0]=='['){sec=lower(t.substr(1,t.find(']')-1));continue;}
    size_t e=t.find('='); if(e==std::string::npos)continue;
    std::string k=trim(t.substr(0,e)),v=trim(t.substr(e+1)); ini[sec][lower(k)]={k,v}; }
  return true;
}
static void saveIni(const char*path,const std::map<std::string,std::vector<std::pair<std::string,std::string>>>&m){}

DWORD GetFileAttributesA(const char*p){struct stat st; if(stat(p,&st))return INVALID_FILE_ATTRIBUTES; return S_ISDIR(st.st_mode)?FILE_ATTRIBUTE_DIRECTORY:0x80;}
BOOL QueryPerformanceCounter(LARGE_INTEGER*l){l->QuadPart=std::chrono::steady_clock::now().time_since_epoch().count();return 1;}
DWORD GetCurrentProcessId(){return 1234;}
DWORD GetTickCount(){return (DWORD)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())+100000;}
BOOL WritePrivateProfileStringA(const char*sec,const char*key,const char*val,const char*path){
  // rewrite whole file preserving order
  std::vector<std::pair<std::string,std::vector<std::pair<std::string,std::string>>>> secs;
  { std::ifstream f(path); std::string line; while(f&&std::getline(f,line)){ std::string t=trim(line); if(t.empty())continue;
      if(t[0]=='['){secs.push_back({t.substr(1,t.find(']')-1),{}});continue;}
      size_t e=t.find('='); if(e==std::string::npos||secs.empty())continue;
      secs.back().second.push_back({trim(t.substr(0,e)),trim(t.substr(e+1))}); } }
  bool fs=false; for(auto&s:secs) if(lower(s.first)==lower(sec)){fs=true; bool fk=false; for(auto&kv:s.second) if(lower(kv.first)==lower(key)){kv.second=val;fk=true;} if(!fk)s.second.push_back({key,val}); }
  if(!fs) secs.push_back({sec,{{key,val}}});
  std::ofstream o(path); for(auto&s:secs){o<<"["<<s.first<<"]\n"; for(auto&kv:s.second)o<<kv.first<<"="<<kv.second<<"\n"; o<<"\n";}
  return 1;
}
DWORD GetPrivateProfileStringA(const char*sec,const char*key,const char*def,char*out,DWORD size,const char*path){
  Ini ini; std::string v=def?def:""; if(loadIni(path,ini)){auto s=ini.find(lower(sec)); if(s!=ini.end()){auto k=s->second.find(lower(key)); if(k!=s->second.end()) v=k->second.second;}}
  strncpy(out,v.c_str(),size-1); out[size-1]=0; return strlen(out);
}
unsigned GetPrivateProfileIntA(const char*sec,const char*key,int def,const char*path){char b[64];char d[32];snprintf(d,32,"%d",def);GetPrivateProfileStringA(sec,key,d,b,64,path);return atoi(b);}
BOOL GetModuleHandleExA(DWORD,LPCSTR,HMODULE*){return 0;}
DWORD GetModuleFileNameA(HMODULE,char*,DWORD){return 0;}
HMODULE GetModuleHandle(const char*){return nullptr;} HMODULE GetModuleHandleA(const char*){return nullptr;}
BOOL VirtualProtect(void*,size_t,DWORD,DWORD*){return 0;}
void OutputDebugStringA(const char*s){g_log+=s;}
BOOL FlushInstructionCache(HANDLE,const void*,size_t){return 1;} HANDLE GetCurrentProcess(){return nullptr;}
short GetAsyncKeyState(int k){return g_keys[k&255]?(short)0x8000:0;}
void Sleep(DWORD ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
BOOL DisableThreadLibraryCalls(HMODULE){return 1;}
HANDLE CreateThread(void*,size_t,DWORD(*)(LPVOID),LPVOID,DWORD,DWORD*){return nullptr;}
BOOL GetFileAttributesExA(const char*p,GET_FILEEX_INFO_LEVELS,void*d){struct stat st; if(stat(p,&st))return 0; auto*x=(WIN32_FILE_ATTRIBUTE_DATA*)d; unsigned long long t=(unsigned long long)st.st_mtim.tv_sec*1000000000ull+st.st_mtim.tv_nsec; x->ftLastWriteTime.dwLowDateTime=(DWORD)t; x->ftLastWriteTime.dwHighDateTime=(DWORD)(t>>32); return 1;}
long CompareFileTime(const FILETIME*a,const FILETIME*b){return memcmp(a,b,sizeof(FILETIME))!=0;}
HMODULE LoadLibraryA(const char*){return nullptr;} void* GetProcAddress(HMODULE,const char*){return nullptr;}
BOOL MessageBeep(unsigned){g_beeps++;return 1;}
HWND GetForegroundWindow(){return g_focused?(HWND)1:(HWND)0;}
DWORD GetWindowThreadProcessId(HWND,DWORD*pid){*pid=1234;return 1;}

// ---- additions for logging / safety tests ----
PVECTORED_EXCEPTION_HANDLER g_veh=nullptr; int g_vehAdds=0, g_vehRemoves=0;
void GetLocalTime(SYSTEMTIME*t){memset(t,0,sizeof(*t));t->wHour=12;t->wMinute=34;t->wSecond=56;t->wMilliseconds=789;}
BOOL DeleteFileA(const char*p){return unlink(p)==0;}
DWORD GetLastError(){return 0;} BOOL CloseHandle(HANDLE){return 1;}
size_t VirtualQuery(const void*p,MEMORY_BASIC_INFORMATION*m,size_t){ if((unsigned long long)p<0x100000)return 0; memset(m,0,sizeof(*m)); m->State=MEM_COMMIT; m->Protect=PAGE_EXECUTE_READ; return sizeof(*m);}
PVOID AddVectoredExceptionHandler(unsigned long,PVECTORED_EXCEPTION_HANDLER h){g_veh=h;g_vehAdds++;return (PVOID)h;}
unsigned long RemoveVectoredExceptionHandler(PVOID){g_veh=nullptr;g_vehRemoves++;return 1;}
