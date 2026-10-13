#pragma once
#include "extension_page.h"
#include "gbf_policy.h"
#include <utility>

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <functional>
#include <deque>
#include <map>
#include <optional>
#include <array>

namespace gbrowser {
struct Rect { int x, y, width, height; };
struct Layout { Rect browser, tarou, rail; };
struct WindowLayout {
  Layout content;
  Rect address, copy, tabs, add, close, tarou_text, tarou_close, tarou_divider;
  Rect rail_title, extensions, tarou_open, proxy_open, arcarum_open, profiles, menu;
};
struct ProfileDialogLayout { Rect list, name, create, rename, use, remove, export_settings, import_settings, settings; };
int UiPixels(int logical_pixels, unsigned dpi = 96);
Rect MinimumClientSize(unsigned dpi = 96);
Layout ComputeLayout(int width, int height, bool tarou_visible, unsigned dpi = 96, unsigned tarou_percent = 50);
WindowLayout ComputeWindowLayout(int width, int height, bool tarou_visible, unsigned dpi = 96, unsigned tarou_percent = 50);
ProfileDialogLayout ComputeProfileDialogLayout(int width, int height, unsigned dpi = 96);
// FR07 in-memory panel resizing. Host cancels on geometry/capture/modal loss;
// only the explicit settings workflow may persist value(), never a gesture.
class PanelProportion {
 public:
  unsigned value() const { return value_; }
  bool dragging() const { return dragging_; }
  void Reset(unsigned percent);
  bool Begin(int pointer_x, int body_width);
  void Move(int pointer_x);
  void Finish() { dragging_ = false; }
  bool Cancel() noexcept;
  void Nudge(int delta);
 private:
  unsigned value_ = 50, initial_ = 50;
  int anchor_ = 0, body_ = 0;
  bool dragging_ = false;
};
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
void ValidateProfileName(const std::wstring& name);
void RequireSafePath(const std::filesystem::path& path);
// Conservative broker permission precheck; opens/closes root and existing
// Cache/Network directories without changing ACLs or reading their contents.
// Does not prove CEF capability grants, recursive access or storage readiness.
void RequireProfileAclPreparation(const std::filesystem::path& path);
bool IsProfileId(const std::string& id);
// Full raw Windows command line. Main process only; not a CEF child policy.
// Windows quoting may represent the token, but its decoded spelling is exact.
bool IsOfflineMainCommandLine(std::wstring_view command_line);
std::optional<RunMode> MainRunMode(std::wstring_view command_line);
bool IsContentUrlAllowed(const std::string& url, RunMode mode);
// Browser-owned request only, not an effective runtime sandbox verdict. Keep
// other feature bytes; reject disabled/decorated target conflicts and bounds.
std::string MergeNetworkSandboxFeature(const std::string& enabled, const std::string& disabled);
bool IsOfflineUrl(const std::string& url);
bool IsOfflineResourceUrl(const std::string& url, const std::string& initiator,
                          bool navigation, bool download);
const std::string& DevelopmentHomeUrl();
// Inputs/return are visual-order keys, not necessarily browser lifecycle IDs.
// Pass only positions available for activation; zero means no neighbor.
int AdjacentTabAfterClose(const std::vector<int>& available, int closed);
enum class Shortcut {
  None, Address, NewTab, ReopenTab, CloseTab, CloseWindow, NextTab, PreviousTab, Back, Forward, Reload, ReloadFresh,
  Find, FindNext, FindPrevious, ZoomIn, ZoomOut, ZoomReset
};
Shortcut MatchShortcut(int key, bool control, bool shift, bool alt, bool unsupported_modifier = false);
// Only the six local actions already permitted as DefaultPageAction.
const char* NativePageCommandName(Shortcut action);
enum class OfflineChromeAction {
  Block, Back, Forward, Reload, ReloadFresh, Stop, Home, Address, NewTab,
  CloseTab, NextTab, PreviousTab, CloseWindow, Extensions, DefaultPageAction
};
struct ChromeCommandBinding { const char* name; OfflineChromeAction action; };
const std::vector<ChromeCommandBinding>& OfflineChromeCommands();
// Per-tab navigation intent and loading state, independent of native UI/CEF.
enum class PageFailureKind { Load, Renderer };
struct PageFailure { PageFailureKind kind; int status, code; };
class PageNavigation {
 public:
  explicit PageNavigation(std::shared_ptr<const ExtensionPage> extension = nullptr,
                          RunMode mode = RunMode::Offline)
      : extension_(std::move(extension)), mode_(mode) {}
  bool Allows(const std::string& url) const {
    return IsPageUrlAllowed(url, extension_) || (!extension_ && IsContentUrlAllowed(url, mode_));
  }
  const std::shared_ptr<const ExtensionPage>& extension() const { return extension_; }
  std::string Submit(const std::string& url);
  std::string LoadingChanged(bool loading);
  void LoadStarted();
  void DocumentAvailable();
  void InvalidateDocument() { document_available_ = false; }
  void LoadFailed(int code = 0);
  void RendererTerminated(int status = 0, int code = 0);
  void CancelPending() { pending_.clear(); }
  bool ready() const { return initial_complete_ && pending_.empty(); }
  // Native worker calls need a live document, not a fully loaded options UI.
  // This never completes the ordinary navigation queue or hides load failures.
  bool document_available() const {
    return document_available_ && !failed_ && !renderer_failed_ && pending_.empty();
  }
  bool loading() const { return loading_; }
  bool failed() const { return failed_; }
  bool renderer_failed() const { return renderer_failed_; }
  std::optional<PageFailure> failure() const { return failure_; }
 private:
  std::shared_ptr<const ExtensionPage> extension_;
  RunMode mode_;
  bool initial_complete_ = false, loading_ = true, failed_ = false, renderer_failed_ = false;
  bool document_available_ = false;
  std::string pending_;
  std::optional<PageFailure> failure_;
};
// Read-only numeric callback facts, not an error diagnosis or a retry action.
// No URL, page content, profile metadata or upstream error text is included.
std::wstring DescribePageNavigation(const PageNavigation& navigation);
class BrowserCreation {
 public:
  explicit BrowserCreation(uint64_t started) : started_(started) {}
  bool Accept(uint64_t now);
  bool Expire(uint64_t now);
  void Cancel();
  bool waiting() const { return state_ == State::Pending || state_ == State::Canceled; }
  bool expired() const { return state_ == State::Expired; }
 private:
  enum class State { Pending, Ready, Canceled, Expired };
  uint64_t started_;
  State state_ = State::Pending;
};
// Track accepted creations even after their UI tab is removed. Unpublished
// browsers remain outstanding until their actual close callback arrives.
enum class BrowserAdmission { Allowed, TabLimit, CreationLimit };
class BrowserWork {
 public:
  static constexpr size_t kMaxSlots = 16, kMaxPending = 4;
  // UI-thread caller supplies all current tabs, including closing tabs.
  BrowserAdmission Admission(const std::set<int>& tracked_tabs) const;
  void Started(int tab, const std::set<int>& tracked_before_insertion);
  void CreationFinished(int tab) { pending_.erase(tab); }
  void Retire(int browser) { retiring_.insert(browser); }
  // Transfer an accepted creation only after recording its retiring instance.
  // Allocation failure retains pending until the actual close callback.
  void CreationRetired(int tab, int browser) { Retire(browser); CreationFinished(tab); }
  void Closed(int browser) { retiring_.erase(browser); }
  bool CanTransition(bool has_tabs) const { return !has_tabs && pending_.empty() && retiring_.empty(); }
 private:
  std::set<int> pending_, retiring_;
};
// Match CEF's opener-scoped popup IDs to pre-reserved product tabs. Timeout or
// opener close is not a terminal signal; keep entries until created/aborted or
// the never-created client is released. Fixed storage makes completion no-alloc.
class PopupRequests {
 public:
  void Reserve(int opener, int popup, int tab);
  int Aborted(int opener, int popup) noexcept;
  bool Finished(int tab) noexcept;
 private:
  struct Request { int opener = 0, popup = 0, tab = 0; };
  std::array<Request, BrowserWork::kMaxPending> requests_{};
};
// URLs stay owned through accepted requests until a live browser is confirmed.
// A synchronous rejection pauses automatic pumping; + may explicitly retry.
class TabRestoreQueue {
 public:
  explicit TabRestoreQueue(RunMode mode = RunMode::Offline) : mode_(mode) {}
  // A positive original visual order survives rollback; zero appends a normal
  // reopened tab. This is presentation metadata, never a reusable browser ID.
  void Push(const std::string& url, bool activate = false, int order = 0,
            std::shared_ptr<const ExtensionPage> extension = nullptr);
  const std::string& Front() const;
  bool activates() const;
  int order() const;
  const std::shared_ptr<const ExtensionPage>& extension() const;
  void Offered(int tab);
  void Rejected(int tab);
  void Confirmed(int tab) { in_flight_.erase(tab); }
  void Retiring(int tab, int browser);
  void Closed(int browser);
  bool empty() const { return urls_.empty() && in_flight_.empty(); }
  bool ready() const { return !paused_ && !urls_.empty(); }
  void Pause() { paused_ = true; }
  void Resume() { paused_ = false; }
  void Clear();
 private:
  struct Request {
    std::string url; bool activate = false; int order = 0;
    std::shared_ptr<const ExtensionPage> extension;
  };
  struct InFlight { Request request; bool awaiting = true; std::set<int> retiring; };
  std::deque<Request> urls_;
  std::map<int, InFlight> in_flight_;
  bool paused_ = false;
  RunMode mode_;
};
// Current-session URLs only, remembered by the host after a confirmed manual
// close. The host clears on successful profile activation, never persists this
// stack, and transfers an intent to TabRestoreQueue before forgetting it.
class RecentlyClosedTabs {
 public:
  explicit RecentlyClosedTabs(RunMode mode = RunMode::Offline) : mode_(mode) {}
  void Remember(const std::string& url, std::shared_ptr<const ExtensionPage> extension = nullptr);
  const std::string& Newest() const;
  const std::shared_ptr<const ExtensionPage>& extension() const;
  void ForgetNewest();
  void Clear() { urls_.clear(); }
  bool empty() const { return urls_.empty(); }
 private:
  struct Page { std::string url; std::shared_ptr<const ExtensionPage> extension; };
  std::deque<Page> urls_;
  RunMode mode_;
};
// Match a complete pointer gesture by stable tab identity, never list index.
// The host supplies 0 for an ineligible hit and cancels on capture/modal loss.
class TabCloseGesture {
 public:
  void Pressed(int tab) { pressed_ = tab > 0 ? tab : 0; }
  int Released(int tab);
  void Cancel() { pressed_ = 0; }
 private:
  int pressed_ = 0;
};
inline constexpr char kProbeCookieUrl[] = "https://gbrowser.invalid/";
inline constexpr char kProbeCookieName[] = "gbrowser-validation-marker";
enum class ProbeReadOutcome { Rejected, Failed, NotObserved, Matched, Mismatch };
class ProbeCookieRead {
 public:
  explicit ProbeCookieRead(std::string expected_profile);
  void Observe(const std::string& name, const std::string& value,
               const std::string& domain, const std::string& path,
               bool secure, bool http_only, bool persistent);
  ProbeReadOutcome Complete(bool api_accepted, bool enumeration_failed = false) const;
 private:
  std::string expected_profile_;
  size_t markers_ = 0;
  bool matching_ = true;
};

class DataRootLock {
 public:
  explicit DataRootLock(const std::filesystem::path& root);
  ~DataRootLock();
  DataRootLock(const DataRootLock&) = delete;
  DataRootLock& operator=(const DataRootLock&) = delete;
 private:
  void* handle_ = nullptr;
};

struct Profile { std::string id; std::wstring name; };
// Captured from an owned profile before an asynchronous browser request.
// Runtime identity/global observations must come from CEF, not this value.
class ProfileContextBinding {
 public:
  ProfileContextBinding(std::string profile, std::filesystem::path cache);
  bool Matches(const std::filesystem::path& actual_cache, bool present,
               bool same_context, bool global, bool shares_other_profile) const;
  const std::string& profile() const { return profile_; }
 private:
  std::string profile_;
  std::filesystem::path cache_;
};
class NewProfileStage;
class ProfileStore {
 public:
  explicit ProfileStore(std::filesystem::path root);
  std::vector<Profile> List() const;
  std::vector<Profile> ListDeleted() const;
  Profile Create(const std::wstring& name);
  // Trusted synchronous code only. Verification is mandatory; no context may
  // use staging, and no profile selection is changed by publication.
  // Own both callbacks before I/O. Replacing the caller's function objects
  // cannot replace this operation's verifier. Shared captures/dependencies
  // still belong to the caller and must remain valid through synchronous return.
  Profile CreatePrepared(const std::wstring& name,
      const std::function<void(NewProfileStage&)>& prepare,
      const std::function<bool(const NewProfileStage&)>& verify);
  std::string Selected() const;
  void Select(const std::string& id);
  // Display metadata only; ID, cache path and selection remain unchanged.
  // Returns false without writing when the validated name is identical.
  bool Rename(const std::string& id, const std::wstring& name);
  void Remove(const std::string& id, const std::string& active_id,
              bool confirmed, bool browsers_closed);
  // Same data root only, under its exclusive application lock. Move intact
  // data back to its original ID; never import, replace or select a profile.
  Profile RestoreDeleted(const std::string& id, const std::string& active_id,
                         bool confirmed, bool browsers_closed);
  std::filesystem::path CachePath(const std::string& id) const;
 private:
  std::filesystem::path ProfilePath(const std::string& id) const;
  std::filesystem::path root_;
};
// Caller holds DataRootLock through CEF shutdown. Resolve and precheck before
// CefInitialize, without committing selection. An empty store creates a default
// profile that remains preserved if later startup fails. Activation rechecks.
std::string PrepareInitialProfile(const std::filesystem::path& root);
// Explicit existing-profile candidate; independent of damaged saved selection.
// Validates saved settings and broker ACL preparation; never writes/selects.
std::string PrepareChosenProfile(const ProfileStore& store, const std::string& id);
}  // namespace gbrowser
