#include "proxy_preference.h"
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
  return "GBWV-PROXY-1\n" + Name(state.choice) + (state.uncertain ? " 1\n" : " 0\n");
}
ProxyPreferenceState DecodeProxyPreference(const std::string& bytes) {
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
void ProxyPreference::Matched(ProxyAction action) { Save({action, false}); }
void ProxyPreference::NoWrite() { auto next = state_; next.uncertain = false; Save(next); }
}
