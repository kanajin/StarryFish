#include <windows.h>
#include <winrt/base.h>
#include <iostream>
#include <string>
#include <stdexcept>
#include "gbf_hotkeys.h"
#include "character_shortcuts.h"
#include "../examples/cpp/session.h"
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
  winrt::init_apartment();
  require(gbf_abi_version()==1,"ABI");require(gbf_create(0,0)==nullptr,"invalid window accepted");
  require(std::string(gbf_probe_script()).find("function characterShortcut")!=std::string::npos,"missing probe");
  HWND w=CreateWindowExW(0,L"STATIC",L"isolated-sdk-check",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
  HWND child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,100,100,w,nullptr,nullptr,nullptr);
  void* e=gbf_create(reinterpret_cast<uintptr_t>(w),reinterpret_cast<uintptr_t>(child));require(e!=nullptr,"create");
  require(std::string(gbf_command(e,"{\"op\":\"start\",\"mask\":1}")).find("inactive-viewport")!=std::string::npos,"hidden viewport accepted");
  require(std::string(gbf_command(e,"{\"op\":\"start\",\"mask\":8388608}")).find("invalid-mask")!=std::string::npos,"mask overflow");
  require(std::string(gbf_command(e,"not-json")).find("invalid-request")!=std::string::npos,"malformed JSON");
  require(std::string(gbf_command(e,"{\"op\":\"cancel\"}")).find("idle")!=std::string::npos,"cancel");
  gbf_destroy(e);DestroyWindow(w);
  namespace c=gbrowser::webview::characters;
  c::InputQueue q;for(unsigned i=1;i<=32;i++)require(q.Push(i),"queue push");require(!q.Push(33),"queue overflow");
  for(unsigned i=1;i<=32;i++)require(q.Pop()==i,"FIFO order");
  auto b=c::Defaults();b.master=true;b[4]='D';b.enabled[8]=false;require(c::Decode(c::Encode(b))==b,"configuration roundtrip");
  c::Sequence s;require(!s.Click(3,10,20)&&s.Click(3,10,20),"two observations");require(!s.Click(6,10,20),"unexpected confirmation");
  std::cout<<"PASS SDK ABI, malformed input, hidden viewport, queue, persistence, sequence; no mouse input emitted\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
