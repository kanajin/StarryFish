#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace gbrowser::webview {
inline bool ValidProxyProfileName(std::string_view name) {
  if (name.empty() || name.size()>512) return false;
  wchar_t text[128]{};
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),text,128);
  return count>0 && std::none_of(text,text+count,[](wchar_t ch){return ch<32 || ch==127;});
}
inline std::string ProxyProfileHex(std::string_view name) {
  std::string result;result.reserve(name.size()*2);
  for (unsigned char ch:name) {result.push_back("0123456789abcdef"[ch>>4]);result.push_back("0123456789abcdef"[ch&15]);}
  return result;
}
inline std::string DecodeProxyProfileHex(std::string_view hex, bool empty=false) {
  if (hex.empty() && empty) return {};
  if (hex.empty() || hex.size()>1024 || hex.size()%2) throw std::runtime_error("情景模式名称格式异常");
  std::string name;
  for (size_t i=0;i<hex.size();i+=2) {
    const auto a=std::string_view("0123456789abcdef").find(hex[i]);
    const auto b=std::string_view("0123456789abcdef").find(hex[i+1]);
    if (a==std::string_view::npos || b==std::string_view::npos) throw std::runtime_error("情景模式名称格式异常");
    name.push_back(static_cast<char>((a<<4)|b));
  }
  if (!ValidProxyProfileName(name)) throw std::runtime_error("情景模式名称格式异常");
  return name;
}
struct ProxyProfileMenu { std::vector<std::string> names; std::string current; };
inline ProxyProfileMenu DecodeProxyProfileMenu(std::string_view value) {
  if (value.size()>70000 || value.substr(0,6)!="SFPM1|") throw std::runtime_error("情景模式列表格式异常");
  const auto split=value.find('|',6);
  if (split==std::string_view::npos) throw std::runtime_error("情景模式列表格式异常");
  ProxyProfileMenu result;result.current=DecodeProxyProfileHex(value.substr(6,split-6),true);
  auto names=value.substr(split+1);
  while (!names.empty()) {
    const auto end=names.find(',');
    auto name=DecodeProxyProfileHex(names.substr(0,end));
    if (result.names.size()>=64 || std::find(result.names.begin(),result.names.end(),name)!=result.names.end())
      throw std::runtime_error("情景模式列表格式异常");
    result.names.push_back(std::move(name));
    if (end==std::string_view::npos) break;
    names.remove_prefix(end+1);
    if (names.empty()) throw std::runtime_error("情景模式列表格式异常");
  }
  if (result.names.empty() || (!result.current.empty() &&
      std::find(result.names.begin(),result.names.end(),result.current)==result.names.end()))
    throw std::runtime_error("情景模式列表格式异常");
  return result;
}
}
