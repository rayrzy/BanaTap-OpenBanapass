// Scripted logic tests for src/banapass.cpp: the real source is built against
// tests/win32_emulation.cpp. Timing based, so it takes about a minute.
#include <windows.h>
#include "banapass.h"
#include <thread>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <string>
extern PVECTORED_EXCEPTION_HANDLER g_veh; extern int g_vehAdds,g_vehRemoves;
extern std::atomic<int> g_keys[256]; extern std::atomic<bool> g_focused; extern std::atomic<int> g_beeps; extern std::string g_log;

static int taps=0; static char lastChip[40], lastAcc[24]; static unsigned char lastCard[168];
static void cb(i32,i32,u8*d,u64){ taps++; memcpy(lastCard,d,168); memcpy(lastChip,d+0x2C,33); memcpy(lastAcc,d+0x50,21); }
static std::string slurp(const char*p){std::ifstream f(p);std::stringstream s;s<<f.rdbuf();return s.str();}
static void put(const char*p,const char*c){std::ofstream f(p);f<<c;}
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} else printf("ok   %s\n",#c);}while(0)
static void ms(int n){usleep(n*1000);}
static void waitTouch(){ BngRwReqWaitTouch(0,0,0,cb,0); }
static void finish(){ BngRwReqWaitTouch(0,0,0,cb,0); } // consumes touchFinished

int main(){
  unlink(".\\card.ini"); unlink(".\\BanaConfig.ini"); unlink(".\\banatap.log");
  { std::ofstream big(".\\banatap.log"); std::string line(1000,'x'); for(int i=0;i<300;i++) big<<line<<"\n"; }
  // --- A: first run creates files
  initBanapass();
  CHECK(slurp(".\\banatap.log").size()<5000);            // oversized log was restarted
  CHECK(slurp(".\\banatap.log").find("started (game base")!=std::string::npos);
  CHECK(g_veh!=nullptr);
  CHECK(slurp(".\\card.ini").find("chipId")!=std::string::npos);
  std::string cfg=slurp(".\\BanaConfig.ini");
  CHECK(cfg.find("OnlyWhenFocused=1")!=std::string::npos);
  CHECK(cfg.find("AutoReadMode=always")!=std::string::npos);
  CHECK(g_log.find("NEW random card identity")!=std::string::npos);
  CHECK(slurp(".\\banatap.log").find("WARNING: card.ini not found")!=std::string::npos);
  CHECK(cfg.find("LogToFile=1")!=std::string::npos);
  shutdownBanapass();
  CHECK(g_veh==nullptr && g_vehRemoves>=1);

  // --- B: manual tap with known card, card.ini untouched
  const char* good="[card]\nchipId=7F5C9744F111111143262C3300040610\naccessCode=30764352518498791337\n";
  put(".\\card.ini",good); put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C ; tap\nAutoRead=0\n");
  initBanapass();
  std::thread th(cardPollThread,(LPVOID)0);
  ms(100); waitTouch(); ms(100);
  CHECK(taps==0);
  g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==1);
  CHECK(strcmp(lastChip,"7F5C9744F111111143262C3300040610")==0);
  CHECK(strcmp(lastAcc,"30764352518498791337")==0);
  CHECK(slurp(".\\card.ini")==good);
  { std::string L=slurp(".\\banatap.log");
    CHECK(L.find("game is waiting for a card")!=std::string::npos);
    CHECK(L.find("card sent (key)")!=std::string::npos);
    CHECK(L.find("7F5C9744F111111143262C3300040610")==std::string::npos);   // IDs never logged
    CHECK(L.find("30764352518498791337")==std::string::npos);
    CHECK(L.find("[12:34:56.789]")!=std::string::npos); }
  finish();

  // --- focus: key ignored when game not focused
  g_focused=false; waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==1);
  CHECK(slurp(".\\banatap.log").find("not in the foreground")!=std::string::npos);
  g_focused=true; ms(150);
  g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==2);
  finish();

  // --- C: invalid format is sent as written, file untouched
  const char* odd="[card]\nchipId=ABC\naccessCode=123\n";
  put(".\\card.ini",odd); waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==3); CHECK(strcmp(lastChip,"ABC")==0); CHECK(strcmp(lastAcc,"123")==0);
  CHECK(slurp(".\\card.ini")==odd); finish();

  // --- D: missing values -> nothing sent
  put(".\\card.ini","[card]\nchipId=\n"); waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==3); CHECK(g_log.find("card not sent")!=std::string::npos);
  put(".\\card.ini",good);
  // card is still waiting; tapping now works
  g_keys['C']=1; ms(500); g_keys['C']=0; CHECK(taps==4); finish();

  // --- D2: values are copied as written, like OpenBanapass (no cut to 20 / 32)
  {
    int savedTaps=taps;
    const char* longAcc="QWERTY0123456789ABCDEFGHJKLMNPRS";   // 32 characters in accessCode
    const char* shortChip="1234567890123456789";                // 19 digits in chipId
    std::string ini=std::string("[card]\naccessCode=")+longAcc+"\nchipId="+shortChip+"\n";
    put(".\\card.ini",ini.c_str()); waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0;
    CHECK(taps==savedTaps+1);
    CHECK(memcmp(lastCard+0x50,longAcc,33)==0);              // whole value + NUL at 0x50
    CHECK(memcmp(lastCard+0x2C,shortChip,20)==0);            // whole value + NUL at 0x2C
    finish();
    // a following, shorter card starts from a fresh template: nothing is left over
    put(".\\card.ini",good); waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0;
    CHECK(taps==savedTaps+2);
    CHECK(memcmp(lastCard+0x50,"30764352518498791337",21)==0);
    CHECK(lastCard[0x68]==0x00 && lastCard[0x69]==0x01);      // template bytes behind the field
    finish();
    taps=savedTaps;
  }

  // --- E: autotap, live reload (config edited at runtime)
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=true\nAutoReadDelayMs=50\nAutoReadMode=once\nBeepOnTap=1\n");
  ms(1300);
  CHECK(g_log.find("config reloaded")!=std::string::npos);
  int before=taps; g_focused=false; waitTouch(); ms(500);
  CHECK(taps==before);              // not focused -> no autotap
  g_focused=true; ms(500);
  CHECK(taps==before);              // sessionTapped already true (once) -> no autotap
  CHECK(g_beeps==0);
  g_keys['C']=1; ms(500); g_keys['C']=0; CHECK(taps==before+1); CHECK(g_beeps==1); finish();

  // --- F: toggle key re-arms; autotap fires once then not again
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=0\nAutoReadDelayMs=50\nAutoReadMode=once\nToggleAutoKey=F9\n");
  ms(1300);
  waitTouch(); ms(500); int b2=taps;
  g_keys[0x78]=1; ms(80); g_keys[0x78]=0;  // F9 = 0x78 -> toggles on and re-arms
  ms(500);
  CHECK(taps==b2+1);
  finish(); waitTouch(); ms(500);
  CHECK(taps==b2+1);                // once mode: no second autotap
  g_keys[0x78]=1; ms(80); g_keys[0x78]=0; ms(80); // off
  g_keys[0x78]=1; ms(80); g_keys[0x78]=0; // on again -> re-armed
  ms(500);
  CHECK(taps==b2+2);
  finish();

  // --- G: cooldown in always mode
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=1\nAutoReadDelayMs=50\nAutoReadMode=always\nAutoReadCooldownMs=1500\n");
  ms(1300);
  int b3=taps; waitTouch(); ms(500);   // earlier taps are old enough: autotap fires
  int b4=taps; CHECK(b4==b3+1);
  finish(); waitTouch(); ms(500);
  CHECK(taps==b4);                     // inside cooldown -> no autotap
  ms(1500);
  CHECK(taps==b4+1);                   // cooldown over -> autotap

  // --- H: stale callback pointer is not called
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=0\n");
  ms(1300);
  int b5=taps;
  finish();                                   // consume the previous "touch finished" state
  BngRwReqWaitTouch(0,0,0,(callbackTouch)0x1234,0); ms(150);
  CHECK(waitingForTouch);
  g_keys['C']=1; ms(500); g_keys['C']=0;
  CHECK(taps==b5);
  CHECK(slurp(".\\banatap.log").find("not executable memory")!=std::string::npos);
  CHECK(!waitingForTouch);

  // --- I: exception logger notes serious exceptions only
  EXCEPTION_RECORD er; EXCEPTION_POINTERS ep; ep.ExceptionRecord=&er; ep.ContextRecord=0;
  er.ExceptionCode=0x40010006; er.ExceptionAddress=(void*)0x401000; g_veh(&ep);
  er.ExceptionCode=0xC0000005; g_veh(&ep);
  { std::string L=slurp(".\\banatap.log");
    CHECK(L.find("exception 0xC0000005")!=std::string::npos);
    CHECK(L.find("0x40010006")==std::string::npos); }
  for(int i=0;i<40;i++) g_veh(&ep);
  { std::string L=slurp(".\\banatap.log"); size_t n=0,pos=0; while((pos=L.find("exception 0xC0000005",pos))!=std::string::npos){n++;pos++;}
    CHECK(n==20); }                                           // capped

  // --- J: LogToFile=0 stops file logging
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=0\nLogToFile=0\n");
  ms(1300);
  size_t sz=slurp(".\\banatap.log").size();
  waitTouch(); g_keys['C']=1; ms(500); g_keys['C']=0; finish();
  CHECK(slurp(".\\banatap.log").size()==sz);
  CHECK(g_log.find("card sent (key)")!=std::string::npos);    // still on DebugView

  // --- K: StrictApi off by default, on when asked; API first-call trace
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=0\nLogToFile=1\n");
  ms(1300);
  char url[]="http://x"; i32 dummy=0;
  CHECK(BngRwReqCancel(9)==1);                                // lenient by default
  CHECK(BngRwReqLatchID(9,0,0)==1);
  CHECK(BngRwReqSendUrlTo(9,0,0,&dummy,url,0,0,0)==1);
  CHECK(BngRwReqSendMailTo(0,0,0,&dummy,0,0,0,0,0,0)==1);
  BngRwReqLed(); BngRwReqLed(); BngRwReqLed();
  { std::string L=slurp(".\\banatap.log"); size_t n=0,pos=0; while((pos=L.find("api: BngRwReqLed called",pos))!=std::string::npos){n++;pos++;}
    CHECK(n==1);                                              // first call only
    CHECK(L.find("api: BngRwReqCancel called")!=std::string::npos); }
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=0\nStrictApi=1\nLogApiCalls=0\n");
  ms(1300);
  CHECK(BngRwReqCancel(9)==-100);
  CHECK(BngRwReqCancel(7)==1 && BngRwReqCancel(0)==1);
  CHECK(BngRwReqLatchID(8,0,0)==-100 && BngRwReqLatchID(0,0,0)==1);
  CHECK(BngRwReqSendUrlTo(0,0,0,&dummy,url,0,0,0)==1);
  CHECK(BngRwReqSendUrlTo(0,0,0,&dummy,0,0,0,0)==-100);       // missing URL
  CHECK(BngRwReqSendUrlTo(9,0,0,&dummy,url,0,0,0)==-100);     // bad reader
  CHECK(BngRwReqSendMailTo(0,0,0,&dummy,0,0,0,0,0,0)==-100);
  CHECK(BngRwReqSendMailTo(0,0,0,&dummy,url,0,0,0,0,0)==1);
  CHECK(BngRwExReadMifareAllBlock()==0xFFFFFF9Cull);          // 64-bit, not sign-extended
  CHECK(BngRwReadMifare()==0xFFFFFF9Cull);
  BngRwReqBeep();                                             // LogApiCalls=0: no trace line
  CHECK(slurp(".\\banatap.log").find("api: BngRwReqBeep called")==std::string::npos);
  CHECK(slurp(".\\banatap.log").find("BanaTap dev started")!=std::string::npos);

  // --- L: the log makes once / always visible
  auto countIn=[&](const char* what){ std::string L=slurp(".\\banatap.log"); size_t n=0,pos=0; while((pos=L.find(what,pos))!=std::string::npos){n++;pos++;} return n; };
  ms(1100);
  put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=1\nAutoReadDelayMs=50\nAutoReadMode=once\n");
  ms(1300);
  CHECK(slurp(".\\banatap.log").find("autoRead=on mode=once")!=std::string::npos);
  { size_t skips=countIn("auto tap skipped: AutoReadMode=once"); int b6=taps;
    finish(); waitTouch(); ms(500);
    CHECK(taps==b6);                                          // once: second request is not auto tapped
    CHECK(countIn("auto tap skipped: AutoReadMode=once")==skips+1);   // ... and the log says why, once
    ms(500);
    CHECK(countIn("auto tap skipped: AutoReadMode=once")==skips+1);
    ms(1100);
    put(".\\BanaConfig.ini","[BanaConfig]\nReadKey=C\nAutoRead=1\nAutoReadDelayMs=50\nAutoReadMode=always\n");
    ms(1300);
    CHECK(slurp(".\\banatap.log").find("autoRead=on mode=always")!=std::string::npos);
    CHECK(taps==b6+1);                                        // always: the pending request is served
    CHECK(slurp(".\\banatap.log").find("card sent (auto), #")!=std::string::npos);
    CHECK(slurp(".\\banatap.log").find("BanaConfig.ini: ")!=std::string::npos); }

  shutdownBanapass(); th.join();
  printf("\n%s (%d failures)\n", fails?"FAILED":"ALL PASSED", fails);
  return fails?1:0;
}
