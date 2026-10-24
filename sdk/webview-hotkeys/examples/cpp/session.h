#pragma once
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <string>
#include <stdexcept>
#include "gbf_hotkeys.h"
// Thin ABI wrapper. The browser owns its async ExecuteScript callback, key events,
// UI scheduling and settings. No WebView2 SDK version is imposed by this wrapper.
class GbfSession {
  void* session_{};
public:
  GbfSession(HWND owner, HWND viewport) {
    if(gbf_abi_version()!=1)throw std::runtime_error("Unsupported GBF ABI");
    session_=gbf_create(reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(viewport));
    if(!session_)throw std::runtime_error("Invalid viewport");
  }
  ~GbfSession(){gbf_destroy(session_);}
  GbfSession(const GbfSession&)=delete;
  GbfSession& operator=(const GbfSession&)=delete;
  winrt::Windows::Data::Json::JsonObject Exchange(const std::string& json) {
    return winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(gbf_command(session_,json.c_str())));
  }
  void Cancel(){Exchange("{\"op\":\"cancel\"}");}
  static std::wstring Probe(const std::wstring& expected,const winrt::Windows::Data::Json::JsonObject& request){
    using namespace winrt::Windows::Data::Json;
    return std::wstring(winrt::to_hstring(gbf_probe_script()))+L"("+
      std::wstring(JsonValue::CreateStringValue(expected).Stringify())+L","+
      std::to_wstring(static_cast<unsigned>(request.GetNamedNumber(L"mask")))+L","+
      std::to_wstring(static_cast<int>(request.GetNamedNumber(L"phase")))+L","+
      std::wstring(JsonValue::CreateStringValue(request.GetNamedString(L"locked")).Stringify())+L")";
  }
};
