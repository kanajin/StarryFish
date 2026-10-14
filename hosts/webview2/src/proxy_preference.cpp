#include "proxy_preference.h"
#include "proxy_profiles.h"
#include <stdexcept>

namespace gbrowser::webview {
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("代理选择记录异常；不重新提交，现有配置保留"); }
std::string Name(const std::optional<ProxyAction>& choice) {
  if (!choice) return "none";
  switch (*choice) {
    case ProxyAction::Acgpower: return "acgpower";
    case ProxyAction::Direct: return "direct";
    case ProxyAction::Island: return "island";
    default: throw std::runtime_error("初始化不是成功代理选择");
  }
}
}
ProxyAction DecodeInstallChoice(const std::string& bytes) {
  for (auto action : {ProxyAction::Acgpower, ProxyAction::Direct, ProxyAction::Island})
    if (bytes == "GBWV-INSTALL-1\n" + Name(action) + "\n") return action;
  throw std::runtime_error("安装连接选择记录异常，请从代理菜单选择");
}
std::string EncodeProxyPreference(const ProxyPreferenceState& state) {
  if (state.choice==ProxyAction::Custom) {
    Require(ValidProxyProfileName(state.custom_name));
    return "GBWV-PROXY-2\n" + ProxyProfileHex(state.custom_name) + (state.uncertain ? " 1\n" : " 0\n");
  }
  Require(state.custom_name.empty());
  return "GBWV-PROXY-1\n" + Name(state.choice) + (state.uncertain ? " 1\n" : " 0\n");
}
ProxyPreferenceState DecodeProxyPreference(const std::string& bytes) {
  if (bytes.rfind("GBWV-PROXY-2\n",0)==0 && bytes.size()>16) {
    const auto end=bytes.size()-3;
    Require(bytes.substr(end)==" 0\n" || bytes.substr(end)==" 1\n");
    ProxyPreferenceState state{ProxyAction::Custom,bytes[end+1]=='1',DecodeProxyProfileHex(std::string_view(bytes).substr(13,end-13))};
    Require(EncodeProxyPreference(state)==bytes);return state;
  }
  for (const auto choice : {std::optional<ProxyAction>{}, std::optional<ProxyAction>{ProxyAction::Acgpower},
      std::optional<ProxyAction>{ProxyAction::Direct}, std::optional<ProxyAction>{ProxyAction::Island}})
    for (bool uncertain : {false, true}) {
      ProxyPreferenceState state{choice, uncertain};
      if (EncodeProxyPreference(state) == bytes) return state;
    }
  throw std::runtime_error("代理选择记录损坏，保留原文件，不自动恢复");
}
ProxyPreference::ProxyPreference(const std::filesystem::path& data) : file_(data / L"proxy-choice.state") {
  if (file_.bytes()) state_ = DecodeProxyPreference(*file_.bytes());
}
void ProxyPreference::Save(ProxyPreferenceState next) {
  const auto bytes = EncodeProxyPreference(next); Require(!failed_);
  try { file_.Save(bytes); state_ = next; } catch (...) { failed_ = true; throw; }
}
void ProxyPreference::BeginWrite() { Require(!state_.uncertain); auto next = state_; next.uncertain = true; Save(next); }
void ProxyPreference::Matched(ProxyAction action, const std::string& custom_name) { Save({action, false, custom_name}); }
void ProxyPreference::NoWrite() { auto next = state_; next.uncertain = false; Save(next); }
}
