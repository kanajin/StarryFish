#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <array>
#include <map>
#include <chrono>
#include <filesystem>

namespace gbrowser {
class ExtensionPage;
// The explicit online validation mode is distinct from the unchanged offline
// policy. It is not a CEF approval, certificate exception or permission grant.
enum class RunMode { Offline, Gbf, GbfOpenNetwork };
bool IsGbfMode(RunMode mode);
// Explicit diagnostic mode admits normal web URLs, not Chrome/extension/file
// protocols. Chromium still owns TLS, CORS/CSP and extension host permissions.
bool IsBrowserNetworkUrl(const std::string& url);
bool IsNetworkPageUrl(const std::string& url, RunMode mode);
const char* RunModeSwitch(RunMode mode);
const std::string& GbfHomeUrl();
const std::string& GbfPreparationUrl();
bool IsGbfPageUrl(const std::string& url);
bool IsGbfLoginUrl(const std::string& url);
// Only explicit fixed-site login window requests retain a native opener.
bool IsGbfLoginPopup(RunMode mode, const std::string& source, const std::string& target, bool gesture);
// Eligibility for Chrome's user-facing identity prompt, not a permission grant.
bool IsGbfIdentityRequest(const std::string& current_url, const std::string& requesting_origin);
bool IsGbfResourceUrl(const std::string& url, const std::string& method,
                      const std::string& initiator, bool navigation, bool download);
// Complete browser-frame ancestry, nearest frame first; opaque/unbound workers
// cannot fabricate this scope. It is never a top-level navigation capability.
struct GbfFrameScope {
  std::array<std::string, 8> urls{};
  size_t count = 0;
  bool complete = false, subframe = false;
};
bool IsGbfCaptchaUrl(const std::string& url);
bool IsGbfCaptchaFrameNavigation(const std::string& url, const GbfFrameScope& scope);
bool IsGbfCaptchaRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download, const GbfFrameScope& scope);
// The official SDK waits for its fixed API-proxy child before mobageReady.
// This grants only that document/script under a bound first-party ancestor;
// app.mobage.jp never becomes a general navigation or social-API capability.
// Serialized "null" is accepted only for the first fixed document navigation
// in a new child; script/resource initiators still require their actual origin.
bool IsGbfLoginSupportUrl(const std::string& url);
bool IsGbfLoginSupportFrameNavigation(const std::string& url, const GbfFrameScope& scope);
bool IsGbfLoginSupportRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download, const GbfFrameScope& scope);
// A redirect-only Mobage cookie check, not a general login/asset/identity origin.
bool IsGbfLoginBridgeStart(const std::string& source, const std::string& initial_url);
bool IsGbfLoginBridgeUrl(const std::string& url);
// Diagnostic metadata only, never an admission rule. Call with CEF's parsed
// host/path components; credentials, query and fragment must not be supplied.
std::string GbfDiagnosticHost(const std::string& canonical_host);
const char* GbfDiagnosticRoute(const std::string& path);
// One context owns this policy; the UI publishes checked immutable sources,
// while IO callbacks read them. Suspension denies future online requests and
// retains file pins through context teardown, not just through visible tabs.
struct ContextResourcePolicy {
  explicit ContextResourcePolicy(RunMode mode);
  void Publish(std::vector<std::shared_ptr<const ExtensionPage>> checked);
  // UI supplies an enabled, fixed-source helper pin in this actual context.
  // A later user load may replace that helper role, never the proxy controller
  // or lease/login state. Failure cannot revive suspended/revoked networking.
  bool IncludeCheckedHelper(std::shared_ptr<const ExtensionPage> checked,
      const std::string& profile, const std::filesystem::path& cache);
  void Suspend();
  void Resume();
  void AcgpowerConfirmed(bool confirmed);
  bool NetworkReady() const;
  bool OpenNetworkDiagnostic() const { return mode_ == RunMode::GbfOpenNetwork; }
  // UI registers only a context-checked native SDK child and its live opener.
  // IO and UI then share this short-lived, single-redirect authority by browser ID.
  bool RegisterLoginPopup(int child, int opener, const std::string& source, const std::string& initial_url);
  bool BeginLoginBridge(int child, const std::string& source, const std::string& target,
      const std::string& method, int status, bool navigation, bool main, bool download);
  bool AllowsLoginBridge(int child, const std::string& target, const std::string& method,
      bool navigation, bool main, bool download) const;
  void CompleteLoginBridge(int child, const std::string& source);
  void LoginNavigation(int browser, const std::string& url, bool redirect);
  void ReleaseLoginBrowser(int browser);
  bool Allows(const std::string& url, const std::string& method,
              const std::string& initiator, bool navigation, bool download,
              const GbfFrameScope& scope = {}) const;
 private:
  const RunMode mode_;
  mutable std::mutex mutex_;
  bool published_ = false, ready_ = false, acgpower_confirmed_ = false;
  std::vector<std::shared_ptr<const ExtensionPage>> components_;
  struct LoginPopup {
    int opener = 0;
    std::string initial, target;  // In memory only; never logged or persisted.
    bool used = false;
    std::chrono::steady_clock::time_point deadline;
  };
  std::map<int, LoginPopup> login_popups_;
};
}
