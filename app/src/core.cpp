#include "core.h"
#include "storage_probe.h"
#include "profile_stage.h"

#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace gbrowser {
namespace fs = std::filesystem;
namespace {
void Fail(const char* message) { throw std::runtime_error(message); }
class OwnedFile {
 public:
  explicit OwnedFile(HANDLE handle) : handle_(handle) {}
  ~OwnedFile() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
  OwnedFile(const OwnedFile&) = delete;
  OwnedFile& operator=(const OwnedFile&) = delete;
  HANDLE get() const { return handle_; }
  HANDLE release() { const auto handle = handle_; handle_ = INVALID_HANDLE_VALUE; return handle; }
 private:
  HANDLE handle_;
};
Rect ClipToClient(Rect rect, int width, int height) {
  const int left = std::clamp(rect.x, 0, width), top = std::clamp(rect.y, 0, height);
  const int right = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rect.x) + rect.width, 0, width));
  const int bottom = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rect.y) + rect.height, 0, height));
  return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}
std::string ReadName(const fs::path& path) {
  RequireSafePath(path);
  OwnedFile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) Fail("无法读取资料信息，文件可能缺失或正被占用。请保留现有文件，关闭原型后检查。");
  BY_HANDLE_FILE_INFORMATION info{};
  LARGE_INTEGER size{};
  if (GetFileType(file.get()) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.get(), &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      !GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 || size.QuadPart > 256)
    Fail("资料信息文件类型或大小异常。请保留文件并停止使用该资料，不要手动覆盖。");
  std::array<char, 256> bytes{};
  DWORD read = 0;
  if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) ||
      read != static_cast<DWORD>(size.QuadPart)) Fail("无法完整读取资料信息。请保留文件，关闭原型后重试。");
  return std::string(bytes.data(), read);
}
void CheckProfileTree(const fs::path& path) {
  // FR04 deletion/recovery move intact data. Bound the same path inspection
  // on both routes; do not copy databases, repair metadata or follow links.
  // This is a point-in-time check under DataRootLock, not a same-SID snapshot.
  size_t entries = 0;
  for (auto cursor = fs::recursive_directory_iterator(path); cursor != fs::recursive_directory_iterator(); ++cursor) {
    if (++entries > 100000 || cursor.depth() > 32)
      Fail("资料目录超过移动检查范围，本次操作停止，原目录保留。");
    RequireSafePath(cursor->path());
  }
}
std::string NewId() {
  std::array<unsigned char, 16> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) Fail("无法生成资料标识，本次创建或保存已停止。请稍后重试。");
  constexpr char hex[] = "0123456789abcdef";
  std::string id = "p-";
  for (auto value : bytes) { id += hex[value >> 4]; id += hex[value & 15]; }
  return id;
}
void WriteNewMetadata(const fs::path& path, const std::string& bytes,
                      const char* create_error, const char* write_error) {
  RequireSafePath(path);
  OwnedFile file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) Fail(create_error);
  DWORD written = 0;
  if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
      written != bytes.size() || !FlushFileBuffers(file.get())) Fail(write_error);
}
}  // namespace

