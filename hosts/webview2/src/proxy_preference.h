#pragma once
#include "user_state.h"
#include "proxy_initialization.h"

namespace gbrowser::webview {
struct ProxyPreferenceState {
  std::optional<ProxyAction> choice;
  bool uncertain = false;
  std::string custom_name;
};
ProxyAction DecodeInstallChoice(const std::string& bytes);
std::string EncodeProxyPreference(const ProxyPreferenceState& state);
ProxyPreferenceState DecodeProxyPreference(const std::string& bytes);
// Gbrowser-owned per-UDF record, never a substitute for worker/effective proxy
// readback. Persist unknown submission before dispatch to avoid restart replay.
class ProxyPreference {
 public:
  explicit ProxyPreference(const std::filesystem::path& data);
  const ProxyPreferenceState& state() const { return state_; }
  void BeginWrite(bool explicit_selection = false);
  void Matched(ProxyAction action, const std::string& custom_name = {});
  void NoWrite();
 private:
  void Save(ProxyPreferenceState next);
  UserStateFile file_;
  ProxyPreferenceState state_;
  bool failed_ = false;
};
}
