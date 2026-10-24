#include <windows.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <memory>
#include <string>
#include "gbf_hotkeys.h"
#include "character_shortcuts.h"
#include "probe.h"
using namespace winrt::Windows::Data::Json;
namespace ch=gbrowser::webview::characters;
namespace {
struct Engine {
  HWND owner{},viewport{},focus{};
  POINT cursor{};
  RECT bounds{};
  ch::InputQueue queue;
  ch::Sequence seq;
  unsigned mask=0;
  uint64_t ticket=0;
  bool waiting=false;
  const wchar_t* invalid_reason=L"inactive-viewport";
  std::wstring input_reason=L"input-rejected";
  std::string locked,output;
  void cancel() {mask=0;queue.Clear();waiting=false;locked.clear();++ticket;}
  bool rect(RECT& r) {return IsWindowVisible(viewport)&&GetWindowRect(viewport,&r)&&r.right>r.left&&r.bottom>r.top;}
  HWND focused() {
    GUITHREADINFO info{sizeof(info)};
    return GetGUIThreadInfo(GetWindowThreadProcessId(owner,nullptr),&info)?info.hwndFocus:nullptr;
  }
  bool valid() {
    POINT p{}; RECT r{}; const auto f=focused();
    if(GetForegroundWindow()!=owner || !IsWindow(owner)){invalid_reason=L"inactive-viewport: foreground-changed";return false;}
    if(!rect(r)||!EqualRect(&r,&bounds)){invalid_reason=L"inactive-viewport: bounds-changed";return false;}
    if(!f){invalid_reason=L"inactive-viewport: no-native-focus";return false;}
    // Only the supplied game viewport may own keyboard focus.
    if(!(f==viewport||IsChild(viewport,f))){invalid_reason=f==owner?L"inactive-viewport: focus-on-browser-owner":L"inactive-viewport: focus-outside-renderer";return false;}
    if(!GetCursorPos(&p)||std::abs(p.x-cursor.x)>2||std::abs(p.y-cursor.y)>2){invalid_reason=L"inactive-viewport: pointer-moved";return false;}
    if(seq.Expired(GetTickCount64())){invalid_reason=L"inactive-viewport: expired";return false;}
    for(int k:{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN})
      if(GetAsyncKeyState(k)&0x8000){invalid_reason=L"inactive-viewport: mouse-or-modifier-held";return false;}
    return true;
  }
  bool begin(unsigned m) {
    mask=m;seq={};seq.started=GetTickCount64();seq.allow_confirmation=(m&(1u<<17))!=0;
    locked.clear();focus=focused();waiting=false;
    return rect(bounds)&&GetCursorPos(&cursor)&&valid();
  }
  JsonObject status(const wchar_t* op) {JsonObject j;j.SetNamedValue(L"op",JsonValue::CreateStringValue(op));return j;}
  JsonObject stop(const wchar_t* why) {cancel();auto j=status(L"cancelled");j.SetNamedValue(L"reason",JsonValue::CreateStringValue(why));return j;}
  JsonObject probe() {
    auto j=status(L"probe");waiting=true;++ticket;
    j.SetNamedValue(L"ticket",JsonValue::CreateNumberValue(static_cast<double>(ticket)));
    j.SetNamedValue(L"mask",JsonValue::CreateNumberValue(mask));
    j.SetNamedValue(L"phase",JsonValue::CreateNumberValue(seq.phase));
    j.SetNamedValue(L"locked",JsonValue::CreateStringValue(winrt::to_hstring(locked)));return j;
  }
  bool click(double x,double y) {
    if(!valid()){input_reason=invalid_reason;return false;}
    POINT p{bounds.left+static_cast<LONG>(std::lround(x)),bounds.top+static_cast<LONG>(std::lround(y))};
    const HWND hit=WindowFromPoint(p);
    if(!PtInRect(&bounds,p)){input_reason=L"input-rejected: target-outside-viewport";return false;}
    if(!(hit==viewport||IsChild(viewport,hit))){input_reason=hit==owner?L"input-rejected: hit-browser-owner":L"input-rejected: target-occluded";return false;}
    const int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(w<=1||h<=1){input_reason=L"input-rejected: invalid-desktop-bounds";return false;}
    // Validate the untouched pointer before sending movement.
    // Do not inject an intermediate move and then mistake its asynchronous
    // delivery/coordinate rounding for user interference.
    if(!valid()){input_reason=invalid_reason;return false;}
    const auto finalHit=WindowFromPoint(p);
    if(!(finalHit==viewport||IsChild(viewport,finalHit))){input_reason=L"input-rejected: target-occluded-before-click";return false;}
    INPUT events[3]{};for(auto& e:events)e.type=INPUT_MOUSE;
    events[0].mi.dx=static_cast<LONG>(std::lround((p.x-left)*65535.0/(w-1)));
    events[0].mi.dy=static_cast<LONG>(std::lround((p.y-top)*65535.0/(h-1)));
    events[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    events[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;events[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    const auto sent=SendInput(3,events,sizeof(INPUT));cursor=p;
    if(sent==2)SendInput(1,events+2,sizeof(INPUT));
    if(sent!=3){input_reason=L"input-rejected: system-click-failed";return false;}
    return true;
  }
  JsonObject command(const JsonObject& in) {
    const auto op=in.GetNamedString(L"op");
    if(op==L"cancel") {cancel();return status(L"idle");}
    if(op==L"start") {
      const auto n=in.GetNamedNumber(L"mask");
      if(n<1||n>=(1u<<23)||n!=std::floor(n))return stop(L"invalid-mask");
      if(mask) {if(!valid())return stop(invalid_reason);return status(queue.Push(static_cast<unsigned>(n))?L"queued":L"queue-full");}
      if(!begin(static_cast<unsigned>(n)))return stop(invalid_reason);
      return status(L"started");
    }
    if(!mask)return status(L"idle");
    if(!valid())return stop(invalid_reason);
    if(op==L"poll") {
      if(waiting)return stop(L"probe-already-pending");
      return probe();
    }
    if(op!=L"step"||!waiting||in.GetNamedNumber(L"ticket")!=static_cast<double>(ticket))return stop(L"stale-reply");
    waiting=false;
    const auto r=in.GetNamedObject(L"reply");
    const auto rawAction=r.GetNamedNumber(L"action");
    if(rawAction!=std::floor(rawAction)||rawAction<0||rawAction>6)return stop(L"invalid-action");
    const int action=static_cast<int>(rawAction);
    if(action==0)return stop(L"target-unavailable");
    if(action==1){seq.previous_action=0;return probe();}
    if(action==4){const auto next=queue.Pop();if(!next){cancel();return status(L"idle");}if(!begin(next))return stop(L"context-changed");return probe();}
    if(action==5){if(!(mask&(1u<<4)))return stop(L"unexpected-reload");cancel();return status(L"reload");}
    const auto context=winrt::to_string(r.GetNamedString(L"context"));
    if(context.empty()||context.size()>64||(!locked.empty()&&locked!=context))return stop(L"target-changed");
    locked=context;
    const double x=r.GetNamedNumber(L"x"),y=r.GetNamedNumber(L"y"),scale=r.GetNamedNumber(L"scale"),
      width=r.GetNamedNumber(L"width"),height=r.GetNamedNumber(L"height");
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(scale)||!std::isfinite(width)||!std::isfinite(height)||
       scale<.1||scale>16||x<0||y<0||x>=width||y>=height||width<=0||height<=0||
       std::abs(width*scale-(bounds.right-bounds.left))>scale+1||std::abs(height*scale-(bounds.bottom-bounds.top))>scale+1)
      return stop(L"viewport-mismatch");
    if(seq.Click(action,x*scale,y*scale)&&!click(x*scale,y*scale))return stop(input_reason.c_str());
    return probe();
  }
};
}
uint32_t gbf_abi_version(){return 1;}
void* gbf_create(uintptr_t owner,uintptr_t viewport){
  const auto w=reinterpret_cast<HWND>(owner),v=reinterpret_cast<HWND>(viewport);
  if(!IsWindow(w)||!IsWindow(v)||GetAncestor(v,GA_ROOT)!=w)return nullptr;
  try{auto p=std::make_unique<Engine>();p->owner=w;p->viewport=v;return p.release();}catch(...){return nullptr;}
}
const char* gbf_probe_script(){return kProbe;}
const char* gbf_command(void* instance,const char* json){
  if(!instance||!json)return "{\"op\":\"error\",\"reason\":\"invalid-argument\"}";
  auto& e=*static_cast<Engine*>(instance);
  try {if(strnlen(json,65537)>65536)throw std::runtime_error("oversize");
    e.output=winrt::to_string(e.command(JsonObject::Parse(winrt::to_hstring(json))).Stringify());
  }catch(...){e.cancel();e.output="{\"op\":\"error\",\"reason\":\"invalid-request\"}";}
  return e.output.c_str();
}
void gbf_destroy(void* instance){delete static_cast<Engine*>(instance);}