void ValidateProfileName(const std::wstring& name) {
  if (name.empty() || name.size() > 64 ||
      std::all_of(name.begin(), name.end(), [](wchar_t c) { return c == L' '; }) ||
      std::any_of(name.begin(), name.end(), [](wchar_t c) {
        return c < 32 || (c >= 0x7f && c <= 0x9f);
      }))
    Fail("资料名称不能为空、全为半角空格或包含控制字符；最多64个UTF-16单位（部分字符占2个单位）。请检查输入；若来自既有文件，请保留文件并停止操作。");
  (void)WideToUtf8(name);
}
int UiPixels(int logical_pixels, unsigned dpi) {
  // Zero is the failure return from GetDpiForWindow; retain the 96-DPI baseline.
  if (!dpi) dpi = 96;
  if (dpi > 768 || logical_pixels < 0 || logical_pixels > 10000) Fail("Invalid UI scale");
  return MulDiv(logical_pixels, static_cast<int>(dpi), 96);
}
Rect MinimumClientSize(unsigned dpi) { return {0, 0, UiPixels(760, dpi), UiPixels(420, dpi)}; }
Layout ComputeLayout(int width, int height, bool visible, unsigned dpi, unsigned tarou_percent) {
  if (tarou_percent < 25 || tarou_percent > 60) Fail("Invalid Tarou panel proportion");
  width = std::max(0, width); height = std::max(0, height);
  const int rail = std::min(UiPixels(68, dpi), width);
  const int body = std::max(0, width - rail);
  const int top = std::min(UiPixels(48, dpi), height);
  const int body_height = height - top;
  const int pane = visible ? static_cast<int>(static_cast<int64_t>(body) * tarou_percent / 100) : 0;
  return {{0, top, body - pane, body_height},
          {body - pane, top, pane, body_height},
          {body, top, rail, body_height}};
}
WindowLayout ComputeWindowLayout(int width, int height, bool visible, unsigned dpi, unsigned tarou_percent) {
  width = std::max(0, width); height = std::max(0, height);
  const auto s = [dpi](int value) { return UiPixels(value, dpi); };
  const auto fit = [width, height](Rect rect) { return ClipToClient(rect, width, height); };
  WindowLayout result{};
  result.content = ComputeLayout(width, height, visible, dpi, tarou_percent);
  const auto& pane = result.content.tarou;
  const auto& rail = result.content.rail;
  result.address = fit({s(8), s(10), s(236), s(28)});
  result.copy = fit({s(250), s(10), s(60), s(28)});
  result.tabs = fit({s(318), s(8), std::max(0, width - s(406)), s(34)});
  result.add = fit({width - s(80), s(10), s(32), s(28)});
  result.close = fit({width - s(42), s(10), s(32), s(28)});
  result.tarou_text = fit({pane.x + s(18), pane.y + s(48), std::max(0, pane.width - s(36)), std::max(0, pane.height - s(68))});
  result.tarou_close = fit({pane.x + pane.width - s(70), pane.y + s(8), s(60), s(26)});
  // Reserve the handle inside the panel gutter; CEF ends at pane.x and the
  // panel text starts 18 logical pixels later, so neither covers this child.
  result.tarou_divider = fit({pane.x, pane.y, visible ? std::min(s(6), pane.width) : 0, pane.height});
  result.rail_title = fit({rail.x, rail.y + s(12), rail.width, s(24)});
  result.extensions = fit({rail.x + s(6), rail.y + s(44), s(56), s(32)});
  result.tarou_open = fit({rail.x + s(6), rail.y + s(84), s(56), s(32)});
  result.proxy_open = fit({rail.x + s(6), rail.y + s(124), s(56), s(32)});
  result.arcarum_open = fit({rail.x + s(6), rail.y + s(164), s(56), s(32)});
  result.profiles = fit({rail.x + s(6), height - s(84), s(56), s(32)});
  result.menu = fit({rail.x + s(6), height - s(44), s(56), s(32)});
  return result;
}
void PanelProportion::Reset(unsigned percent) {
  if (percent < 25 || percent > 60) Fail("Invalid Tarou panel proportion");
  value_ = percent; dragging_ = false;
}
bool PanelProportion::Begin(int pointer_x, int body_width) {
  if (dragging_ || body_width <= 0) return false;
  anchor_ = pointer_x; body_ = body_width; initial_ = value_; dragging_ = true;
  return true;
}
void PanelProportion::Move(int pointer_x) {
  if (!dragging_) return;
  // Anchor to the original click, not the moving child or last rounded value.
  // Signed 64-bit math keeps capture coordinates outside the window bounded.
  const int64_t scaled = (static_cast<int64_t>(pointer_x) - anchor_) * 100;
  const int64_t delta = scaled >= 0 ? (scaled + body_ / 2) / body_ : -((-scaled + body_ / 2) / body_);
  value_ = static_cast<unsigned>(std::clamp<int64_t>(static_cast<int64_t>(initial_) - delta, 25, 60));
}
bool PanelProportion::Cancel() noexcept {
  if (!dragging_) return false;
  const bool changed = value_ != initial_;
  value_ = initial_; dragging_ = false; return changed;
}
void PanelProportion::Nudge(int delta) {
  Cancel();
  value_ = static_cast<unsigned>(std::clamp<int64_t>(static_cast<int64_t>(value_) + delta, 25, 60));
}
ProfileDialogLayout ComputeProfileDialogLayout(int width, int height, unsigned dpi) {
  width = std::max(0, width); height = std::max(0, height);
  const auto s = [dpi](int value) { return UiPixels(value, dpi); };
  const auto fit = [width, height](Rect rect) { return ClipToClient(rect, width, height); };
  const int list_height = std::max(0, height - s(198));
  const int backup_width = std::max(0, width - s(40)) / 2;
  return {fit({s(16), s(16), std::max(0, width - s(32)), list_height}),
          fit({s(16), list_height + s(28), std::max(0, width - s(152)), s(28)}),
          fit({width - s(126), list_height + s(28), s(110), s(28)}),
          fit({s(16), height - s(46), s(110), s(30)}),
          fit({width - s(244), height - s(46), s(110), s(30)}),
          fit({width - s(126), height - s(46), s(110), s(30)}),
          fit({s(16), height - s(86), backup_width, s(30)}),
          fit({s(24) + backup_width, height - s(86), std::max(0, width - s(40) - backup_width), s(30)}),
          fit({s(16), height - s(126), std::max(0, width - s(32)), s(30)})};
}

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  const int length = static_cast<int>(text.size());
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
  if (!count) Fail("Invalid UTF-8");
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), count);
  return result;
}
std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int length = static_cast<int>(text.size());
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
  if (!count) Fail("Invalid Unicode");
  std::string result(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), count, nullptr, nullptr);
  return result;
}
void RequireSafePath(const fs::path& input) {
  if (input.empty() || input.native().find(L'\0') != std::wstring::npos)
    Fail("Empty/NUL path rejected before native interpretation");
  auto path = fs::absolute(input).lexically_normal();
  while (!path.empty()) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
      Fail("Link/reparse path rejected");
    if (attrs == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND &&
        GetLastError() != ERROR_PATH_NOT_FOUND) Fail("Cannot inspect path");
    const auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
}
bool IsProfileId(const std::string& id) {
  return id.size() == 34 && id.substr(0, 2) == "p-" &&
    std::all_of(id.begin() + 2, id.end(), [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::string MergeNetworkSandboxFeature(const std::string& enabled, const std::string& disabled) {
  constexpr std::string_view required = "NetworkServiceSandbox";
  for (const auto* value : {&enabled, &disabled})
    if (value->size() > 32766 || value->find('\0') != std::string::npos)
      Fail("Invalid startup feature list");
  const auto inspect = [&](const std::string& value, bool enabling) {
    bool present = false;
    size_t start = 0;
    while (start < value.size()) {
      const auto comma = value.find(',', start);
      auto token = std::string_view(value).substr(start,
          comma == std::string::npos ? value.size() - start : comma - start);
      const auto first = token.find_first_not_of(" \t\r\n\f\v");
      if (first != std::string_view::npos) {
        token = token.substr(first, token.find_last_not_of(" \t\r\n\f\v") - first + 1);
        auto name = token;
        if (name.front() == '*') name.remove_prefix(1);
        name = name.substr(0, name.find_first_of("<:"));
        const auto last = name.find_last_not_of(" \t\r\n\f\v");
        if (last != std::string_view::npos) name = name.substr(0, last + 1);
        if (name == required) {
          if (!enabling || token != required)
            Fail("NetworkServiceSandbox has conflicting startup feature configuration");
          present = true;
        }
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    return present;
  };
  inspect(disabled, false);
  if (inspect(enabled, true)) return enabled;
  const auto extra = required.size() + (enabled.empty() ? 0 : 1);
  if (enabled.size() > 32766 - extra) Fail("Startup feature list exceeds argument budget");
  return enabled.empty() ? std::string(required) : enabled + "," + std::string(required);
}
namespace {
std::optional<std::wstring> MainArgument(std::wstring_view command_line) {
  // Bound the raw input and reject embedded NUL before the Windows parser can
  // silently truncate it. Empty input has special executable-path semantics.
  if (command_line.empty() || command_line.size() > 32766 ||
      command_line.find(L'\0') != std::wstring_view::npos) return std::nullopt;
  // A bounded view may not be terminated at its end; own the exact view so the
  // parser cannot read a hidden suffix outside it.
  const std::wstring bounded(command_line);
  int count = 0;
  auto arguments = CommandLineToArgvW(bounded.c_str(), &count);
  if (!arguments) return std::nullopt;
  struct ArgumentOwner {
    LPWSTR* arguments;
    ~ArgumentOwner() { LocalFree(arguments); }
  } owner{arguments};
  if ((count != 1 && count != 2) || arguments[0][0] == L'\0') return std::nullopt;
  if (count == 1) return std::wstring();
  if (arguments[1][0] == L'\0') return std::nullopt;
  return std::wstring(arguments[1]);
}
}
bool IsDefaultMainCommandLine(std::wstring_view command_line) {
  const auto argument = MainArgument(command_line);
  return argument && argument->empty();
}
std::optional<RunMode> MainRunMode(std::wstring_view command_line) {
  const auto parsed = MainArgument(command_line);
  if (!parsed) return std::nullopt;
  const auto& marker = *parsed;
  if (marker == L"--validation-offline") return RunMode::Offline;
  if (marker == L"--validation-gbf") return RunMode::Gbf;
  if (marker == L"--validation-gbf-open-network") return RunMode::GbfOpenNetwork;
  return std::nullopt;
}
bool IsOfflineMainCommandLine(std::wstring_view command_line) {
  return MainRunMode(command_line) == RunMode::Offline;
}
bool IsContentUrlAllowed(const std::string& url, RunMode mode) {
  return IsOfflineUrl(url) || (IsGbfMode(mode) &&
      (url == GbfPreparationUrl() || IsNetworkPageUrl(url, mode)));
}
ProbeCookieRead::ProbeCookieRead(std::string expected_profile)
    : expected_profile_(std::move(expected_profile)) {
  if (!IsProfileId(expected_profile_)) Fail("Invalid validation profile ID");
}
void ProbeCookieRead::Observe(const std::string& name, const std::string& value,
    const std::string& domain, const std::string& path, bool secure,
    bool http_only, bool persistent) {
  if (name != kProbeCookieName) return;
  ++markers_;
  matching_ = matching_ && value == expected_profile_ && domain == "gbrowser.invalid" &&
              path == "/" && secure && http_only && persistent;
}
ProbeReadOutcome ProbeCookieRead::Complete(bool api_accepted, bool enumeration_failed) const {
  if (!api_accepted) return ProbeReadOutcome::Rejected;
  if (enumeration_failed) return ProbeReadOutcome::Failed;
  if (!markers_) return ProbeReadOutcome::NotObserved;
  return markers_ == 1 && matching_ ? ProbeReadOutcome::Matched : ProbeReadOutcome::Mismatch;
}
const std::string& DevelopmentHomeUrl() {
  static const std::string url = "data:text/html,%3Cmeta%20charset=utf-8%3E%3Ctitle%3E%E7%A6%BB%E7%BA%BF%E5%BC%80%E5%8F%91%E9%A1%B5%3C/title%3E%3Cstyle%3Ebody{font:18px%20Segoe%20UI;background:%23f2f4f5;color:%232a353e;margin:56px}h1{font-size:30px}%3C/style%3E%3Ch1%3EGbrowser%3C/h1%3E%3Cp%3EWindows%20offline%20development%20session%3C/p%3E%3Cp%3EGBF%20navigation%20awaits%20the%20runtime%20security%20gate.%3C/p%3E";
  return url;
}
bool IsOfflineUrl(const std::string& url) {
  return url == "about:blank" || url == "chrome://sandbox/" ||
    url == "chrome://version/" || url == "chrome://extensions/" ||
    url == DevelopmentHomeUrl() || url == kStorageProbePage;
}
int AdjacentTabAfterClose(const std::vector<int>& available, int closed) {
  int before = 0, after = 0;
  for (int id : available) {
    if (id > closed && (!after || id < after)) after = id;
    if (id > 0 && id < closed && id > before) before = id;
  }
  return after ? after : before;
}
Shortcut MatchShortcut(int key, bool control, bool shift, bool alt, bool unsupported_modifier) {
  if (unsupported_modifier) return Shortcut::None;
  if (alt) {
    if (control || shift) return Shortcut::None;
    if (key == VK_LEFT) return Shortcut::Back;
    if (key == VK_RIGHT) return Shortcut::Forward;
    return Shortcut::None;
  }
  if (!control && key == VK_F3) return shift ? Shortcut::FindPrevious : Shortcut::FindNext;
  if (key == VK_F5) return control || shift ? Shortcut::ReloadFresh : Shortcut::Reload;
  if (!control) return Shortcut::None;
  if (key == 'G') return shift ? Shortcut::FindPrevious : Shortcut::FindNext;
  if (key == VK_OEM_PLUS || (key == VK_ADD && !shift)) return Shortcut::ZoomIn;
  if (key == 'R') return shift ? Shortcut::ReloadFresh : Shortcut::Reload;
  if (key == VK_TAB) return shift ? Shortcut::PreviousTab : Shortcut::NextTab;
  if (key == 'T') return shift ? Shortcut::ReopenTab : Shortcut::NewTab;
  if (key == 'W') return shift ? Shortcut::CloseWindow : Shortcut::CloseTab;
  if (shift) return Shortcut::None;
  if (key == 'F') return Shortcut::Find;
  if (key == VK_OEM_MINUS || key == VK_SUBTRACT) return Shortcut::ZoomOut;
  if (key == '0' || key == VK_NUMPAD0) return Shortcut::ZoomReset;
  if (key == 'L') return Shortcut::Address;
  return Shortcut::None;
}
const char* NativePageCommandName(Shortcut action) {
  switch (action) {
    case Shortcut::Find: return "IDC_FIND";
    case Shortcut::FindNext: return "IDC_FIND_NEXT";
    case Shortcut::FindPrevious: return "IDC_FIND_PREVIOUS";
    case Shortcut::ZoomIn: return "IDC_ZOOM_PLUS";
    case Shortcut::ZoomOut: return "IDC_ZOOM_MINUS";
    case Shortcut::ZoomReset: return "IDC_ZOOM_NORMAL";
    default: return nullptr;
  }
}
const std::vector<ChromeCommandBinding>& OfflineChromeCommands() {
  using A = OfflineChromeAction;
  static const std::vector<ChromeCommandBinding> commands = {
    {"IDC_BACK", A::Back}, {"IDC_FORWARD", A::Forward}, {"IDC_RELOAD", A::Reload},
    {"IDC_RELOAD_BYPASSING_CACHE", A::ReloadFresh}, {"IDC_STOP", A::Stop}, {"IDC_HOME", A::Home},
    {"IDC_FOCUS_LOCATION", A::Address}, {"IDC_NEW_TAB", A::NewTab}, {"IDC_CLOSE_TAB", A::CloseTab},
    {"IDC_SELECT_NEXT_TAB", A::NextTab}, {"IDC_SELECT_PREVIOUS_TAB", A::PreviousTab},
    {"IDC_CLOSE_WINDOW", A::CloseWindow}, {"IDC_MANAGE_EXTENSIONS", A::Extensions},
    {"IDC_CUT", A::DefaultPageAction}, {"IDC_COPY", A::DefaultPageAction}, {"IDC_PASTE", A::DefaultPageAction},
    {"IDC_FIND", A::DefaultPageAction}, {"IDC_FIND_NEXT", A::DefaultPageAction},
    {"IDC_FIND_PREVIOUS", A::DefaultPageAction}, {"IDC_ZOOM_PLUS", A::DefaultPageAction},
    {"IDC_ZOOM_MINUS", A::DefaultPageAction}, {"IDC_ZOOM_NORMAL", A::DefaultPageAction},
    {"IDC_CONTENT_CONTEXT_COPY", A::DefaultPageAction}, {"IDC_CONTENT_CONTEXT_CUT", A::DefaultPageAction},
    {"IDC_CONTENT_CONTEXT_PASTE", A::DefaultPageAction},
    {"IDC_CONTENT_CONTEXT_PASTE_AND_MATCH_STYLE", A::DefaultPageAction},
    {"IDC_CONTENT_CONTEXT_SELECTALL", A::DefaultPageAction},
    {"IDC_CONTENT_CONTEXT_UNDO", A::DefaultPageAction}, {"IDC_CONTENT_CONTEXT_REDO", A::DefaultPageAction}
  };
  return commands;
}
std::string PageNavigation::Submit(const std::string& url) {
  if (!Allows(url)) throw std::runtime_error("Unsafe navigation intent rejected");
  InvalidateDocument();
  if (!initial_complete_) { pending_ = url; return {}; }
  return url;
}
std::string PageNavigation::LoadingChanged(bool loading) {
  loading_ = loading && !renderer_failed_;
  if (loading) return {};
  initial_complete_ = true;
  DocumentAvailable();  // Fallback when CEF's earlier document callback is absent.
  if (renderer_failed_ || pending_.empty()) return {};
  auto url = std::move(pending_); pending_.clear();
  InvalidateDocument();
  loading_ = true;
  return url;
}
void PageNavigation::LoadStarted() {
  InvalidateDocument();
  failed_ = false; renderer_failed_ = false; loading_ = true;
  failure_.reset();
}
void PageNavigation::DocumentAvailable() {
  if (!failed_ && !renderer_failed_ && pending_.empty()) document_available_ = true;
}
void PageNavigation::LoadFailed(int code) {
  InvalidateDocument();
  failed_ = true;
  // Late load errors must not replace the stopped renderer's facts.
  if (!renderer_failed_) failure_ = PageFailure{PageFailureKind::Load, 0, code};
}
void PageNavigation::RendererTerminated(int status, int code) {
  InvalidateDocument();
  renderer_failed_ = true; loading_ = false;
  initial_complete_ = true; pending_.clear();
  failure_ = PageFailure{PageFailureKind::Renderer, status, code};
}
std::wstring DescribePageNavigation(const PageNavigation& navigation) {
  const auto failure = navigation.failure();
  if (failure) {
    if (failure->kind == PageFailureKind::Renderer)
      return L"页面进程已停止。\n终止状态：" + std::to_wstring(failure->status) +
          L"\n进程错误码：" + std::to_wstring(failure->code) +
          L"\n可手动刷新重试；若再次失败，请正常关闭后重启。";
    return L"页面加载失败。\n加载错误码：" + std::to_wstring(failure->code) +
        (navigation.loading() ? L"\n加载状态：仍在加载。" : L"\n加载状态：已结束。") +
        L"\n可等待加载结束后手动刷新重试。";
  }
  if (navigation.loading()) return L"页面正在加载。";
  if (!navigation.ready()) return L"页面尚未就绪。";
  return L"页面已就绪；当前未记录加载或页面进程错误。";
}
bool BrowserCreation::Expire(uint64_t now) {
  if (waiting() && now - started_ >= 30000) { state_ = State::Expired; return true; }
  return false;
}
bool BrowserCreation::Accept(uint64_t now) {
  Expire(now);
  if (state_ != State::Pending) return false;
  state_ = State::Ready; return true;
}
void BrowserCreation::Cancel() {
  if (state_ == State::Pending) state_ = State::Canceled;
}
BrowserAdmission BrowserWork::Admission(const std::set<int>& tracked_tabs) const {
  for (int tab : tracked_tabs) if (tab <= 0) Fail("Invalid tracked tab identity");
  size_t slots = tracked_tabs.size();
  if (slots >= kMaxSlots) return BrowserAdmission::TabLimit;
  for (int tab : pending_) if (!tracked_tabs.count(tab) && ++slots >= kMaxSlots)
    return BrowserAdmission::TabLimit;
  if (retiring_.size() >= kMaxSlots - slots) return BrowserAdmission::TabLimit;
  return pending_.size() >= kMaxPending ? BrowserAdmission::CreationLimit : BrowserAdmission::Allowed;
}
void BrowserWork::Started(int tab, const std::set<int>& tracked_before_insertion) {
  if (tab <= 0 || pending_.count(tab) || tracked_before_insertion.count(tab))
    Fail("Duplicate or invalid pending browser identity");
  if (Admission(tracked_before_insertion) != BrowserAdmission::Allowed)
    Fail("Browser creation budget exhausted");
  pending_.insert(tab);
}
void PopupRequests::Reserve(int opener, int popup, int tab) {
  if (opener <= 0 || popup < 0 || tab <= 0) Fail("Invalid popup request identity");
  Request* available = nullptr;
  for (auto& request : requests_) {
    if (request.tab == tab || (request.opener == opener && request.popup == popup))
      Fail("Duplicate popup request identity");
    if (!request.tab && !available) available = &request;
  }
  if (!available) Fail("Popup request budget exhausted");
  *available = {opener, popup, tab};
}
int PopupRequests::Aborted(int opener, int popup) noexcept {
  for (auto& request : requests_) if (request.opener == opener && request.popup == popup && request.tab) {
    const auto tab = request.tab; request = {}; return tab;
  }
  return 0;
}
bool PopupRequests::Finished(int tab) noexcept {
  for (auto& request : requests_) if (request.tab == tab && tab > 0) { request = {}; return true; }
  return false;
}
void TabRestoreQueue::Push(const std::string& url, bool activate, int order,
                          std::shared_ptr<const ExtensionPage> extension) {
  if (order < 0 || (extension && extension->target()) ||
      !(IsPageUrlAllowed(url, extension) || (!extension && IsContentUrlAllowed(url, mode_))) ||
      urls_.size() + in_flight_.size() >= BrowserWork::kMaxSlots)
    Fail("Invalid or excessive restored tabs");
  urls_.push_back({url, activate, order, std::move(extension)});
}
const std::shared_ptr<const ExtensionPage>& TabRestoreQueue::extension() const {
  if (urls_.empty()) Fail("No tab awaiting restoration");
  return urls_.front().extension;
}
const std::string& TabRestoreQueue::Front() const {
  if (urls_.empty()) Fail("No tab awaiting restoration");
  return urls_.front().url;
}
bool TabRestoreQueue::activates() const {
  if (urls_.empty()) Fail("No tab awaiting restoration");
  return urls_.front().activate;
}
int TabRestoreQueue::order() const {
  if (urls_.empty()) Fail("No tab awaiting restoration");
  return urls_.front().order;
}
void TabRestoreQueue::Offered(int tab) {
  if (!ready() || tab <= 0 || in_flight_.count(tab)) Fail("Invalid restoration offer");
  in_flight_.emplace(tab, InFlight{urls_.front(), true, {}});
  urls_.pop_front();
}
void TabRestoreQueue::Rejected(int tab) {
  const auto it = in_flight_.find(tab);
  if (it == in_flight_.end()) return;
  if (!it->second.awaiting || !it->second.retiring.empty()) Fail("Restoration request is not synchronously rejectable");
  urls_.push_front(it->second.request); in_flight_.erase(it); Pause();
}
void TabRestoreQueue::Retiring(int tab, int browser) {
  const auto it = in_flight_.find(tab);
  if (it == in_flight_.end()) return;
  if (browser <= 0) Fail("Invalid retiring restoration browser");
  it->second.retiring.insert(browser); it->second.awaiting = false;
}
void TabRestoreQueue::Closed(int browser) {
  for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
    if (!it->second.retiring.count(browser)) continue;
    if (it->second.retiring.size() == 1) {
      urls_.push_front(it->second.request); in_flight_.erase(it);
    } else it->second.retiring.erase(browser);
    return;
  }
}
void TabRestoreQueue::Clear() {
  if (!in_flight_.empty()) Fail("Cannot discard unsettled restoration requests");
  urls_.clear(); paused_ = false;
}
void RecentlyClosedTabs::Remember(const std::string& url, std::shared_ptr<const ExtensionPage> extension) {
  if ((extension && extension->target()) ||
      !(IsPageUrlAllowed(url, extension) || (!extension && IsContentUrlAllowed(url, mode_))))
    Fail("Unsafe recently closed tab rejected");
  urls_.push_back({url, std::move(extension)});
  if (urls_.size() > BrowserWork::kMaxSlots) urls_.pop_front();
}
const std::string& RecentlyClosedTabs::Newest() const {
  if (urls_.empty()) Fail("No recently closed tab");
  return urls_.back().url;
}
const std::shared_ptr<const ExtensionPage>& RecentlyClosedTabs::extension() const {
  if (urls_.empty()) Fail("No recently closed tab");
  return urls_.back().extension;
}
void RecentlyClosedTabs::ForgetNewest() {
  if (urls_.empty()) Fail("No recently closed tab");
  urls_.pop_back();
}
int TabCloseGesture::Released(int tab) {
  const int pressed = pressed_; pressed_ = 0;
  return pressed > 0 && tab == pressed ? pressed : 0;
}
bool IsOfflineResourceUrl(const std::string& url, const std::string& initiator,
                          bool navigation, bool download) {
  if (download) return false;
  if (url == kStorageProbePage) return navigation;
  if (IsOfflineUrl(url)) return true;
  if (navigation) return false;
  if (url == kStorageProbeScript) return initiator == kStorageProbeOrigin;
  // CEF supplies a serialized origin. Only these exact internal documents
  // may fetch their packaged scripts/styles/images; top-level policy is exact.
  // Extension content, external resources and downloads remain denied.
  const bool version = initiator == "chrome://version";
  const bool sandbox = initiator == "chrome://sandbox";
  const bool extensions = initiator == "chrome://extensions";
  if (!version && !sandbox && !extensions) return false;
  const auto starts = [&url](const char* prefix) { return url.rfind(prefix, 0) == 0; };
  return starts("chrome://resources/") || starts("chrome://theme/") ||
         (version && starts("chrome://version/")) ||
         (sandbox && starts("chrome://sandbox/")) ||
         (extensions && starts("chrome://extensions/"));
}
DataRootLock::DataRootLock(const fs::path& root) {
  RequireSafePath(root);  // Inspect raw input before absolute/lexical conversion.
  const auto path = fs::absolute(root).lexically_normal() / L"process.lock";
  RequireSafePath(path);
  OwnedFile file(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    Fail("Another Gbrowser process owns the data root, or the root is inaccessible");
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (GetFileType(file.get()) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.get(), &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    Fail("Invalid data root lock file; existing data preserved");
  handle_ = file.release();
}
DataRootLock::~DataRootLock() {
  if (handle_) CloseHandle(handle_);
}

ProfileContextBinding::ProfileContextBinding(std::string profile, fs::path cache)
    : profile_(std::move(profile)), cache_(std::move(cache)) {
  if (cache_.empty() || cache_.native().find(L'\0') != std::wstring::npos)
    Fail("Empty/NUL context path rejected before normalization");
  cache_ = cache_.lexically_normal();
  cache_.make_preferred();
  if (!IsProfileId(profile_) || !cache_.is_absolute() || cache_.native().find(L'\0') != std::wstring::npos ||
      cache_.filename() != Utf8ToWide(profile_) || cache_.parent_path().filename() != L"Profiles")
    Fail("Invalid persistent profile context binding");
}
bool ProfileContextBinding::Matches(const fs::path& actual_cache, bool present,
                                   bool same_context, bool global, bool shares_other_profile) const {
  if (!present || !same_context || global || shares_other_profile || !actual_cache.is_absolute() ||
      actual_cache.native().find(L'\0') != std::wstring::npos) return false;
  auto actual = actual_cache.lexically_normal(); actual.make_preferred();
  return actual == cache_;
}

ProfileStore::ProfileStore(fs::path root) {
  RequireSafePath(root);
  root_ = fs::absolute(root).lexically_normal();
  RequireSafePath(root_);
  fs::create_directories(root_ / L"Profiles");
  RequireSafePath(root_ / L"Profiles");
}
fs::path ProfileStore::ProfilePath(const std::string& id) const {
  if (!IsProfileId(id)) Fail("资料标识异常，已停止操作。请保留资料文件并关闭原型后检查。");
  const auto path = root_ / L"Profiles" / Utf8ToWide(id);
  RequireSafePath(path);
  return path;
}
namespace {
std::vector<Profile> ListProfileDirectory(const fs::path& directory, bool missing_allowed) {
  RequireSafePath(directory);
  if (missing_allowed && !fs::exists(directory)) return {};
  std::vector<Profile> profiles;
  for (const auto& item : fs::directory_iterator(directory)) {
    RequireSafePath(item.path());
    const auto id = WideToUtf8(item.path().filename().wstring());
    // CEF stores installation data and its global Default profile alongside
    // request-context profiles. Only our opaque IDs belong to this manager.
    if (!IsProfileId(id)) continue;
    if (!item.is_directory()) Fail("资料路径不是目录，无法读取列表。请保留现场，关闭原型后检查。");
    auto name = Utf8ToWide(ReadName(item.path() / L"name.txt"));
    ValidateProfileName(name);
    profiles.push_back({id, std::move(name)});
  }
  std::sort(profiles.begin(), profiles.end(), [](const Profile& a, const Profile& b) {
    return a.id < b.id;
  });
  return profiles;
}
}  // namespace
std::vector<Profile> ProfileStore::List() const { return ListProfileDirectory(root_ / L"Profiles", false); }
std::vector<Profile> ProfileStore::ListDeleted() const { return ListProfileDirectory(root_ / L"DeletedProfiles", true); }
Profile ProfileStore::Create(const std::wstring& name) {
  return CreatePrepared(name, [](NewProfileStage&) {},
      [](const NewProfileStage& stage) { return stage.file_count() == 0; });
}
Profile ProfileStore::CreatePrepared(const std::wstring& name,
    const std::function<void(NewProfileStage&)>& prepare,
    const std::function<bool(const NewProfileStage&)>& verify) {
  // Freeze function objects before any profile/staging I/O. A preparation
  // callback may update the caller's registry, including these very objects.
  const auto owned_prepare = prepare;
  const auto owned_verify = verify;
  if (!owned_prepare || !owned_verify) Fail("资料准备或核验模块缺失，已停止创建。请保留现场并检查程序配置。");
  ValidateProfileName(name);
  const auto bytes = WideToUtf8(name);
  Profile profile{NewId(), name};
  const auto path = ProfilePath(profile.id);
  const auto staging_root = root_ / L"PendingProfiles";
  RequireSafePath(staging_root);
  fs::create_directories(staging_root);
  RequireSafePath(staging_root);
  const auto staged = staging_root / Utf8ToWide(profile.id);
  RequireSafePath(staged);
  if (!fs::create_directory(staged)) Fail("暂存资料已存在，已停止创建。请保留已有目录，不要覆盖或删除。");
  const auto meta = staged / L"name.txt";
  WriteNewMetadata(meta, bytes, "无法创建暂存资料信息。请保留暂存目录，关闭原型后检查。",
                   "无法完成暂存资料信息写入。请保留暂存目录，不要将其作为正式资料使用。");
  NewProfileStage stage(staged);
  owned_prepare(stage);
  stage.Seal();
  if (!owned_verify(stage)) Fail("资料核验未通过，未发布为正式资料。请保留暂存目录，不要重复覆盖。");
  stage.CheckIntegrity();
  // Only completely prepared data enters the CEF profile root. Never replace a
  // destination or remove incomplete staging evidence after failure.
  if (ReadName(meta) != bytes) Fail("暂存资料信息已变化，已停止发布。请保留暂存目录，关闭原型后检查。");
  RequireSafePath(staged); RequireSafePath(path);
  if (!MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
    Fail("无法发布新资料。请保留暂存目录，关闭原型后检查；不要手动覆盖正式资料。");
  return profile;
}
std::string ProfileStore::Selected() const {
  const auto path = root_ / L"selected-profile.txt";
  RequireSafePath(path);
  if (!fs::exists(path)) return {};
  const auto id = ReadName(path);
  (void)CachePath(id);
  return id;
}
void ProfileStore::Select(const std::string& id) {
  (void)CachePath(id);
  const auto target = root_ / L"selected-profile.txt";
  const auto temporary = root_ / Utf8ToWide("selected-" + NewId() + ".tmp");
  RequireSafePath(target); RequireSafePath(temporary);
  WriteNewMetadata(temporary, id, "无法准备资料选择文件。请关闭原型后检查文件占用或目录权限。",
                   "无法完成资料选择写入。请保留临时文件，关闭原型后检查。");
  if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    Fail("无法提交资料选择。请保留临时文件，关闭原型后检查；不要手动替换选择文件。");
}
bool ProfileStore::Rename(const std::string& id, const std::wstring& name) {
  ValidateProfileName(name);
  const auto path = CachePath(id);
  const auto target = path / L"name.txt";
  const auto previous = ReadName(target);
  ValidateProfileName(Utf8ToWide(previous));
  const auto bytes = WideToUtf8(name);
  if (previous == bytes) return false;
  const auto temporary = root_ / Utf8ToWide("rename-" + NewId() + ".tmp");
  WriteNewMetadata(temporary, bytes, "无法准备新名称，尚未替换原名称。请关闭原型后检查文件占用或目录权限。",
                   "无法完成新名称写入，尚未替换原名称。请保留临时文件，关闭原型后检查。");
  // Called under the application's data-root ownership. Recheck the path and
  // old bytes before same-volume replacement; do not repair damaged metadata.
  RequireSafePath(path); RequireSafePath(temporary);
  if (ReadName(target) != previous) Fail("资料名称在操作期间发生变化，已停止替换。请保留临时文件，关闭原型后检查。");
  if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    Fail("无法提交新名称。请保留临时文件，关闭原型后检查；不要手动覆盖资料信息。");
  return true;
}
fs::path ProfileStore::CachePath(const std::string& id) const {
  const auto path = ProfilePath(id);
  if (!fs::is_directory(path)) Fail("找不到资料目录。请保留现有文件，关闭原型后检查资料路径。");
  ValidateProfileName(Utf8ToWide(ReadName(path / L"name.txt")));
  return path;
}
void ProfileStore::Remove(const std::string& id, const std::string& active,
                          bool confirmed, bool browsers_closed) {
  if (!confirmed) Fail("删除资料需要明确确认，本次未删除。");
  if (!browsers_closed || id == active || id == Selected()) Fail("当前选中或仍在使用的资料不能删除。请先切换到其他资料，并在正常退出重启后再操作。");
  const auto path = ProfilePath(id);
  if (!fs::is_directory(path)) Fail("找不到资料目录。请保留现有文件，关闭原型后检查资料路径。");
  CheckProfileTree(path);
  // Logical deletion is an atomic directory move. Keep data recoverable and
  // avoid partial recursive deletion if a file remains locked.
  const auto recycle = root_ / L"DeletedProfiles";
  RequireSafePath(recycle);
  fs::create_directories(recycle);
  const auto target = recycle / Utf8ToWide(id);
  RequireSafePath(target);
  if (fs::exists(target)) Fail("回收区已有同标识资料，已停止移动。请保留两处数据，不要覆盖回收区。");
  fs::rename(path, target);
}
Profile ProfileStore::RestoreDeleted(const std::string& id, const std::string& active,
                                     bool confirmed, bool browsers_closed) {
  if (!confirmed) Fail("恢复资料需要明确确认，回收区保持原样。");
  if (!browsers_closed || id == active || id == Selected())
    Fail("仍在使用或当前选中的资料不能恢复，请正常关闭后重试。");
  const auto target = ProfilePath(id); // Opaque ID and root containment validation.
  const auto source = root_ / L"DeletedProfiles" / Utf8ToWide(id);
  RequireSafePath(source);
  if (!fs::is_directory(source)) Fail("回收区找不到该资料，请保留现场并重新打开列表。");
  if (fs::exists(target)) Fail("正式资料中已有相同标识，恢复已停止，两处数据均保留。");
  const auto bytes = ReadName(source / L"name.txt");
  Profile recovered{id, Utf8ToWide(bytes)}; ValidateProfileName(recovered.name);
  CheckProfileTree(source);
  RequireSafePath(source); RequireSafePath(target);
  if (ReadName(source / L"name.txt") != bytes) Fail("回收区资料名称已变化，恢复停止，原目录保留。");
  // Same-volume atomic move without replacement; no recursive copy, repair or
  // deletion. Locked/conflicting destinations preserve the recycled directory.
  if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
    Fail("无法恢复资料，回收区及正式资料保持原样。请正常关闭后检查占用或权限。");
  return recovered;
}
}  // namespace gbrowser
