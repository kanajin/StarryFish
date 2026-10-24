#include <windows.h>
#include <roapi.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include "core.h"
#include "application_layout.h"
#include "proxy_initialization.h"
#include "proxy_preset.h"
#include "directory_pin.h"
#include "component_catalogue.h"
#include "components.h"
#include "file_digest.h"
#include "proxy_scripts.h"
#include "timing_log.h"
#include "launch_plan.h"
#include "tab_slots.h"
#include "ui_interaction.h"
#include "user_state.h"
#include "profile_catalogue.h"
#include "profile_dialog.h"
#include "profile_transfer.h"
#include "proxy_preference.h"
#include "proxy_profiles.h"
#include "bookmark_navigation.h"
#include "convenience_layout.h"
#include "dual_transition.h"
#include "callback_gate.h"
#include "game_copy.h"
#include "browser_history.h"
#include "login_resume.h"
#include "ui_theme.h"
#include "compact_chrome.h"
#include "host_ui_flow.h"
#include "application_gate.h"
#include "target_reply.h"
#include "zero_initialization.h"
#include "resource_cache.h"
#include "network_capture.h"
#include "skill_overlay.h"
#include "character_shortcuts.h"
#include "shortcut_dialog.h"
#include <shlwapi.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <functional>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using namespace gbrowser;
namespace {
constexpr int kProxy = 101, kTarou = 102, kArcarum = 103, kHome = 104;
constexpr int kProxySettings = 109;
constexpr int kCopy = 110;
constexpr int kNewTab = 111, kExtensions = 113, kMenu = 114;
constexpr int kDownloads = 115, kProfiles = 116, kReturnProfile = 117, kExit = 118;
constexpr int kDual = 119, kLink = 120, kMoba = 121, kDetach = 122, kTarouClose = 123, kTarouDetach = 124;
constexpr int kLightTheme = 125, kDarkTheme = 126;
constexpr int kMinimize = 127, kMaximize = 128, kSecondaryMenu = 129;
constexpr int kLayout1 = 130, kLayout2 = 131, kReload = 134;
constexpr int kCacheToggle = 135, kCacheInfo = 136, kCacheClear = 137;
constexpr int kHideLoading = 138;
constexpr int kCaptureStop = 139, kCaptureInfo = 140, kCaptureClear = 141;
constexpr int kSkillCooldown = 142, kCharacterKeys = 143;
constexpr UINT_PTR kCharacterTimer = 12;
constexpr UINT kCacheResult = WM_APP + 10;
constexpr UINT kProxyMenuReady = WM_APP + 11;
constexpr UINT_PTR kShutdownTimer = 11;
constexpr int kProxyProfileFirst = 3000;
constexpr UINT kShowNotice = WM_APP + 8;
constexpr UINT kMeasureSecondaryMenu = WM_APP + 9;
constexpr size_t kLeftGame = 4, kArcarumView = 5;
constexpr UINT kCloseLogin = WM_APP + 1;
constexpr UINT kPageShortcut = WM_APP + 2;
constexpr UINT kFocusLogin = WM_APP + 3;
constexpr UINT kCloseOrdinary = WM_APP + 4;
constexpr UINT kRestoreProxy = WM_APP + 5;
constexpr UINT kHideTarou = WM_APP + 6, kHideArcarum = WM_APP + 7;
void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("WebView2接口操作失败"); }
template<typename Getter> std::wstring ComText(Getter getter) {
  LPWSTR value = nullptr;
  const auto result = getter(&value);
  std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> text(value, CoTaskMemFree);
  Check(result);
  return value ? value : L"";
}
template<typename T> gbrowser::webview::cache::Headers CacheHeaders(T* headers) {
  gbrowser::webview::cache::Headers result;
  ComPtr<ICoreWebView2HttpHeadersCollectionIterator> iterator; Check(headers->GetIterator(&iterator));
  BOOL present = FALSE; Check(iterator->get_HasCurrentHeader(&present)); size_t bytes = 0;
  while (present) {
    LPWSTR name = nullptr, value = nullptr;
    const auto hr = iterator->GetCurrentHeader(&name, &value);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> n(name, CoTaskMemFree), v(value, CoTaskMemFree);
    Check(hr);
    const auto a = WideToUtf8(name ? name : L""), b = WideToUtf8(value ? value : L"");
    bytes += a.size() + b.size(); if (bytes > 32768 || result.size() >= 128) throw std::runtime_error("cache headers too large");
    result.emplace_back(a, b); Check(iterator->MoveNext(&present));
  }
  return result;
}
gbrowser::webview::cache::Request CacheRequest(ICoreWebView2WebResourceRequest* request) {
  ComPtr<ICoreWebView2HttpRequestHeaders> headers; Check(request->get_Headers(&headers));
  return {WideToUtf8(ComText([&](LPWSTR* p) { return request->get_Uri(p); })),
      WideToUtf8(ComText([&](LPWSTR* p) { return request->get_Method(p); })), CacheHeaders(headers.Get())};
}
struct CachePending {
  ComPtr<ICoreWebView2WebResourceRequestedEventArgs> args;
  ComPtr<ICoreWebView2Deferral> deferral;
  size_t view = 0;
  uint64_t generation = 0, navigation = 0, epoch = 0, network = 0, started = 0;
  std::string url;
};
struct View {
  ComPtr<ICoreWebView2Controller> controller; ComPtr<ICoreWebView2> core;
  std::optional<RECT> bounds;
  std::optional<bool> visible;
  std::optional<POINT> parent_position;
  // All host writes pass here; Configure resets caches for each new controller.
  // Failed setters never publish a cached success.
  void Bounds(RECT next) {
    if (!bounds || !EqualRect(&*bounds, &next)) { Check(controller->put_Bounds(next)); bounds = next; }
  }
  void Visible(bool next) {
    if (!visible || *visible != next) { Check(controller->put_IsVisible(next)); visible = next; }
  }
  void Reparent(HWND parent) {
    Check(controller->put_ParentWindow(parent)); bounds.reset(); parent_position.reset();
  }
  void NotifyPosition() {
    if (!controller || !visible.value_or(false)) return;
    HWND parent = nullptr; Check(controller->get_ParentWindow(&parent));
    POINT point{};
    if (ClientToScreen(parent, &point) && (!parent_position ||
        parent_position->x != point.x || parent_position->y != point.y)) {
      Check(controller->NotifyParentWindowPositionChanged()); parent_position = point;
    }
  }
};
// Both official popup paths must retain the deferral until binding finishes.
// A rejected/cancelled request still completes exactly once without navigating.
struct WindowRequest {
  ComPtr<ICoreWebView2NewWindowRequestedEventArgs> args;
  ComPtr<ICoreWebView2Deferral> deferral;
  explicit WindowRequest(ICoreWebView2NewWindowRequestedEventArgs* value) : args(value) {
    Check(args->GetDeferral(&deferral));
  }
  ~WindowRequest() { if (deferral) deferral->Complete(); }
  void Complete() { auto held = deferral; deferral.Reset(); Check(held->Complete()); }
};
struct BookmarkRequest {
  std::shared_ptr<WindowRequest> window;
  gbrowser::webview::TabSlots::Ticket origin;
  uint64_t navigation_epoch;
  std::string source;
};

// One STA owns the fixed game/helper/proxy/login roles and seven optional
// shared-profile tabs. Optional views are created only on explicit user action.
struct PendingProfileImport {
  std::string id, previous;
  bool restored = false;
  gbrowser::webview::transfer::JsonObject document;
};
std::shared_ptr<PendingProfileImport> pending_profile_import;

class Host {
 public:
  explicit Host(const gbrowser::webview::LaunchPlan& plan, const wchar_t*& phase, const std::string& requested = {})
      : mode_(plan.network_mode), phase_(phase) {
    phase_ = L"核对候选程序和环境";
    const auto started = GetTickCount64();
    std::array<wchar_t, MAX_PATH> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("候选程序路径异常");
    // Use the spelling verified from the filesystem, not the launcher's casing.
    // This does not accept aliases/redirects or change existing registrations.
    root_ = directories_.Tree(std::filesystem::path(executable.data()).parent_path());
    payload_ = root_;
    auto local = ComText([](LPWSTR* value) { return SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, value); });
    auto identity = root_.native(); CharLowerBuffW(identity.data(), static_cast<DWORD>(identity.size()));
    FileDigest hash; hash.Add(identity.data(), identity.size() * sizeof(wchar_t));
    installation_state_ = std::filesystem::path(local) / L"StarryFish" / L"Installations" / Utf8ToWide(hash.Finish().substr(0, 16));
    for (const wchar_t* name : {L"WEBVIEW2_USER_DATA_FOLDER", L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
         L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"})
      if (GetEnvironmentVariableW(name, nullptr, 0)) throw std::runtime_error("拒绝WebView2环境覆盖");
    // Lock the installation before reading any profile index or creating its
    // UDF. Locks/pins survive all runtime callbacks and process shutdown.
    application_lock_ = std::make_unique<DataRootLock>(root_);
    RequireSafePath(installation_state_); directories_.CreateTree(installation_state_);
    phase_ = L"读取本机资料索引";
    profiles_ = std::make_unique<gbrowser::webview::ProfileCatalogue>(installation_state_, plan.data_leaf);
    profile_leaf_ = plan.data_leaf;
    active_profile_ = requested.empty() ? profiles_->state().selected : requested;
    profiles_->Find(active_profile_);
    data_ = gbrowser::webview::ProfileDataPath(installation_state_, plan.data_leaf, active_profile_);
    phase_ = L"核对资料目录身份（路径重定向会被拒绝）";
    RequireSafePath(data_); directories_.CreateTree(data_);
    data_lock_ = std::make_unique<DataRootLock>(data_); timings_.Open(data_, started);
    settings_root_ = installation_state_;
    if (std::filesystem::exists(data_ / L"HostSettings")) settings_root_ = directories_.Tree(data_ / L"HostSettings");
    proxy_preference_ = std::make_unique<gbrowser::webview::ProxyPreference>(data_);
    { gbrowser::webview::UserStateFile overlay(settings_root_ / L"skill-overlay.state");
      skill_enabled_ = overlay.bytes() && *overlay.bytes() == "ON\n"; }
    phase_ = L"读取全局窗口布局";
    try {
      shortcut_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"shortcut-keys.state");
      if (shortcut_file_->bytes()) shortcut_keys_ = gbrowser::webview::characters::Decode(*shortcut_file_->bytes());
    } catch (...) { shortcut_file_.reset(); }
    window_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"window-layout.state");
    if (window_file_->bytes()) {
      window_state_ = gbrowser::webview::DecodeWindowState(*window_file_->bytes());
      panel_percent_ = window_state_.panel_percent;
    }
    pane_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"game-panes.state");
    if (pane_file_->bytes()) panes_ = gbrowser::webview::DecodePaneState(*pane_file_->bytes(), panel_percent_);
    else { panes_.group_percent = panel_percent_;
      panes_.tarou_share = (std::clamp)(panel_percent_ * 100 / (panel_percent_ + 100), 1U, 99U) * 100; }
    panel_percent_ = panes_.group_percent;
    tarou_window_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"tarou-window.state");
    if (tarou_window_file_->bytes()) tarou_window_state_ = gbrowser::webview::DecodeWindowState(*tarou_window_file_->bytes());
    phase_ = L"核对程序更新是否完成";
    gbrowser::webview::RequireCompleteApplicationUpdate(root_);
    catalogue_ = std::make_unique<gbrowser::webview::ComponentCatalogue>(root_);
    registrations_ = std::make_unique<gbrowser::webview::ComponentRegistrations>(root_, data_);
    for (const auto& component : catalogue_->entries()) {
      auto source = payload_ / L"Components" / component.name;
      directories_.Tree(source);
      for (const auto& entry : std::filesystem::recursive_directory_iterator(source))
        if (entry.is_directory()) directories_.One(entry.path());
      pages_.push_back(PrepareExtensionPage(source, component.tree, component.files, component.bytes,
          WideToUtf8(component.entry), "webview2-candidate", data_));
      sources_.push_back(source);
    }
    // This fourth component is immutable product code, separate from the two
    // install-time/helper-update catalogue entries. It never loads remote JS.
    const auto& scripts = kScriptsComponent;
    scripts_component_ = {scripts.name, scripts.entry, scripts.tree, scripts.files, scripts.bytes};
    auto source = root_ / scripts.name;
    directories_.Tree(source);
    pages_.push_back(PrepareExtensionPage(source, scripts.tree, scripts.files, scripts.bytes,
        WideToUtf8(scripts.entry), "webview2-candidate", data_));
    sources_.push_back(source);
    timings_.Mark("component-source-pin-ready");
    theme_.LoadIcon(0, sources_[0] / L"img/icons/omega-48.png");
    theme_.LoadIcon(1, sources_[1] / L"assets/icon-48.png");
    theme_.LoadIcon(2, sources_[2] / L"icons/icon-48.png");
    appearance_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"appearance.state");
    if (appearance_file_->bytes()) theme_.SetDark(gbrowser::webview::DecodeDarkAppearance(*appearance_file_->bytes()));
    layout_choice_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"layout-choice.state");
    if (layout_choice_file_->bytes()) browser_layout_ = gbrowser::webview::DecodeBrowserLayout(*layout_choice_file_->bytes());
    try {
      loading_display_file_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"loading-display.state");
      if (loading_display_file_->bytes()) hide_loading_ = gbrowser::webview::DecodeHiddenLoading(*loading_display_file_->bytes());
    } catch (...) { loading_display_file_.reset(); } // Optional cosmetic state never stops a game session.
    try {
      cache_setting_ = std::make_unique<gbrowser::webview::UserStateFile>(settings_root_ / L"static-cache.state");
      if (cache_setting_->bytes()) cache_enabled_ = *cache_setting_->bytes() == "STARRYFISH-CACHE-ON\n";
    } catch (...) { cache_enabled_ = false; }
  }
  ~Host() {
    callbacks_.Cancel(); lifecycle_callbacks_.Cancel();
    // Synchronous startup can fail before the message pump owns shutdown.
    // Startup failure has no game stream jobs. Stop its optional cache scan
    // before destroying the notification HWND; normal close already drained it.
    resource_cache_.reset();
    // Detach this stack object before destroying its native window.
    if (shortcut_dialog_) shortcut_dialog_->Close();
    if (profile_dialog_) profile_dialog_->Close();
    if (arcarum_window_) { SetWindowLongPtrW(arcarum_window_, GWLP_USERDATA, 0); DestroyWindow(arcarum_window_); }
    if (tarou_window_) { SetWindowLongPtrW(tarou_window_, GWLP_USERDATA, 0); DestroyWindow(tarou_window_); }
    if (window_) { SetWindowLongPtrW(window_, GWLP_USERDATA, 0); DestroyWindow(window_); }
    if (browser_process_) CloseHandle(browser_process_);
    if (font_) DeleteObject(font_);
  }
  bool RestartIntoGame() const { return restart_gbf_ && !failed_; }
  std::string NextProfile() const { return (!failed_ || return_profile_) && ShutdownComplete() ? next_profile_ : std::string{}; }
  bool ShutdownComplete() const { return closing_ && pending_ == 0 && !window_ && !environment_; }
  void MessageLoopFailed() {
    restart_gbf_ = false;
    timings_.Mark("message-loop-failed");
    StopNetworking(L"窗口消息异常，正在等待候选正常退出；资料保持保护");
    Close();
  }
  bool PreTranslate(const MSG& message) {
    if (transfer_busy_) return message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
    if (shortcut_dialog_) { auto copy = message; if (shortcut_dialog_->Translate(copy)) return true; }
    if (profile_dialog_) { auto copy = message; if (profile_dialog_->Translate(copy)) return true; }
    // Native controls need the same commands as page accelerators. Login
    // keystrokes, including KeePassXC input, never enter this command path.
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) return false;
    const bool native = message.hwnd == window_ || message.hwnd == address_ || message.hwnd == tabs_ ||
        std::find(buttons_.begin(), buttons_.end(), message.hwnd) != buttons_.end();
    if (!native) return false;
    if (message.wParam == VK_ESCAPE && (GetCapture() == tabs_ || GetCapture() == divider_)) {
      tab_close_.Cancel(); CancelSplit();
      if (GetCapture() == tabs_) ReleaseCapture();
      return true;
    }
    if (message.hwnd == address_ && message.wParam == VK_RETURN) {
      Guard([&] { SubmitAddress(); }); return true;
    }
    if (message.hwnd == address_ && message.wParam == VK_ESCAPE) {
      Guard([&] { drafts_[active_] = false; UpdateAddress(); FocusActive(); }); return true;
    }
    const auto action = PageKey(static_cast<UINT>(message.wParam));
    if (action == Shortcut::None) return false;
    Guard([&] { PageAction(action); }); return true;
  }

  void Create(HINSTANCE instance, int show) {
    phase_ = L"检查稳定版 WebView2 运行库（154 或更新）";
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    Check(options->put_AreBrowserExtensionsEnabled(TRUE));
    // Never use an installed preview browser as a runtime fallback.
    Check(options->put_ReleaseChannels(COREWEBVIEW2_RELEASE_CHANNELS_STABLE));
    const auto version = ComText([&](LPWSTR* value) {
      return GetAvailableCoreWebView2BrowserVersionStringWithOptions(nullptr, options.Get(), value);
    });
    if (!gbrowser::webview::SupportedRuntimeVersion(version))
      throw std::runtime_error("运行库版本低于候选基线或不是稳定运行库");
    phase_ = L"创建游戏窗口";
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES};
    if (!InitCommonControlsEx(&controls)) throw std::runtime_error("无法创建标签控件");
    WNDCLASSW cls{};
    cls.hInstance = instance; cls.lpszClassName = L"GbrowserWebView2Candidate";
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    cls.lpfnWndProc = Procedure; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    cls.lpszClassName = L"GbrowserMobageLogin";
    RegisterClassW(&cls);
    cls.lpszClassName = L"GbrowserHelperWindow";
    RegisterClassW(&cls);
    cls.lpszClassName = L"GbrowserArcarumPane";
    RegisterClassW(&cls);
    cls.lpszClassName = L"GbrowserTarouPane";
    RegisterClassW(&cls);
    cls.lpszClassName = L"GbrowserWebView2Candidate";
    window_ = CreateWindowExW(0, cls.lpszClassName,
        mode_ == RunMode::Offline ? L"StarryFish · 离线候选" : L"StarryFish · 游戏候选", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error("无法创建候选窗口");
    if (shortcut_keys_.master) {
      RAWINPUTDEVICE keyboard{0x01,0x06,0u,window_};
      if (!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard))) throw std::runtime_error("无法恢复键盘快捷键输入");
      character_enabled_=true;
      for(size_t key=0;key<character_tracker_.down.size();++key)
        character_tracker_.down[key]=(GetAsyncKeyState(static_cast<int>(key))&0x8000)!=0;
    }
    address_ = CreateWindowExW(0, L"EDIT", L"GBF My Page", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, window_, nullptr, instance, nullptr);
    tabs_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY,
        0, 0, 10, 10, window_, nullptr, instance, nullptr);

    divider_ = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_NOTIFY | SS_GRAYRECT,
        0, 0, 10, 10, window_, nullptr, instance, nullptr);
    if (!divider_ || !SetWindowSubclass(divider_, DividerProcedure, 2, reinterpret_cast<DWORD_PTR>(this)))
      throw std::runtime_error("无法创建分屏拖动条");
    helper_divider_ = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_NOTIFY | SS_GRAYRECT,
        0, 0, 10, 10, window_, nullptr, instance, nullptr);
    if (!helper_divider_ || !SetWindowSubclass(helper_divider_, DividerProcedure, 4, reinterpret_cast<DWORD_PTR>(this)))
      throw std::runtime_error("无法创建助手分屏拖动条");

    buttons_[0] = Button(instance, kProfiles, L"资料");
    buttons_[1] = Button(instance, kProxy, L"代理");
    buttons_[2] = Button(instance, kTarou, L"太郎");
    buttons_[3] = Button(instance, kArcarum, L"肉鸽");
    buttons_[4] = Button(instance, kDual, L"双窗");
    buttons_[5] = Button(instance, kCopy, L"复制");
    buttons_[6] = Button(instance, kExtensions, L"管理");
    buttons_[7] = Button(instance, kMenu, L"更多");
    buttons_[8] = Button(instance, kLink, L"主窗口网址发送到副窗口");
    buttons_[9] = Button(instance, kDetach, L"拆出");
    buttons_[10] = Button(instance, kTarouClose, L"关闭太郎");
    buttons_[11] = Button(instance, kTarouDetach, L"拆出/停靠太郎");
    buttons_[12] = Button(instance, kMinimize, L"最小化");
    buttons_[13] = Button(instance, kMaximize, L"最大化 / 还原");
    buttons_[14] = Button(instance, kExit, L"关闭浏览器");
    buttons_[15] = Button(instance, kReload, L"刷新");
    buttons_[16] = Button(instance, kNewTab, L"新建标签");
    buttons_[17] = Button(instance, kCharacterKeys, L"键盘快捷键");
    tarou_title_ = CreateWindowW(L"STATIC", L"Tarou", WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW,
        0, 0, 10, 10, window_, nullptr, instance, nullptr);
    const auto frame = [&] {
      return CreateWindowW(L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
          0, 0, 10, 10, window_, nullptr, instance, nullptr);
    };
    right_frame_ = frame(); left_frame_ = frame(); arcarum_frame_ = frame(); tarou_frame_ = frame();
    for (HWND item : {right_frame_, left_frame_, arcarum_frame_, tarou_frame_})
      if (!item || !SetWindowSubclass(item, FrameProcedure, 3, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("无法创建游戏及助手裁剪容器");
    if (!address_ || !tabs_) throw std::runtime_error("无法创建游戏工具栏");
    if (!SetWindowSubclass(tabs_, TabProcedure, 1, reinterpret_cast<DWORD_PTR>(this)))
      throw std::runtime_error("无法接入标签中键手势");
    SendMessageW(address_, EM_SETLIMITTEXT, 8192, 0);
    titles_[0] = L"GBF";
    RebuildTabs();
    for (HWND button : buttons_) if (!button) throw std::runtime_error("无法创建游戏按钮");
    const auto tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
    if (tips) {
      const std::array<const wchar_t*, 18> names{L"用户资料", L"ZeroOmega 代理", L"打开/关闭太郎", L"打开/关闭肉鸽",
          L"单窗/双窗", L"复制当前网址", L"组件管理", L"更多 · 外观与浏览器功能", L"副窗口跟随主窗口一次；同网址则刷新",
          L"拆出肉鸽", L"关闭太郎", L"拆出/停靠太郎", L"最小化", L"最大化 / 还原", L"关闭浏览器",
          L"刷新", L"新建标签 · Ctrl+T", L"键盘快捷键设置"};
      for (size_t i = 0; i < buttons_.size(); ++i) {
        TOOLINFOW tool{sizeof(tool)}; tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(buttons_[i]); tool.lpszText = const_cast<wchar_t*>(names[i]);
        SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
      }
    }
    tabs_tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
    if (tabs_tip_) {
      TOOLINFOW tool{sizeof(tool)}; tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.hwnd = window_;
      tool.uId = reinterpret_cast<UINT_PTR>(tabs_); tool.lpszText = LPSTR_TEXTCALLBACKW;
      SendMessageW(tabs_tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    theme_.Window(window_); RefreshProfileTitle(); UpdateFont();
    try { capture_server_ = std::make_unique<gbrowser::webview::capture::Server>(capture_); }
    catch (...) { Notice(L"本机网络抓取接口未能启动；可继续浏览"); }
    if (!SetTimer(window_, 9, 100, nullptr)) throw std::runtime_error("无法观察网络抓取状态");
    Layout(); RestoreLayout(show);
    try {
      if (!SetTimer(window_, 6, 100, nullptr)) throw std::runtime_error("cache timer unavailable");
      resource_cache_ = std::make_unique<gbrowser::webview::cache::Worker>(root_ / L"Cache" / L"Static-v1",
          [window = window_] { PostMessageW(window, kCacheResult, 0, 0); });
      cache_initializing_ = true; ++pending_;
    } catch (...) { KillTimer(window_, 6); resource_cache_.reset(); }
    phase_ = L"创建运行环境";
    ++pending_;
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(nullptr, data_.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
              --pending_;
              Guard([&] {
                Check(error);
                if (!environment) throw std::runtime_error("缺少候选运行环境");
                environment_ = environment;
                ComPtr<ICoreWebView2Environment7> data_environment; Check(environment_.As(&data_environment));
                const auto actual = ComText([&](LPWSTR* value) { return data_environment->get_UserDataFolder(value); });
                auto actual_path = std::filesystem::path(actual).lexically_normal(); actual_path.make_preferred();
                if (_wcsicmp(actual_path.c_str(), data_.c_str()) != 0) throw std::runtime_error("运行资料根不匹配");
                timings_.Mark("runtime-environment-ready");
                phase_ = L"创建游戏及助手页面";
                ComPtr<ICoreWebView2Environment5> lifecycle; Check(environment_.As(&lifecycle));
                EventRegistrationToken token{};
                Check(lifecycle->add_BrowserProcessExited(Callback<ICoreWebView2BrowserProcessExitedEventHandler>(
                    [this, lifetime = lifecycle_callbacks_.Track()](ICoreWebView2Environment*, ICoreWebView2BrowserProcessExitedEventArgs* args) -> HRESULT {
                      if (!lifetime.Active()) return S_OK;
                      Guard([&] {
                        UINT32 pid = 0; Check(args->get_BrowserProcessId(&pid));
                        if (!browser_pid_ || pid == browser_pid_) {
                          browser_exited_ = true;
                          timings_.Mark("runtime-exit-confirmed");
                          if (!closing_) StopNetworking(L"运行库已退出；请正常关闭候选后重新启动");
                        }
                      });
                      Drain(); return S_OK;
                    }).Get(), &token));
                if (!closing_) CreateView(0);
              });
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { --pending_; Check(result); }
  }

 private:
  HWND Button(HINSTANCE instance, int id, const wchar_t* text) {
    const auto button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    gbrowser::webview::UiTheme::AttachButton(button); return button;
  }
  // Defer modal UI until the WebView callback has returned. Coalesce notices
  // during an open/pending dialog; progress and success never use this path.
  void Notice(const wchar_t* text) noexcept {
    if (!window_ || closing_ || notice_pending_ || notice_open_) return;
    notice_pending_ = text; // Only static host messages, never page/credential data.
    if (!PostMessageW(window_, kShowNotice, 0, 0)) notice_pending_ = nullptr;
  }
  void StopNetworking(const wchar_t* text) noexcept {
    if (!failed_) timings_.Mark("operation-stopped");
    failed_ = true; proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_;
    if (proxy_) proxy_->Invalidate(3);
    if (window_) { KillTimer(window_, 2); KillTimer(window_, 3); }
    dual_transition_.Cancel();
    Notice(text);
  }
  void Guard(const std::function<void()>& action) noexcept {
    try { action(); }
    catch (...) {
      StopNetworking(L"操作未完成；游戏网络接续已停止，请关闭候选后重新启动");
      // Fixed phase labels, never exception strings with URLs or secrets.
      SetWindowTextW(address_, phase_);
    }
  }
  void CreateView(size_t index) {
    ++pending_;
    const auto result = environment_->CreateCoreWebView2Controller(index == 0 ? right_frame_ : index == 1 ? tarou_frame_ : window_,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, index](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
              --pending_;
              Guard([&] {
                Check(error);
                if (!controller) throw std::runtime_error("缺少候选页面控制器");
                views_[index].controller = controller;
                browser_started_ = true;
                Check(controller->get_CoreWebView2(&views_[index].core));
                if (!browser_pid_) {
                  UINT32 pid = 0; Check(views_[index].core->get_BrowserProcessId(&pid));
                  if (!pid) throw std::runtime_error("运行库进程身份缺失");
                  browser_pid_ = pid;
                  browser_process_ = OpenProcess(SYNCHRONIZE, FALSE, pid);
                  // BrowserProcessExited remains the supported completion
                  // authority if a process wait handle cannot be opened.
                }
                if (closing_) return;
                Configure(index); Layout();
                constexpr const char* phases[]{"game-view-ready", "helper-view-ready", "proxy-view-ready"};
                timings_.Mark(phases[index]);
                if (index < 2) CreateView(index + 1); else GetInstalled();
              });
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { --pending_; Check(result); }
  }
  bool ExtensionAsset(const std::string& url) const {
    for (const auto& page : pages_) if (page->AllowsAsset(url)) return true;
    return false;
  }
  bool ViewExtensionAsset(size_t index, const std::string& url) const {
    if (!IsAuxiliary(index)) return ExtensionAsset(url);
    const auto& page = auxiliary_pages_[index - gbrowser::webview::TabSlots::kAuxFirst];
    return page && page->AllowsAsset(url);
  }
  bool BrowsingAvailable() const {
    return gbrowser::webview::BrowserNetworkAvailable(mode_, closing_, failed_);
  }
  bool Document(const std::string& url) const {
    return url == "about:blank" || ExtensionAsset(url) ||
        (BrowsingAvailable() && IsBrowserNetworkUrl(url));
  }
  void Configure(size_t index, bool defer_resources = false) {
    views_[index].bounds.reset(); views_[index].visible.reset(); views_[index].parent_position.reset();
    moba_css_[index] = 0; menu_css_[index] = 0;
    auto core = views_[index].core;
    const auto generation = view_generations_[index];
    EventRegistrationToken token{};
    if (gbrowser::webview::GameSurface(index)) {
      Check(views_[index].controller->add_GotFocus(Callback<ICoreWebView2FocusChangedEventHandler>(
          [this, index, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
            if (!closing_ && CurrentView(index, generation)) character_focused_view_ = index;
            return S_OK;
          }).Get(), &token));
      Check(views_[index].controller->add_LostFocus(Callback<ICoreWebView2FocusChangedEventHandler>(
          [this, index, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
            if (!closing_ && CurrentView(index, generation) && character_focused_view_ == index) {
              character_focused_view_ = views_.size(); character_tracker_ = {}; CancelCharacterSwitch();
            }
            return S_OK;
          }).Get(), &token));
    }
    if (gbrowser::webview::GameSurface(index)) Check(views_[index].controller->add_ZoomFactorChanged(
        Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
            [this, index, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
              Guard([&] { if (!closing_ && CurrentView(index, generation)) LayoutGameView(index); }); return S_OK;
            }).Get(), &token));
    if (index != 2 && index != 3 && !IsAuxiliary(index))
      Check(views_[index].controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [this, index, generation](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
              Guard([&] {
                if (!CurrentView(index, generation) || closing_ ||
                    (index != active_ && index != 1 && index != kLeftGame && index != kArcarumView)) return;
                COREWEBVIEW2_KEY_EVENT_KIND kind{}; Check(args->get_KeyEventKind(&kind));
                const bool released=kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP || kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_UP;
                UINT key=0;Check(args->get_VirtualKey(&key));
                const bool modified=(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||
                    (GetKeyState(VK_SHIFT)&0x8000)||(GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000);
                if(character_enabled_ && gbrowser::webview::GameSurface(index) &&
                    gbrowser::webview::characters::Actions(shortcut_keys_,key) && !gbrowser::webview::characters::Modifier(key)) {
                  // Raw input owns all gesture edges, including Escape/F-keys.
                  // The accelerator only suppresses the browser's default action;
                  // forwarding here can lose releases or duplicate a raw gesture.
                  if(!modified){Check(args->put_Handled(TRUE));return;}
                }
                if(released)return;
                const auto action = PageKey(key);
                if (action == Shortcut::None) return;
                Check(args->put_Handled(TRUE));
                COREWEBVIEW2_PHYSICAL_KEY_STATUS physical{}; Check(args->get_PhysicalKeyStatus(&physical));
                if (physical.WasKeyDown) return;
                // Dispatch after returning from the synchronous accelerator
                // callback; do not invoke WebView methods in its nested loop.
                const auto origin = (generation << 8) | index;
                if (!PostMessageW(window_, kPageShortcut, static_cast<WPARAM>(action), static_cast<LPARAM>(origin)))
                  throw std::runtime_error("无法接续页面快捷键");
              }); return S_OK;
            }).Get(), &token));
    Check(core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
          args->put_Cancel(TRUE);
          Guard([&] {
            if (!CurrentView(index, generation)) return;
            if (character_busy_ && character_view_ == index) CancelCharacterSwitch();
            ++navigation_epochs_[index]; // Deferred bookmark opens cannot outlive their document.
            if (index == 2) {
              controller_ready_ = false;
              if (proxy_) proxy_->Invalidate(3);
              proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_;
              KillTimer(window_, 2); KillTimer(window_, 3);
              ++target_generation_;
            }
            const auto uri = ComText([&](LPWSTR* text) { return args->get_Uri(text); });
            if (index >= gbrowser::webview::TabSlots::kFirst && IsGbfPageUrl(WideToUtf8(uri)) && !bound_game_id_) {
              Notice(L"请先等待主游戏页与助手绑定，再在普通标签打开其他GBF页面"); return;
            }
            const auto url = WideToUtf8(uri);
            const bool allowed = !closing_ && (IsAuxiliary(index) ?
                BrowsingAvailable() && auxiliary_pages_[index - gbrowser::webview::TabSlots::kAuxFirst] &&
                auxiliary_pages_[index - gbrowser::webview::TabSlots::kAuxFirst]->AllowsPage(url) : Document(url));
            if (index == 3) timings_.Mark(allowed ? "login-navigation-started" : "login-navigation-rejected");
            Check(args->put_Cancel(!allowed));
            if (gbrowser::webview::GameSurface(index) && allowed) {
              capture_game_[index] = IsGbfPageUrl(url);
              skill_routes_[index] = gbrowser::webview::cooldown::BattleRoute(url);
              if (capture_observers_[index]) { capture_observers_[index]->ResetCooldown(); capture_observers_[index]->Tick(); }
            }

          }); return S_OK;
        }).Get(), &token));
    Check(core->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
          args->put_Cancel(TRUE);
          Guard([&] {
            if (!CurrentView(index, generation)) return;
            const auto uri = ComText([&](LPWSTR* text) { return args->get_Uri(text); });
            const auto url = WideToUtf8(uri);
            Check(args->put_Cancel(closing_ || (IsAuxiliary(index) ?
                !BrowsingAvailable() || (url != "about:blank" && !ViewExtensionAsset(index, url)) : !Document(url))));
          }); return S_OK;
        }).Get(), &token));
    Check(core->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [this, index, generation](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
          const auto handled = args->put_Handled(TRUE);
          if (FAILED(handled)) return handled;
          Guard([&] {
            if (!CurrentView(index, generation)) return;
            if (index == 1 || index == kArcarumView) OpenAuxiliary(sender, args);
            else if (!OpenBookmark(index, sender, args)) OpenLogin(index, sender, args);
          });
          return S_OK;
        }).Get(), &token));
    Check(core->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
        [this, index, generation](ICoreWebView2*, IUnknown*) -> HRESULT {
          if (!CurrentView(index, generation)) return S_OK;
          if (index == 3 && login_window_) PostMessageW(window_, kCloseLogin, 0, static_cast<LPARAM>(generation));
          if (index == 1) PostMessageW(window_, kHideTarou, 0, 0);
          if (index == kArcarumView) PostMessageW(window_, kHideArcarum, 0, 0);
          if (index >= gbrowser::webview::TabSlots::kFirst)
            PostMessageW(window_, kCloseOrdinary, static_cast<WPARAM>(index), static_cast<LPARAM>(generation));
          return S_OK;
        }).Get(), &token));
    Check(core->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
          return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        }).Get(), &token));
    // User-requested webpage/extension exports use the runtime's normal save
    // and download UI. Do not replace it with a blanket cancellation, hidden
    // path override, custom file writer or automatic file execution.
    ComPtr<ICoreWebView2_9> downloads; Check(core.As(&downloads));
    // The game and assistant can each be narrower than the default dialog.
    // Anchor left before the first layout so its file labels stay visible.
    Check(downloads->put_DefaultDownloadDialogCornerAlignment(
        COREWEBVIEW2_DEFAULT_DOWNLOAD_DIALOG_CORNER_ALIGNMENT_TOP_LEFT));
    Check(downloads->put_DefaultDownloadDialogMargin({8, 8}));
    ComPtr<ICoreWebView2_18> external; Check(core.As(&external));
    Check(external->add_LaunchingExternalUriScheme(Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
        [](ICoreWebView2*, ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) -> HRESULT { return args->put_Cancel(TRUE); }
        ).Get(), &token));
    // Popup web contents are replaced by put_NewWindow. Its resource handler
    // must be attached after that call, while the request is still deferred.
    if (!defer_resources && index != 3 && !IsAuxiliary(index)) ConfigureResources(index);
    if (index == 3) Check(core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
          Guard([&] {
            if (!CurrentView(index, generation)) return;
            BOOL success = FALSE; Check(args->get_IsSuccess(&success));
            COREWEBVIEW2_WEB_ERROR_STATUS error{}; Check(args->get_WebErrorStatus(&error));
            ComPtr<ICoreWebView2NavigationCompletedEventArgs2> details; Check(args->QueryInterface(IID_PPV_ARGS(&details)));
            int http = 0; Check(details->get_HttpStatusCode(&http));
            timings_.Mark(success ? "login-navigation-completed" : "login-navigation-failed");
            timings_.Value("login-web-error", static_cast<int>(error));
            timings_.Value("login-http-status", http);
            if (!success && login_window_ && !closing_ && !login_closing_) {
              const auto title = L"StarryFish · 登录页加载失败（网络错误 " + std::to_wstring(error) +
                  L"，HTTP " + std::to_wstring(http) + L"）";
              SetWindowTextW(login_window_, title.c_str());
              Notice(L"Mobage登录页加载失败；请记录登录窗口标题，不重复点击或输入凭据");
            }
          }); return S_OK;
        }).Get(), &token));
    Check(core->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
          Guard([&] {
            if (!CurrentView(index, generation) || closing_) return;
            if (character_busy_ && character_view_ == index) CancelCharacterSwitch();
            BOOL document = FALSE; Check(args->get_IsNewDocument(&document));
            if (gbrowser::webview::GameSurface(index)) {
              const auto source = WideToUtf8(GameSource(index));
              const auto route = gbrowser::webview::cooldown::BattleRoute(source);
              if (route != skill_routes_[index]) {
                skill_routes_[index] = route;
                if (capture_observers_[index]) capture_observers_[index]->ResetCooldown();
              }
              capture_game_[index] = IsGbfPageUrl(source) && !IsGbfLoginUrl(source);
              if (capture_observers_[index]) capture_observers_[index]->Tick();
            }
            if (document && gbrowser::webview::GameSurface(index)) {
              // Keep the viewport stable while a game document reloads. Clearing
              // the known inset shrinks the WebView before its adapter reports
              // the same inset again. Non-game documents must never inherit it.
              const auto source = WideToUtf8(ComText([&](LPWSTR* value) { return views_[index].core->get_Source(value); }));
              const int inset = gbrowser::webview::RetainedGameInset(moba_css_[index], source);
              if (inset != moba_css_[index]) { moba_css_[index] = inset; LayoutGameView(index); }
            }
            if (index == active_) UpdateAddress();
            if (CurrentView(index, generation) && index == 0 && !closing_) { ++game_source_epoch_; PinArcarumTarget(); }

          }); return S_OK;
        }).Get(), &token));
    if (index == 0 || index >= gbrowser::webview::TabSlots::kFirst)
      Check(core->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
          [this, index, generation](ICoreWebView2* sender, IUnknown*) -> HRESULT {
            Guard([&] {
              if (!CurrentView(index, generation) || closing_) return;
              auto title = ComText([&](LPWSTR* text) { return sender->get_DocumentTitle(text); });
              // Only a bounded UI label. Page titles never identify a login
              // window, authorize a request or enter diagnostic logs.
              for (auto& ch : title) if (ch < 32) ch = L' ';
              if (title.size() > 48) title.resize(48);
              if (IsAuxiliary(index)) {
                if (const auto popup = auxiliary_windows_[index - gbrowser::webview::TabSlots::kAuxFirst])
                  SetWindowTextW(popup, (L"StarryFish · 助手工具 · " + title).c_str());
              } else {
                titles_[index] = title.empty() ? (index ? L"新标签" : L"GBF") : title;
                RebuildTabs();
              }
            }); return S_OK;
          }).Get(), &token));
    if (index == 0 || (index >= gbrowser::webview::TabSlots::kFirst && !IsAuxiliary(index))) {
      ComPtr<ICoreWebView2_15> favicon;
      if (SUCCEEDED(core.As(&favicon))) {
        const auto result = favicon->add_FaviconChanged(Callback<ICoreWebView2FaviconChangedEventHandler>(
            [this, index, generation](ICoreWebView2*, IUnknown*) -> HRESULT {
              if (!closing_ && CurrentView(index, generation)) {
                ++favicon_epochs_[index]; RequestTabFavicon(index, generation);
              }
              return S_OK;
            }).Get(), &token);
        if (FAILED(result)) timings_.Mark("tab-favicon-event-unavailable");
      }
    }
    ComPtr<ICoreWebView2_2> document; Check(core.As(&document));
    Check(document->add_DOMContentLoaded(Callback<ICoreWebView2DOMContentLoadedEventHandler>(
        [this, index, generation](ICoreWebView2* sender, ICoreWebView2DOMContentLoadedEventArgs*) -> HRESULT {
          Guard([&] {
            if (closing_ || !CurrentView(index, generation)) return;
            if (index == 3) { timings_.Mark("login-document-ready"); return; }
            if (gbrowser::webview::GameSurface(index)) {
              ApplyLoadingDisplay(index);
              Check(sender->ExecuteScript(Utf8ToWide(std::string(kGameRoleScript) + (index == kLeftGame ?
                  (panes_.secondary_menu_hidden ? "(true,true);" : "(true,false);") : "(false,false);")).c_str(), nullptr));
              Check(sender->ExecuteScript(Utf8ToWide(std::string(kGameUiScript) + "();").c_str(), nullptr));
              Check(sender->ExecuteScript(Utf8ToWide(std::string(kPanePointerScript) + "();").c_str(), nullptr));
              if (index == 0) { BindGame(); PinArcarumTarget(); }
              return;
            }
            if (index == 1 || index == kArcarumView) {
              const auto helper = index == 1 ? bound_tarou_ : pages_[2];
              const auto actual = WideToUtf8(ComText([&](LPWSTR* text) { return sender->get_Source(text); }));
              if (helper && helper->AllowsPage(actual)) Check(sender->ExecuteScript(
                  Utf8ToWide(std::string(kPanePointerScript) + "(\"" + helper->url() + "\");").c_str(), nullptr));
              if (index == kArcarumView) PinArcarumTarget();
              return;
            }
            if (index != 2) return;
            const auto source = WideToUtf8(ComText([&](LPWSTR* value) { return sender->get_Source(value); }));
            controller_ready_ = pages_[0]->AllowsPage(source);
            if (controller_ready_) timings_.Mark("proxy-control-document-ready");
            ++document_generation_;
            if (controller_ready_ && initialize_new_zero_) {
              initialize_new_zero_ = false;
              StartProxy(ProxyAction::Initialize);
            } else if (controller_ready_) RestoreSavedProxy();
            if (controller_ready_) BindGame();
          }); return S_OK;
        }).Get(), &token));
    Check(core->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
          if (CurrentView(index, generation)) StopNetworking(L"页面进程已停止；不会自动重放代理操作"); return S_OK;
        }).Get(), &token));
    if (gbrowser::webview::GameSurface(index) || index == 1 || index == kArcarumView) Check(core->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this, index, generation](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
              Guard([&] {
                if (!BrowsingAvailable() || !CurrentView(index, generation)) return;
                const auto source = WideToUtf8(ComText([&](LPWSTR* p) { return args->get_Source(p); }));
                const auto helper = index == 1 ? bound_tarou_ : index == kArcarumView ? pages_[2] : nullptr;
                if (!(helper ? helper->AllowsPage(source) : IsGbfPageUrl(source)) ||
                    source != WideToUtf8(ComText([&](LPWSTR* p) { return sender->get_Source(p); }))) return;
                LPWSTR raw = nullptr;
                if (FAILED(args->TryGetWebMessageAsString(&raw))) return;
                std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> held(raw, CoTaskMemFree);
                const std::wstring_view message(raw ? raw : L"");
                if (gbrowser::webview::GameSurface(index) && message == L"gbrowser:active-game") {
                  if (index == active_ || (index == kLeftGame && panes_.dual)) { skill_view_ = index; RefreshCooldown(); }
                } else if (gbrowser::webview::GameSurface(index) && message == L"gbrowser:loading-display") {
                  ApplyLoadingDisplay(index);
                } else if (message == L"gbrowser:back" || message == L"gbrowser:forward") {
                  const auto origin = (generation << 8) | index;
                  PostMessageW(window_, kPageShortcut, static_cast<WPARAM>(message == L"gbrowser:back" ? Shortcut::Back : Shortcut::Forward),
                      static_cast<LPARAM>(origin));
                } else if (gbrowser::webview::GameSurface(index) && message.substr(0, 17) == L"gbrowser:submenu:") {
                  const auto digits = message.substr(17);
                  if (digits.empty() || digits.size() > 3 || digits.find_first_not_of(L"0123456789") != std::wstring_view::npos) return;
                  const int css = std::stoi(std::wstring(digits));
                  if (css > 0 && css <= 512) {
                    menu_css_[index] = css;
                    if (index == kLeftGame) {
                      last_secondary_menu_width_ = MeasuredSecondaryWidth(index);
                      last_secondary_menu_dpi_ = Dpi();
                    }
                  }
                } else if (gbrowser::webview::GameSurface(index) && message.substr(0, 14) == L"gbrowser:moba:") {
                  const auto digits = message.substr(14);
                  if (digits.empty() || digits.size() > 3 || digits.find_first_not_of(L"0123456789") != std::wstring_view::npos) return;
                  const auto inset = std::stoi(std::wstring(digits));
                  if (inset <= 256 && inset != moba_css_[index]) { moba_css_[index] = inset; LayoutGameView(index); }
                }
              }); return S_OK;
            }).Get(), &token));
    if (gbrowser::webview::GameSurface(index)) InstallLoadingDisplay(index);
    if (index == 2) views_[index].Visible(FALSE);
    // A native popup must never navigate before put_NewWindow: the runtime
    // supplies its initial navigation and preserves the real window.opener.
    if (index < 3 && mode_ == RunMode::Offline) {
      const auto instruction = L"当前为离线准备。从菜单打开GBF开始游戏候选；游戏使用独立资料。";
      const auto page = std::wstring(L"<!doctype html><meta charset=utf-8><style>body{font:18px 'Segoe UI';padding:40px;color:#263139;background:#f5f6f7}</style><h1>StarryFish</h1><p>") + instruction + L"</p>";
      Check(core->NavigateToString(page.c_str()));
    }
  }
  void CharacterTrace(const char* event,int value=0) noexcept {
    try {
      // Explicit local diagnostic opt-in, checked once to keep normal input cheap.
      static const auto output=[] {
        wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
        const auto root=std::filesystem::path(path).parent_path();
        return std::filesystem::exists(root/L"shortcut-diagnostics.enabled")?
            root/L"shortcut-diagnostics.log":std::filesystem::path{};
      }();
      if(output.empty())return;
      std::ofstream out(output,std::ios::app);
      out<<GetTickCount64()<<" "<<event<<" "<<value<<"\n";
    }catch(...){}
  }
  void CharacterKeyboardInput(HRAWINPUT handle) noexcept {
    // Foreground-only raw keyboard input is necessary: WebView2's accelerator
    // event deliberately excludes ordinary digits. No background registration,
    // global hook, text collection, or suppression of normal page input.
    if (!character_enabled_ || closing_ || transfer_busy_) return;
    RAWINPUT input{}; UINT bytes = sizeof(input);
    if (GetRawInputData(handle, RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        input.header.dwType != RIM_TYPEKEYBOARD) return;
    const auto key = gbrowser::webview::characters::NormalizeKey(input.data.keyboard.VKey);
    const bool released = (input.data.keyboard.Flags & RI_KEY_BREAK) != 0;
    const bool modified = (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    CharacterKeyInput(key,released,modified);
  }
  void CharacterKeyInput(unsigned key,bool released,bool modified) noexcept {
    if (!character_enabled_ || closing_) return;
    const auto picked = character_tracker_.Feed(key, released, modified);
    if(gbrowser::webview::characters::Actions(shortcut_keys_,key))CharacterTrace(released?"release":"press",static_cast<int>(gbrowser::webview::characters::Actions(shortcut_keys_,key)));
    if (GetForegroundWindow() != window_) return;
    const int slot = gbrowser::webview::characters::Actions(shortcut_keys_, picked);
    CharacterTrace("picked",slot);
    if (!slot) { if (!released && key < 256 && !gbrowser::webview::characters::Actions(shortcut_keys_, key)) CancelCharacterSwitch(); return; }
    const auto index = character_focused_view_;
    if (index >= views_.size() || !gbrowser::webview::GameSurface(index)) return;
    if (character_busy_ && CharacterContext()) {
      character_queue_.Push(static_cast<unsigned>(slot)); return;
    }
    StartCharacterSwitch(index, view_generations_[index], slot);
  }
  void CancelCharacterSwitch(bool clear_queue=true) {
    if(character_busy_)CharacterTrace("cancel-phase",character_sequence_.phase);
    if(clear_queue)character_queue_.Clear();
    KillTimer(window_, kCharacterTimer);
    character_busy_ = false; character_pending_ = false; ++character_epoch_;
    character_source_.clear(); character_motion_ = {};
  }
  void StartCharacterSwitch(size_t index, uint64_t generation, int slot, bool queued=false) {
    // A foreground native key gesture starts a bounded sequence. Renderer
    // calls begin on the next timer tick, never inside input dispatch.
    CancelCharacterSwitch(!queued);
    if (!character_enabled_ || closing_ || failed_ || GetForegroundWindow() != window_) return;
    CharacterTrace("start",slot);
    character_view_ = index; character_generation_ = generation;
    character_navigation_ = navigation_epochs_[index]; character_slot_ = slot; character_context_.clear();
    character_focus_ = GetFocus();
    if (!character_focus_ || !GetCursorPos(&character_cursor_)) return;
    character_sequence_ = {}; character_sequence_.allow_confirmation=(slot & (1u<<17))!=0; character_sequence_.started = GetTickCount64();
    character_busy_ = SetTimer(window_, kCharacterTimer, 50, nullptr) != 0;
  }
  void CompleteCharacterSwitch() {
    // Revalidate the origin before advancing. Every queued action probes fresh
    // DOM state, so e.g. 3,Q uses character 3's skill after its panel opens.
    if(!CharacterContext()) {CancelCharacterSwitch();return;}
    const auto next=character_queue_.Pop();
    if(!next) {CancelCharacterSwitch();return;}
    const auto index=character_view_;const auto generation=character_generation_;
    StartCharacterSwitch(index,generation,static_cast<int>(next),true);
    if(character_busy_)PollCharacterSwitch();else character_queue_.Clear();
  }
  bool CharacterContext() {
    POINT cursor{};
    if (!character_busy_ || !character_enabled_ || closing_ || failed_ ||
        character_sequence_.Expired(GetTickCount64()) || GetForegroundWindow() != window_ ||
        GetFocus() != character_focus_ || character_focused_view_ != character_view_ ||
        !CurrentView(character_view_, character_generation_) ||
        navigation_epochs_[character_view_] != character_navigation_ ||
        !(character_view_ == active_ || (character_view_ == kLeftGame && panes_.dual)) ||
        !views_[character_view_].visible.value_or(false) || !GetCursorPos(&cursor) ||
        std::abs(cursor.x - character_cursor_.x) > 2 || std::abs(cursor.y - character_cursor_.y) > 2) return false;
    for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
      if (GetAsyncKeyState(key) & 0x8000) return false;
    return true;
  }
  void PollCharacterSwitch() noexcept {
    try {
      if (!CharacterContext()) { CharacterTrace("context-failed"); CancelCharacterSwitch(); return; }
      if (character_pending_) return;
      const auto index = character_view_;
      const auto source = WideToUtf8(GameSource(index));
      if (((character_slot_ & (1u << 4)) ? !IsGbfPageUrl(source) : gbrowser::webview::cooldown::BattleRoute(source).empty()) ||
          (!character_source_.empty() && character_source_ != source)) { CancelCharacterSwitch(); return; }
      character_source_ = source;
      using namespace winrt::Windows::Data::Json;
      const auto quoted = JsonValue::CreateStringValue(Utf8ToWide(source)).Stringify();
      const auto script = Utf8ToWide(std::string(kCharacterShortcutScript)) + L"(" + std::wstring(quoted.c_str()) +
          L"," + std::to_wstring(character_slot_) + L"," + std::to_wstring(character_sequence_.phase) + L"," +
          std::wstring(JsonValue::CreateStringValue(Utf8ToWide(character_context_)).Stringify().c_str()) + L");";
      RECT bounds{}; Check(views_[index].controller->get_Bounds(&bounds));
      HWND parent = nullptr; Check(views_[index].controller->get_ParentWindow(&parent));
      POINT origin{}; if (!ClientToScreen(parent, &origin)) { CancelCharacterSwitch(); return; }
      const auto epoch = character_epoch_;
      const auto callback = callbacks_.Track(); character_pending_ = true;
      const auto result = views_[index].core->ExecuteScript(script.c_str(),
          Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
              [this, callback, epoch, index, bounds, parent, origin](HRESULT error, LPCWSTR encoded) -> HRESULT {
                if (!callback.Complete()) return S_OK;
                if (epoch != character_epoch_) return S_OK;
                character_pending_ = false;
                try {
                  if (FAILED(error) || !encoded || !CharacterContext() ||
                      WideToUtf8(GameSource(index)) != character_source_) { CancelCharacterSwitch(); return S_OK; }
                  JsonObject reply;
                  if (!JsonObject::TryParse(encoded, reply)) { CancelCharacterSwitch(); return S_OK; }
                  const auto action = reply.GetNamedNumber(L"action", 0);
                  CharacterTrace("dom",static_cast<int>(action));
                  CharacterTrace("reason",static_cast<int>(reply.GetNamedNumber(L"reason",0)));
                  if (action == 5 && (character_slot_ & (1u << 4))) { CancelCharacterSwitch(); Check(views_[index].core->Reload()); return S_OK; }
                  if (action == 0) { CancelCharacterSwitch(); return S_OK; }
                  if (action == 4) { CompleteCharacterSwitch(); return S_OK; }
                  if (action == 1) { character_sequence_.previous_action = 0; character_motion_ = {}; return S_OK; }
                  const auto context = WideToUtf8(std::wstring(reply.GetNamedString(L"context", L"").c_str()));
                  if (context.empty() || (!character_context_.empty() && context != character_context_)) { CancelCharacterSwitch(); return S_OK; }
                  character_context_ = context;
                  if (action != 2 && action != 3 && action != 6) { CancelCharacterSwitch(); return S_OK; }
                  const double x = reply.GetNamedNumber(L"x"), y = reply.GetNamedNumber(L"y");
                  const double scale = reply.GetNamedNumber(L"scale"), width = reply.GetNamedNumber(L"width"), height = reply.GetNamedNumber(L"height");
                  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) || !std::isfinite(width) || !std::isfinite(height) ||
                      scale < 0.1 || scale > 16 || x < 0 || y < 0 || width <= 0 || height <= 0 || x >= width || y >= height ||
                      std::abs(width * scale - (bounds.right - bounds.left)) > scale + 1 ||
                      std::abs(height * scale - (bounds.bottom - bounds.top)) > scale + 1) { CancelCharacterSwitch(); return S_OK; }
                  RECT current{}; Check(views_[index].controller->get_Bounds(&current));
                  HWND current_parent = nullptr; Check(views_[index].controller->get_ParentWindow(&current_parent));
                  POINT current_origin{};
                  if (current_parent != parent || !EqualRect(&bounds, &current) || !ClientToScreen(parent, &current_origin) ||
                      current_origin.x != origin.x || current_origin.y != origin.y) { CancelCharacterSwitch(); return S_OK; }
                  POINT point{origin.x + bounds.left + static_cast<LONG>(std::lround(x * scale)),
                      origin.y + bounds.top + static_cast<LONG>(std::lround(y * scale))};
                  RECT clip{}; GetClientRect(parent, &clip);
                  POINT client{point.x - origin.x, point.y - origin.y};
                  const HWND hit = WindowFromPoint(point);
                  if (!PtInRect(&bounds, client) || !PtInRect(&clip, client) || !hit ||
                      (hit != character_focus_ && !IsChild(character_focus_, hit))) { CancelCharacterSwitch(); return S_OK; }
                  auto next_sequence = character_sequence_;
                  if (!next_sequence.Click(static_cast<int>(action), point.x, point.y)) {
                    character_sequence_ = next_sequence; character_motion_ = {}; return S_OK;
                  }
                  const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
                  const int width_pixels = GetSystemMetrics(SM_CXVIRTUALSCREEN), height_pixels = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                  if (width_pixels <= 1 || height_pixels <= 1 || !CharacterContext()) { CancelCharacterSwitch(); return S_OK; }
                  const auto now = GetTickCount64();
                  const bool starting_motion=!character_motion_.active;
                  if (starting_motion) {
                    unsigned seed=0;
                    if (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) { CancelCharacterSwitch(); return S_OK; }
                    character_motion_.Begin(character_cursor_.x,character_cursor_.y,point.x,point.y,now,seed);
                    SetTimer(window_,kCharacterTimer,USER_TIMER_MINIMUM,nullptr);
                  }
                  if (character_motion_.to_x!=point.x || character_motion_.to_y!=point.y) { CancelCharacterSwitch(); return S_OK; }
                  const bool arrived=character_motion_.Done(now);
                  // Emit the curved midpoint immediately; the next validated sample
                  // finishes the near-instant path without a long animation.
                  const auto sample=character_motion_.At(starting_motion?now+character_motion_.duration/2:now);
                  point.x=std::clamp(static_cast<LONG>(std::lround(sample[0])),static_cast<LONG>(left),static_cast<LONG>(left+width_pixels-1));
                  point.y=std::clamp(static_cast<LONG>(std::lround(sample[1])),static_cast<LONG>(top),static_cast<LONG>(top+height_pixels-1));
                  if (arrived) {character_sequence_=next_sequence;character_motion_={};SetTimer(window_,kCharacterTimer,50,nullptr);}
                  INPUT input[3]{};
                  for (auto& event : input) event.type = INPUT_MOUSE;
                  input[0].mi.dx = static_cast<LONG>(std::lround((point.x - left) * 65535.0 / (width_pixels - 1)));
                  input[0].mi.dy = static_cast<LONG>(std::lround((point.y - top) * 65535.0 / (height_pixels - 1)));
                  input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
                  input[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                  character_cursor_ = point;
                  const UINT count=arrived?3:1;
                  const UINT sent = SendInput(count, input, sizeof(INPUT));
                  if (sent != count) {
                    // If only move+down entered the stream, release our own
                    // button without replaying the failed click sequence.
                    if (sent == 2) SendInput(1, &input[2], sizeof(INPUT));
                    CancelCharacterSwitch();
                  }
                } catch (...) { CancelCharacterSwitch(); }
                return S_OK;
              }).Get());
      if (FAILED(result)) { callback.Complete(); CancelCharacterSwitch(); }
    } catch (...) { CancelCharacterSwitch(); }
  }
  bool character_enabled_ = false, character_busy_ = false, character_pending_ = false;
  size_t character_view_ = 0, character_focused_view_ = gbrowser::webview::TabSlots::kViewCount;
  gbrowser::webview::characters::KeyTracker character_tracker_;
  gbrowser::webview::characters::InputQueue character_queue_;
  gbrowser::webview::characters::Bindings shortcut_keys_ = gbrowser::webview::characters::Defaults();
  std::unique_ptr<gbrowser::webview::UserStateFile> shortcut_file_;
  std::unique_ptr<gbrowser::webview::ShortcutDialog> shortcut_dialog_;
  int character_slot_ = 0;
  uint64_t character_epoch_ = 0, character_generation_ = 0, character_navigation_ = 0;
  HWND character_focus_ = nullptr;
  POINT character_cursor_{};
  std::string character_source_, character_context_;
  gbrowser::webview::characters::Sequence character_sequence_;
  gbrowser::webview::characters::Motion character_motion_;
  void ApplyLoadingDisplay(size_t index) noexcept {
    if (closing_ || !gbrowser::webview::GameSurface(index) || !views_[index].core) return;
    try {
      const auto script = Utf8ToWide(std::string(kLoadingDisplayScript) + (hide_loading_ ? "(true);" : "(false);"));
      // The script independently checks the exact top-level origin; navigation
      // between this call and execution cannot modify another site's document.
      if (FAILED(views_[index].core->ExecuteScript(script.c_str(), nullptr)))
        timings_.Mark("loading-display-apply-unavailable");
    } catch (...) { timings_.Mark("loading-display-apply-unavailable"); }
  }
  void InstallLoadingDisplay(size_t index) {
    const auto generation = view_generations_[index];
    const auto script = Utf8ToWide(std::string(kLoadingDisplayScript) + "();");
    const auto callback = callbacks_.Track();
    const auto result = views_[index].core->AddScriptToExecuteOnDocumentCreated(script.c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this, callback, index, generation](HRESULT error, LPCWSTR) -> HRESULT {
              if (!callback.Complete()) return S_OK;
              if (!closing_ && CurrentView(index, generation)) {
                if (FAILED(error)) timings_.Mark("loading-display-install-unavailable");
                // Registration is asynchronous. Cover an already created first
                // document as well; DOMContentLoaded supplies the same fallback.
                ApplyLoadingDisplay(index);
              }
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); timings_.Mark("loading-display-install-unavailable"); }
  }
  void ConfigureResources(size_t index) {
    if (gbrowser::webview::GameSurface(index)) {
      capture_observers_[index].reset();
      const auto generation = view_generations_[index];
      capture_observers_[index] = std::make_unique<gbrowser::webview::capture::Observer>(
          views_[index].core.Get(), capture_, index, [this, index, generation] {
            return !closing_ && CurrentView(index, generation) && capture_game_[index];
          });
      capture_observers_[index]->Tick();
      ConfigureCooldown(index);
    }
    auto core = views_[index].core;
    const auto generation = view_generations_[index];
    EventRegistrationToken token{};
    ComPtr<ICoreWebView2_22> requests; Check(core.As(&requests));
    Check(requests->AddWebResourceRequestedFilterWithRequestSourceKinds(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
        COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL));
    Check(core->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
          ComPtr<ICoreWebView2WebResourceResponse> denied;
          const auto response = environment_->CreateWebResourceResponse(nullptr, 403, L"Forbidden", L"Content-Type: text/plain", &denied);
          if (FAILED(response)) return response;
          const auto deny = args->put_Response(denied.Get());
          if (FAILED(deny)) return deny;
          Guard([&] {
            if (!CurrentView(index, generation)) return;
            ComPtr<ICoreWebView2WebResourceRequest> request; Check(args->get_Request(&request));
            auto uri = WideToUtf8(ComText([&](LPWSTR* value) { return request->get_Uri(value); }));
            if (!closing_ && (ViewExtensionAsset(index, uri) || uri == "about:blank" ||
                (BrowsingAvailable() && IsBrowserNetworkUrl(uri))))
            {
              Check(args->put_Response(nullptr));
              TryCacheRequest(index, generation, request.Get(), args);
            }
          }); return S_OK;
        }).Get(), &token));
    ComPtr<ICoreWebView2_2> responses; Check(core.As(&responses));
    Check(responses->add_WebResourceResponseReceived(Callback<ICoreWebView2WebResourceResponseReceivedEventHandler>(
        [this, index, generation](ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs* args) -> HRESULT {
          CaptureCacheResponse(index, generation, args); return S_OK;
        }).Get(), &token));
  }
  bool CacheAllowed(size_t index, uint64_t generation) const {
    if (!resource_cache_ || !cache_enabled_ || cache_clearing_ || closing_ || failed_ || !proxy_verified_ ||
        mode_ != RunMode::GbfOpenNetwork || !CurrentView(index, generation) || !gbrowser::webview::GameSurface(index)) return false;
    return gbrowser::webview::cache::GameDocument(WideToUtf8(ComText([&](LPWSTR* text) { return views_[index].core->get_Source(text); })));
  }
  void TryCacheRequest(size_t index, uint64_t generation, ICoreWebView2WebResourceRequest* request,
      ICoreWebView2WebResourceRequestedEventArgs* args) noexcept {
    // Caching is optional. Any local error leaves the approved request on its
    // original Chromium/proxy path; no auxiliary HTTP client exists here.
    ComPtr<ICoreWebView2Deferral> held;
    uint64_t ticket = 0;
    try {
      if (!CacheAllowed(index, generation) || cache_pending_.size() >= 16 || !resource_cache_->stats().files) return;
      COREWEBVIEW2_WEB_RESOURCE_CONTEXT context{}; Check(args->get_ResourceContext(&context));
      if (context != COREWEBVIEW2_WEB_RESOURCE_CONTEXT_IMAGE && context != COREWEBVIEW2_WEB_RESOURCE_CONTEXT_SCRIPT &&
          context != COREWEBVIEW2_WEB_RESOURCE_CONTEXT_STYLESHEET && context != COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FONT &&
          context != COREWEBVIEW2_WEB_RESOURCE_CONTEXT_MEDIA) return;
      const auto input = CacheRequest(request); if (!gbrowser::webview::cache::Eligible(input)) return;
      ComPtr<ICoreWebView2WebResourceResponse> denied;
      Check(environment_->CreateWebResourceResponse(nullptr, 403, L"Forbidden", L"Content-Type: text/plain", &denied));
      Check(args->GetDeferral(&held));
      Check(args->put_Response(denied.Get()));
      const auto id = ++cache_sequence_;
      ticket = id;
      cache_pending_.emplace(id, CachePending{args, held, index, generation, navigation_epochs_[index], cache_epoch_, cache_network_epoch_, GetTickCount64(), input.url});
      if (!resource_cache_->Lookup(id, input.url)) { cache_pending_.erase(id); args->put_Response(nullptr); held->Complete(); return; }
      ++pending_;
    } catch (...) { cache_pending_.erase(ticket); if (held) { args->put_Response(nullptr); held->Complete(); } }
  }
  void CaptureCacheResponse(size_t index, uint64_t generation, ICoreWebView2WebResourceResponseReceivedEventArgs* args) noexcept {
    try {
      if (!CacheAllowed(index, generation) || cache_captures_ >= 4 || cache_jobs_ >= 16) return;
      ComPtr<ICoreWebView2WebResourceRequest> request; Check(args->get_Request(&request));
      const auto input = CacheRequest(request.Get()); if (!gbrowser::webview::cache::Eligible(input)) return;
      ComPtr<ICoreWebView2WebResourceResponseView> response; Check(args->get_Response(&response));
      int status = 0; Check(response->get_StatusCode(&status));
      ComPtr<ICoreWebView2HttpResponseHeaders> headers; Check(response->get_Headers(&headers));
      auto entry = gbrowser::webview::cache::Admit(input, status, CacheHeaders(headers.Get()), gbrowser::webview::cache::Now());
      if (!entry) return;
      const auto epoch = cache_epoch_, navigation = navigation_epochs_[index];
      const auto callback = callbacks_.Track(); ++cache_captures_;
      const auto hr = response->GetContent(Callback<ICoreWebView2WebResourceResponseViewGetContentCompletedHandler>(
          [this, callback, index, generation, epoch, navigation, entry = std::move(*entry)](HRESULT error, IStream* content) mutable -> HRESULT {
            if (!callback.Complete()) return S_OK;
            --cache_captures_;
            try {
              if (SUCCEEDED(error) && content && CacheAllowed(index, generation) && epoch == cache_epoch_ &&
                  navigation == navigation_epochs_[index] && cache_jobs_ < 16 && resource_cache_->Store(++cache_sequence_, std::move(entry), content)) {
                ++pending_; ++cache_jobs_;
              }
            } catch (...) {} // A capture failure never interrupts gameplay.
            Drain(); return S_OK;
          }).Get());
      if (FAILED(hr)) { callback.Complete(); --cache_captures_; }
    } catch (...) {}
  }
  void CompleteCacheJobs() {
    if (!resource_cache_ || cache_stopping_) return;
    if (cache_initializing_ && resource_cache_->stats().initialized) { cache_initializing_ = false; --pending_; }
    for (auto& result : resource_cache_->Take()) {
      const auto found = cache_pending_.find(result.id);
      if (found != cache_pending_.end()) {
        auto held = std::move(found->second); cache_pending_.erase(found);
        if (!held.deferral) { --pending_; continue; } // Timed out to normal network already.
        try {
          // The original default permit is revoked on close/proxy change/view
          // reuse. Recheck immediately before releasing every async deferral.
          const bool valid = !closing_ && !failed_ && proxy_verified_ && CurrentView(held.view, held.generation) &&
              held.navigation == navigation_epochs_[held.view] && held.network == cache_network_epoch_;
          if (!valid) {
            ComPtr<ICoreWebView2WebResourceResponse> denied;
            if (SUCCEEDED(environment_->CreateWebResourceResponse(nullptr, 403, L"Forbidden", L"Content-Type: text/plain", &denied)))
              held.args->put_Response(denied.Get());
          } else {
            Check(held.args->put_Response(nullptr));
            if (cache_enabled_ && !cache_clearing_ && GetTickCount64() - held.started < 250 && held.epoch == cache_epoch_ &&
                result.entry && result.entry->url == held.url) {
              const auto headers = gbrowser::webview::cache::ReplayHeaders(*result.entry, gbrowser::webview::cache::Now());
              if (!headers.empty()) {
                ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(result.entry->body.data()),
                    static_cast<UINT>(result.entry->body.size())));
                ComPtr<ICoreWebView2WebResourceResponse> response;
                if (stream && SUCCEEDED(environment_->CreateWebResourceResponse(stream.Get(), 200, L"OK", Utf8ToWide(headers).c_str(), &response)) &&
                    SUCCEEDED(held.args->put_Response(response.Get()))) { ++cache_served_; cache_served_bytes_ += result.entry->body.size(); resource_cache_->Hit(*result.entry); }
              }
            }
          }
        } catch (...) {} // Keep network fallback, unless the gate was revoked.
        held.deferral->Complete();
      } else {
        --cache_jobs_;
        if (result.id == cache_clear_id_) {
          cache_clearing_ = false;
          Notice(result.cleared ? L"静态资源缓存已清理；之后会按需重新获取。账号和插件数据保持不变。" : L"静态资源缓存未能完整清理；游戏仍使用正常网络。");
        }
      }
      --pending_;
    }
    // A slow disk must not hold game loading behind the cache. Keep the worker
    // ticket accounted for shutdown, but release its browser request promptly.
    for (auto& pair : cache_pending_) {
      auto& held = pair.second;
      if (!held.deferral || GetTickCount64() - held.started < 250) continue;
      if (!closing_ && !failed_ && proxy_verified_ && CurrentView(held.view, held.generation) &&
          held.navigation == navigation_epochs_[held.view] && held.network == cache_network_epoch_)
        held.args->put_Response(nullptr);
      held.deferral->Complete(); held.deferral.Reset(); held.args.Reset();
    }
    Drain();
  }
  void GetInstalled() {
    ComPtr<ICoreWebView2_13> version; Check(views_[0].core.As(&version));
    ComPtr<ICoreWebView2Profile> profile; Check(version->get_Profile(&profile));
    if (FAILED(profile.As(&profile_))) {
      StopNetworking(L"当前WebView2运行库不支持扩展接入；请更新微软Evergreen运行库后重启，现有资料保留");
      return;
    }
    const auto callback = callbacks_.Track();
    const auto result = profile_->GetBrowserExtensions(Callback<ICoreWebView2ProfileGetBrowserExtensionsCompletedHandler>(
        [this, callback](HRESULT error, ICoreWebView2BrowserExtensionList* list) -> HRESULT {
          if (!callback.Complete()) return S_OK;
          Guard([&] {
            Check(error); if (closing_) return;
            if (!list) throw std::runtime_error("缺少扩展登记列表");
            UINT32 count = 0; Check(list->get_Count(&count));
            if (count > 64) throw std::runtime_error("扩展列表超限");
            for (UINT32 index = 0; index < count; ++index) {
              ComPtr<ICoreWebView2BrowserExtension> extension; Check(list->GetValueAtIndex(index, &extension));
              if (!extension) throw std::runtime_error("扩展登记项缺失");
              const auto id = ComText([&](LPWSTR* text) { return extension->get_Id(text); });
              const auto name = ComText([&](LPWSTR* text) { return extension->get_Name(text); });
              constexpr const wchar_t* names[]{L"Proxy SwitchyOmega 3 (ZeroOmega)", L"Tarou", L"arcarum-dungeon", L"StarryFish 内置书签与汉化"};
              for (size_t i = 0; i < extensions_.size(); ++i) {
                if (id == Utf8ToWide(UnpackedExtensionId(sources_[i]))) extensions_[i] = extension;
                else if (name == names[i])
                  throw std::runtime_error("同角色扩展已在其他路径登记；保留现有登记，不重复安装");
              }
            }
            LoadComponent(0);
          }); Drain(); return S_OK;
        }).Get());
    if (FAILED(result)) { callback.Complete(); Check(result); }
  }
  const gbrowser::webview::AcceptedComponent& Component(size_t index) const {
    return index < 3 ? catalogue_->entries().at(index) : scripts_component_;
  }
  void ValidateComponent(size_t index, bool record = false) {
    BOOL enabled = FALSE; Check(extensions_[index]->get_IsEnabled(&enabled));
    const auto id = ComText([&](LPWSTR* text) { return extensions_[index]->get_Id(text); });
    if (!enabled || id != Utf8ToWide(UnpackedExtensionId(sources_[index])))
      throw std::runtime_error("扩展被停用或来源身份不符；不会自动改变用户停用决定");
    if (record) registrations_->Record(index, Component(index).tree);
    ++loaded_;
    if (index + 1 < extensions_.size()) LoadComponent(index + 1);
    else {
      // UDF was read back from the environment; now all fixed components are
      // verified. A failed target never replaces the previous successful ID.
      if (pending_profile_import && pending_profile_import->id == active_profile_) {
        if (!pending_profile_import->restored) { RestoreProfile(); return; }
        gbrowser::webview::transfer::RestoreSessionCookies(views_[0].core.Get(), pending_profile_import->document);
        pending_profile_import.reset();
      }
      profiles_->Activate(active_profile_);
      timings_.Mark("component-registrations-ready");
      phase_ = L"准备代理控制页";

      if (BrowsingAvailable()) OpenHome(false);
      else Check(views_[0].core->Navigate(Utf8ToWide(pages_[3]->url()).c_str()));
      Check(views_[2].core->Navigate(Utf8ToWide(pages_[0]->url()).c_str()));
      RecordComponents();
    }
  }
  void LoadComponent(size_t index) {
    const bool registered = extensions_[index] != nullptr;
    if (index == 0) {
      // Persist before Add; a crash after runtime registration but before the
      // options document is ready can resume only this unstarted first setup.
      initialize_new_zero_ = gbrowser::webview::ZeroInitialization(data_).Prepare(!registered);
    }
    if (registered) {
      BOOL enabled = FALSE; Check(extensions_[index]->get_IsEnabled(&enabled));
      // Same-path Add re-enables disabled extensions on runtime 154. Never
      // use reinstallation to override a user's disabled state.
      if (!enabled) throw std::runtime_error("扩展已停用；保留停用状态，不执行重载");
      if (index == 0) { ValidateComponent(index); return; } // Fixed ZeroOmega is never manually updated.
    }
    // Check unknown/corrupt local receipt outcomes before any Add, including
    // when the runtime reports no registration in this profile.
    const bool unchanged = index != 0 && registrations_->Matches(index, Component(index).tree);
    if (registered && unchanged) { ValidateComponent(index); return; }
    const auto callback = callbacks_.Track();
    const auto result = profile_->AddBrowserExtension(sources_[index].c_str(),
        Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
            [this, callback, index](HRESULT error, ICoreWebView2BrowserExtension* extension) -> HRESULT {
              if (!callback.Complete()) return S_OK;
              Guard([&] {
                Check(error); if (closing_) return;
                if (!extension) throw std::runtime_error("缺少自动加载的扩展身份");
                extensions_[index] = extension;
                ValidateComponent(index, index != 0);
              }); Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); Check(result); }
  }
  void RecordComponents() {
    // Public component facts only; not webpage URLs, login or extension storage.
    std::ofstream stream(data_ / L"host-components.log", std::ios::trunc);
    if (!stream) throw std::runtime_error("无法保存候选组件状态");
    stream << "HOST_COMPONENTS_AUTOMATICALLY_LOADED_NOT_GAME_ACCEPTANCE\n";
    for (size_t index = 0; index < extensions_.size(); ++index)
      stream << WideToUtf8(Component(index).name) << ' ' << UnpackedExtensionId(sources_[index]) << " ENABLED\n";
    stream << (catalogue_->installed() ? "ACCEPTED_INITIAL_INSTALL_TREE_RECHECKED\n" : "FIXED_REVIEW_BYTES_NOT_LATEST_AT_INSTALL\n");
    stream << "NETWORK_PROXY_REQUIRES_FRESH_MATCHING_OBSERVATION\n";
  }

  void OpenLogin(size_t index, ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) {
    if (index != 0 || !BrowsingAvailable() || login_window_) return;
    const auto source = WideToUtf8(ComText([&](LPWSTR* value) { return sender->get_Source(value); }));
    const auto target = WideToUtf8(ComText([&](LPWSTR* value) { return args->get_Uri(value); }));
    BOOL gesture = FALSE; Check(args->get_IsUserInitiated(&gesture));
    if (!IsGbfLoginPopup(mode_, source, target, gesture != FALSE)) {
      Notice(L"仅接续由游戏主动打开的Mobage登录窗口"); return;
    }
    phase_ = L"创建Mobage登录窗口";
    timings_.Mark("login-popup-request-accepted");
    KillTimer(window_, 8);
    ++login_attempt_;
    login_resume_ = {view_generations_[0], navigation_epochs_[0], login_attempt_, Utf8ToWide(source)};
    auto request = std::make_shared<WindowRequest>(args);
    login_closing_ = false;
    login_window_ = CreateWindowExW(0, L"GbrowserMobageLogin", L"StarryFish · Mobage登录",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 620, 760, window_, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)), this);
    if (!login_window_) throw std::runtime_error("无法创建登录窗口");
    ++pending_; login_pending_ = true;
    const auto callback = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this, request](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
          --pending_; login_pending_ = false;
          Guard([&] {
            Check(error);
            if (!controller) throw std::runtime_error("登录控制器缺失");
            views_[3].controller = controller;
            ++view_generations_[3];
            Check(controller->get_CoreWebView2(&views_[3].core));
            if (login_closing_ || !BrowsingAvailable()) return;
            // Same environment/default profile as the opener. Configure the
            // untouched view before assigning it to the browser's own request.
            Configure(3); LayoutLogin();
            Check(request->args->put_NewWindow(views_[3].core.Get()));
            ConfigureResources(3);
            timings_.Mark("login-web-contents-bound");
            views_[3].Visible(TRUE);
            ShowWindow(login_window_, SW_SHOWNORMAL);
            phase_ = L"加载Mobage登录页";
          });
          Guard([&] { request->Complete(); timings_.Mark("login-request-deferral-completed"); });
          // put_NewWindow replaces the view's web contents. Restore focus
          // after its deferral, never to the discarded pre-binding child HWND.
          if (!closing_ && !failed_ && login_window_) PostMessageW(login_window_, kFocusLogin, 0, 0);
          if (closing_ || login_closing_ || failed_) CloseLogin();
          Drain(); return S_OK;
        });
    const auto result = environment_->CreateCoreWebView2Controller(login_window_, callback.Get());
    if (FAILED(result)) {
      --pending_; login_pending_ = false; CloseLogin(); Check(result);
    }
  }
  void LayoutLogin() {
    if (!login_window_ || !views_[3].controller) return;
    RECT bounds{}; GetClientRect(login_window_, &bounds);
    views_[3].Bounds(bounds);
  }
  void FocusLogin() {
    if (!login_window_ || !views_[3].controller || closing_ || login_closing_ || failed_ ||
        GetForegroundWindow() != login_window_) return;
    // Returning from KeePassXC can focus the native frame rather than the
    // browser child. PROGRAMMATIC resumes the page's last focused input; do
    // not select a form field, read it, inject keys or steal foreground focus.
    const auto focus = GetFocus();
    if (!focus || focus == login_window_ || !IsChild(login_window_, focus))
      Check(views_[3].controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
  }
  void CloseLogin(bool resume = false) {
    const bool was_open = login_window_ && !login_closing_;
    login_closing_ = true;
    if (login_pending_) return;
    if (views_[3].controller) views_[3].controller->Close();
    views_[3].core.Reset(); views_[3].controller.Reset();
    if (login_window_) { DestroyWindow(login_window_); login_window_ = nullptr; }
    if (resume && was_open && !closing_ && !failed_ && BrowsingAvailable() &&
        gbrowser::webview::LoginResumeEntry(login_resume_.source)) {
      // The SDK may have queued its authorization callback before window.close.
      // Let it finish before considering a single ordinary game entry reload.
      if (SetTimer(window_, 8, 750, nullptr)) timings_.Mark("login-resume-grace-started");
      else timings_.Mark("login-resume-timer-unavailable");
    }
  }
  void ResumeLogin() {
    KillTimer(window_, 8);
    if (closing_ || failed_ || login_window_ || login_pending_ || !BrowsingAvailable() || !views_[0].core) return;
    const auto source = ComText([&](LPWSTR* value) { return views_[0].core->get_Source(value); });
    const auto recovery = login_resume_;
    if (!recovery.Matches(view_generations_[0], navigation_epochs_[0], login_attempt_, source)) return;
    const auto callback = callbacks_.Track();
    const auto result = views_[0].core->ExecuteScript(gbrowser::webview::kLoginChannelVisible,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, callback, recovery](HRESULT error, LPCWSTR json) -> HRESULT {
              if (!callback.Complete()) return S_OK;
              Guard([&] {
                if (FAILED(error) || !json || std::wstring_view(json) != L"true" || closing_ || failed_ ||
                    login_window_ || login_pending_ || !BrowsingAvailable() || !views_[0].core) return;
                const auto current = ComText([&](LPWSTR* value) { return views_[0].core->get_Source(value); });
                if (!recovery.Matches(view_generations_[0], navigation_epochs_[0], login_attempt_, current)) return;
                // Consume before Reload: reentrancy or a late callback cannot repeat it.
                ++login_attempt_;
                Check(views_[0].core->Reload());
                timings_.Mark("login-channel-entry-reloaded");
              }); Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); timings_.Mark("login-resume-probe-unavailable"); }
  }

  static bool IsAuxiliary(size_t index) {
    return index >= gbrowser::webview::TabSlots::kAuxFirst && index < gbrowser::webview::TabSlots::kViewCount;
  }
  size_t AuxiliaryIndex(HWND window) const {
    for (size_t slot = 0; slot < auxiliary_windows_.size(); ++slot)
      if (auxiliary_windows_[slot] == window) return slot + gbrowser::webview::TabSlots::kAuxFirst;
    return views_.size();
  }
  void LayoutAuxiliary(size_t index) {
    const auto popup = auxiliary_windows_[index - gbrowser::webview::TabSlots::kAuxFirst];
    if (!popup || !views_[index].controller) return;
    RECT bounds{}; GetClientRect(popup, &bounds);
    views_[index].Bounds(bounds);
  }
  void ReleaseAuxiliary(gbrowser::webview::TabSlots::Ticket ticket) {
    if (!IsAuxiliary(ticket.index) || !tab_slots_.Current(ticket)) return;
    const auto slot = ticket.index - gbrowser::webview::TabSlots::kAuxFirst;
    if (views_[ticket.index].controller) Check(views_[ticket.index].controller->Close());
    views_[ticket.index].core.Reset(); views_[ticket.index].controller.Reset();
    auxiliary_pages_[slot].reset();
    const auto popup = auxiliary_windows_[slot]; auxiliary_windows_[slot] = nullptr;
    if (popup) DestroyWindow(popup);
    tab_slots_.Release(ticket);
  }
  void CloseAuxiliary(size_t index, uint64_t generation) {
    const gbrowser::webview::TabSlots::Ticket ticket{index, generation};
    if (!IsAuxiliary(index) || !tab_slots_.Current(ticket)) return;
    if (tab_slots_.Cancel(ticket)) ReleaseAuxiliary(ticket);
    else if (const auto popup = auxiliary_windows_[index - gbrowser::webview::TabSlots::kAuxFirst])
      ShowWindow(popup, SW_HIDE); // Retain HWND/slot until real creation completion.
  }
  void OpenAuxiliary(ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) {
    if (!BrowsingAvailable() || !bound_game_id_) return;
    const size_t role = sender == views_[1].core.Get() ? 1 : sender == views_[kArcarumView].core.Get() ? 2 : 0;
    if (!role) return;
    const auto source = WideToUtf8(ComText([&](LPWSTR* value) { return sender->get_Source(value); }));
    const auto target = WideToUtf8(ComText([&](LPWSTR* value) { return args->get_Uri(value); }));
    const auto source_page = role == 1 ? bound_tarou_ : pages_[2];
    if (!source_page || !source_page->AllowsPage(source)) return;
    // Original helper registry only, checked against its retained source files.
    // Arbitrary web links, debug windows and nested popups are not capabilities.
    auto page = pages_[role]->Auxiliary(target);
    if (!page) { Notice(L"该助手窗口未接入；原有游戏和面板保留"); return; }
    const auto ticket = tab_slots_.ReserveAuxiliary();
    if (!ticket) { Notice(L"最多同时打开4个助手工具窗口；请先关闭一个"); return; }
    const auto index = ticket->index, slot = index - gbrowser::webview::TabSlots::kAuxFirst;
    view_generations_[index] = ticket->generation;
    auxiliary_pages_[slot] = std::move(page);
    try {
      auto request = std::make_shared<WindowRequest>(args);
      auxiliary_windows_[slot] = CreateWindowExW(0, L"GbrowserHelperWindow", L"StarryFish · 助手工具",
          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 850, 700, window_, nullptr,
          reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)), this);
      if (!auxiliary_windows_[slot]) throw std::runtime_error("无法创建助手工具窗口");
      ++pending_;
      const auto result = environment_->CreateCoreWebView2Controller(auxiliary_windows_[slot],
          Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
              [this, ticket = *ticket, request](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                --pending_;
                const bool wanted = tab_slots_.Complete(ticket) && BrowsingAvailable();
                if (!wanted || FAILED(error) || !controller) {
                  if (controller) controller->Close();
                  Guard([&] { ReleaseAuxiliary(ticket); request->Complete();
                    if (wanted) Notice(L"助手工具窗口未能创建；游戏页面保留"); });
                  Drain(); return S_OK;
                }
                Guard([&] {
                  views_[ticket.index].controller = controller;
                  Check(controller->get_CoreWebView2(&views_[ticket.index].core));
                  Configure(ticket.index); LayoutAuxiliary(ticket.index);
                  if (!tab_slots_.Current(ticket) || !BrowsingAvailable()) return;
                  // Bind the untouched same-environment view before adding its
                  // resource handler; preserve the browser's real opener.
                  Check(request->args->put_NewWindow(views_[ticket.index].core.Get()));
                  ConfigureResources(ticket.index);
                  views_[ticket.index].Visible(true);
                  ShowWindow(auxiliary_windows_[ticket.index - gbrowser::webview::TabSlots::kAuxFirst], SW_SHOWNORMAL);
                  timings_.Mark("helper-popup-bound");
                });
                Guard([&] { request->Complete(); });
                if (!BrowsingAvailable()) Guard([&] { CloseAuxiliary(ticket.index, ticket.generation); });
                Drain(); return S_OK;
              }).Get());
      if (FAILED(result)) {
        --pending_; tab_slots_.Complete(*ticket); ReleaseAuxiliary(*ticket); Check(result);
      }
    } catch (...) {
      tab_slots_.Complete(*ticket); ReleaseAuxiliary(*ticket); throw;
    }
  }

  void BindGame() {
    if (closing_ || failed_ || !controller_ready_ || !BrowsingAvailable()) return;
    // Coalesce a newer document-ready event until the existing query returns.
    // Never overlap requests or bind a reply to the replacement document.
    if (target_polling_) { target_rebind_pending_ = true; return; }
    const auto source = WideToUtf8(ComText([&](LPWSTR* value) { return views_[0].core->get_Source(value); }));
    if (!IsGbfPageUrl(source)) return;
    target_source_ = source;
    target_started_ = GetTickCount64();
    ++target_generation_;
    const auto script = std::string(kTargetScript) + '(' + std::to_string(target_generation_) +
        ",\"" + pages_[0]->url() + "\"," + std::to_string(bound_game_id_) + ");";
    Check(views_[2].core->ExecuteScript(Utf8ToWide(script).c_str(), nullptr));
    if (!SetTimer(window_, 3, 350, nullptr)) throw std::runtime_error("无法核对游戏目标");
  }
  void PollGameTarget() {
    if (closing_ || failed_ || !controller_ready_ || !BrowsingAvailable()) return;
    if (GetTickCount64() - target_started_ >= 5000) {
      KillTimer(window_, 3);
      Notice(target_polling_ ? L"目标查询超时，仍等待运行库返回；不会重叠提交" : L"游戏目标尚未确定；可点太郎再次核对"); return;
    }
    if (target_polling_) return;
    const auto generation = target_generation_;
    const auto document = document_generation_;
    target_polling_ = generation; const auto callback = callbacks_.Track();
    const auto result = views_[2].core->ExecuteScript(L"window.__gbrowserGameTarget || null",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, callback, generation, document](HRESULT error, LPCWSTR value) -> HRESULT {
              if (!callback.Complete()) return S_OK;
              if (target_polling_ == generation) target_polling_ = 0;
              Guard([&] {
                Check(error);
                if (closing_ || failed_ || generation != target_generation_ || document != document_generation_ ||
                    !BrowsingAvailable() || !value || std::wstring_view(value) == L"null") return;
                if (GetTickCount64() - target_started_ >= 5000) {
                  KillTimer(window_, 3); Notice(L"游戏目标查询已超时；可点太郎再次核对"); return;
                }
                const auto current = WideToUtf8(ComText([&](LPWSTR* text) { return views_[0].core->get_Source(text); }));
                if (current != target_source_ || !IsGbfPageUrl(current)) {
                  KillTimer(window_, 3);
                  target_rebind_pending_ = IsGbfPageUrl(current);
                  return;
                }
                const auto reply = gbrowser::webview::ParseTargetReply(WideToUtf8(value), generation);
                KillTimer(window_, 3);
                if (reply.reason) {
                  timings_.Value("game-target-rejection", reply.reason);
                  if (reply.reason == 4) timings_.Value("game-target-invalid-id", reply.sample);
                  Notice(L"未找到唯一游戏目标；不会绑定到其他页面"); return;
                }
                const auto tab = reply.id;
                // Once positively identified, the permanent game controller
                // keeps this runtime tab ID. Other GBF tabs cannot steal it.
                const bool open_helper = gbrowser::webview::NeedsHelperTargetNavigation(
                    bound_game_id_, tab, bound_tarou_ != nullptr);
                bound_game_id_ = tab;
                timings_.Mark("game-target-bound", target_started_);
                if (open_helper) {
                  const auto& component = catalogue_->entries()[1];
                  bound_tarou_ = PrepareExtensionPage(sources_[1], component.tree, component.files, component.bytes,
                      WideToUtf8(component.entry), "webview2-candidate", data_, tab);
                  Check(views_[1].core->Navigate(Utf8ToWide(bound_tarou_->url()).c_str()));
                  timings_.Mark("tarou-first-target-navigation", target_started_);
                } else timings_.Mark("tarou-existing-target-preserved", target_started_);
                EnsurePaneView(kLeftGame); EnsurePaneView(kArcarumView);

              });
              if (target_rebind_pending_) {
                target_rebind_pending_ = false;
                Guard([&] { BindGame(); });
              }
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); if (target_polling_ == generation) target_polling_ = 0; Check(result); }
  }

  void RestoreSavedProxy() {
    if (restore_started_ || mode_ == RunMode::Offline || closing_ || failed_) return;
    restore_started_ = true;
    const auto& saved = proxy_preference_->state();
    // Existing profiles without a host record may already be correctly set.
    // Adopt only fresh matching readback, never silently choose on a mismatch.
    restore_fallback_ = saved.uncertain ? std::optional<ProxyAction>{} : saved.choice;
    if (saved.choice) { StartProxy(*saved.choice, true, saved.custom_name); return; }
    if (saved.uncertain) return; // Never replay an uncertain first submission.
    gbrowser::webview::UserStateFile choice(root_ / L"install-choice.state");
    if (choice.bytes()) StartProxy(gbrowser::webview::DecodeInstallChoice(*choice.bytes()));
  }
  void ReadProxyMenu() {
    if (closing_ || failed_ || !controller_ready_ || loaded_!=4) { Notice(L"ZeroOmega尚未就绪，请稍后再打开代理菜单");return; }
    if (proxy_menu_loading_) return;
    const auto source=WideToUtf8(ComText([&](LPWSTR* text){return views_[2].core->get_Source(text);}));
    if (!pages_[0]->AllowsPage(source)) {Notice(L"ZeroOmega控制页已变化，请重新打开设置");return;}
    proxy_menu_loading_=true;proxy_menu_generation_=document_generation_;proxy_menu_started_=GetTickCount64();
    ++proxy_menu_request_;
    const std::string url="\""+pages_[0]->url()+"\"";
    const auto script=std::string(kProxyProfilesScript)+"("+url+");"+kProxyMenuScript+
        "("+std::to_string(proxy_menu_request_)+","+url+");";
    const auto result=views_[2].core->ExecuteScript(Utf8ToWide(script).c_str(),nullptr);
    if (FAILED(result) || !SetTimer(window_,7,100,nullptr)) {
      proxy_menu_loading_=false;Notice(L"情景模式列表未能读取，请稍后重试");
    }
  }
  void PollProxyMenu() {
    if (!proxy_menu_loading_ || proxy_menu_polling_) return;
    if (closing_ || failed_ || !controller_ready_ || document_generation_!=proxy_menu_generation_ ||
        GetTickCount64()-proxy_menu_started_>60000) {
      proxy_menu_loading_=false;KillTimer(window_,7);
      if (!closing_) Notice(L"情景模式列表未能读取，请在ZeroOmega设置中核对");return;
    }
    const auto request=proxy_menu_request_,generation=proxy_menu_generation_;
    proxy_menu_polling_=true;const auto callback = callbacks_.Track();
    const auto script=L"window.__starryfishProxyMenuTake ? window.__starryfishProxyMenuTake("+std::to_wstring(request)+L") : null";
    const auto result=views_[2].core->ExecuteScript(script.c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [this, callback,request,generation](HRESULT error,LPCWSTR value) -> HRESULT {
          if (!callback.Complete()) return S_OK;proxy_menu_polling_=false;
          if (!closing_ && proxy_menu_loading_ && request==proxy_menu_request_ && generation==document_generation_) {
            const std::wstring_view encoded=value?value:L"null";
            if (FAILED(error) || encoded!=L"null") {
              proxy_menu_loading_=false;KillTimer(window_,7);
              try {
                if (FAILED(error) || encoded.size()<2 || encoded.size()>70002 || encoded.front()!=L'"' || encoded.back()!=L'"')
                  throw std::runtime_error("情景模式列表不可用");
                proxy_menu_=gbrowser::webview::DecodeProxyProfileMenu(WideToUtf8(std::wstring(encoded.substr(1,encoded.size()-2))));
                if (!PostMessageW(window_,kProxyMenuReady,static_cast<WPARAM>(request),0)) throw std::runtime_error("情景模式菜单不可用");
              } catch (...) {Notice(L"情景模式列表未能读取，请在ZeroOmega设置中核对");}
            }
          }
          Drain();return S_OK;
        }).Get());
    if (FAILED(result)) {callback.Complete();proxy_menu_polling_=false;proxy_menu_loading_=false;KillTimer(window_,7);
      Notice(L"情景模式列表未能读取，请稍后重试");Drain();}
  }
  void ShowProxyMenu(uint64_t request) {
    if (closing_ || failed_ || request!=proxy_menu_request_ || document_generation_!=proxy_menu_generation_ || !controller_ready_) return;
    std::vector<std::string> names;
    for (const auto* name:{"acgpower","direct","岛风go"})
      if (std::find(proxy_menu_.names.begin(),proxy_menu_.names.end(),name)!=proxy_menu_.names.end()) names.emplace_back(name);
    for (const auto& name:proxy_menu_.names) if (std::find(names.begin(),names.end(),name)==names.end()) names.push_back(name);
    HMENU menu=CreatePopupMenu();if (!menu) {Notice(L"代理菜单未能打开");return;}
    for (size_t i=0;i<names.size();++i) {
      auto label=names[i]=="direct"?std::wstring(L"直连"):Utf8ToWide(names[i]);
      if (label.size()>48) {label.resize(48);if (label.back()>=0xd800 && label.back()<=0xdbff) label.pop_back();label+=L"…";}
      AppendMenuW(menu,MF_STRING|(names[i]==proxy_menu_.current?MF_CHECKED:0),kProxyProfileFirst+i,label.c_str());
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,kProxySettings,L"ZeroOmega设置");
    const auto menu_art=theme_.PrepareMenu(menu);
    RECT button{};GetWindowRect(buttons_[1],&button);
    const int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,button.right,button.bottom,0,window_,nullptr);
    DestroyMenu(menu);
    if (selected==kProxySettings) {Action(selected);return;}
    if (selected<kProxyProfileFirst || static_cast<size_t>(selected-kProxyProfileFirst)>=names.size()) return;
    if (closing_ || !controller_ready_ || document_generation_!=proxy_menu_generation_ || request!=proxy_menu_request_) return;
    const auto& name=names[static_cast<size_t>(selected-kProxyProfileFirst)];
    if (mode_==RunMode::Offline && name!="direct") {Notice(L"离线候选不启动联网代理");return;}
    StartProxy(ProxyAction::Custom,false,name,true);
  }
  void StartProxy(ProxyAction action, bool read_only = false, std::string custom_name = {}, bool explicit_selection = false) {
    if (action==ProxyAction::Custom && !gbrowser::webview::ValidProxyProfileName(custom_name)) {
      Notice(L"情景模式名称无法识别；现有配置保留");return;
    }
    if (failed_) { Notice(L"当前候选已停止网络接续；请关闭后重新启动"); return; }
    if (!controller_ready_ || closing_ || loaded_ != 4) { Notice(L"代理控制页尚未就绪"); return; }
    if (proxy_ && (proxy_->pending() || (proxy_->state() == ProxyInitializationState::Unconfirmed &&
        !explicit_selection && (read_only || !proxy_recorded_ || proxy_preference_->state().uncertain)))) {
      Notice(L"上一操作结果尚未确定；不会重复写入代理"); return;
    }
    if (!read_only && action != ProxyAction::Initialize && proxy_preference_->state().uncertain && !explicit_selection) {
      Notice(L"上次代理提交结果未知；请保留现场，在原版设置中核对，不自动重放"); return;
    }
    if (action == ProxyAction::Initialize) {
      // A freshly added ZeroOmega gets installer defaults once. An existing
      // installation, interrupted reset or disabled extension is never reset.
      if (!gbrowser::webview::ZeroInitialization(data_).Begin()) {
        Notice(L"初始配置已有执行记录；不会重复重置"); return;
      }
    }
    restore_probe_ = read_only; proxy_recorded_ = false;
    proxy_explicit_selection_ = explicit_selection;
    proxy_uncertain_before_ = proxy_preference_->state().uncertain;
    proxy_custom_name_ = std::move(custom_name);
    phase_ = read_only ? L"只读核对上次代理" : L"核对并应用本次代理选择";
    proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_;
    proxy_started_ = GetTickCount64(); proxy_timed_ = false;
    timings_.Mark(action == ProxyAction::Acgpower ? "proxy-acgpower-started" :
        action == ProxyAction::Initialize ? "proxy-initialization-started" : "proxy-other-selection-started");
    proxy_ = std::make_unique<ProxyInitialization>(++operation_, 3, 1, std::to_string(document_generation_),
        "webview2-candidate", GetTickCount64(), action);
    const auto url = pages_[0]->url();
    const std::string expected = "\"" + url + "\"";
    std::string script = std::string(kProxyProfilesScript) + "(" + expected + ");" + kProxyBridge + "(" + std::to_string(operation_) + ',' + expected + ");" +
        kProxyScript + "(" + std::to_string(operation_) + ',' + expected + ',' + std::string(InitialProxyOptions()) +
        ",\"" + std::string(ProxyActionName(action)) + "\"," + (read_only ? "true" : "false") +
        ",\"" + gbrowser::webview::ProxyProfileHex(proxy_custom_name_) + "\"," +
        ((!read_only && action != ProxyAction::Initialize) ? "true" : "false") + ");";

    Check(views_[2].core->ExecuteScript(Utf8ToWide(script).c_str(), nullptr));
    if (!SetTimer(window_, 2, 200, nullptr)) throw std::runtime_error("无法观察代理操作");
  }

  void PollProxy() {
    if (closing_ || failed_ || !proxy_ || polling_ || !controller_ready_) return;
    if (proxy_->Expire(GetTickCount64())) {
      timings_.Mark("proxy-operation-or-observation-expired", proxy_started_);
      if (!restore_probe_ && !proxy_uncertain_before_ && proxy_->state() == ProxyInitializationState::PreflightUnavailable)
        proxy_preference_->NoWrite();
      proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_; KillTimer(window_, 2); return;
    }
    if (!proxy_->pending() && !proxy_->observing()) { KillTimer(window_, 2); return; }
    const auto operation = operation_;
    const auto generation = document_generation_;
    polling_ = true; const auto callback = callbacks_.Track();
    const auto result = views_[2].core->ExecuteScript(L"window.__gbrowserProxyTake ? window.__gbrowserProxyTake() : null",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, callback, operation, generation](HRESULT error, LPCWSTR value) -> HRESULT {
              if (!callback.Complete()) return S_OK; polling_ = false;
              Guard([&] {
                if (closing_ || failed_ || !proxy_ || operation != operation_ || generation != document_generation_) return;
                Check(error);
                const auto source = WideToUtf8(ComText([&](LPWSTR* text) { return views_[2].core->get_Source(text); }));
                if (!pages_[0]->AllowsPage(source)) { proxy_->Invalidate(3); proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_; return; }
                const std::wstring encoded = value ? value : L"null";
                if (encoded == L"null") return;
                // The private queue emits only decimal IDs and a bounded ASCII
                // operation verb. Reject escaped JSON, content or extra fields.
                if (encoded.size() < 5 || encoded.size() > 64 || encoded.front() != L'"' || encoded.back() != L'"')
                  throw std::runtime_error("代理回复格式异常");
                const auto body = WideToUtf8(encoded.substr(1, encoded.size() - 2));
                const auto split = body.find('|');
                if (split == std::string::npos || split == 0 || split > 10 ||
                    body.find_first_not_of("0123456789", 0) != split ||
                    body.find_first_not_of("0123456789:abcdefghijklmnopqrstuvwxyz", split + 1) != std::string::npos)
                  throw std::runtime_error("代理回复字段异常");
                const auto reply = proxy_->Query(body.substr(split + 1), 3, 1, std::to_string(generation),
                    "webview2-candidate", GetTickCount64());
                if (reply == ProxyInitializationReply::Authorize &&
                    proxy_->state() == ProxyInitializationState::Running && !restore_probe_ &&
                    proxy_->action() != ProxyAction::Initialize)
                  proxy_preference_->BeginWrite(proxy_explicit_selection_); // A new manual intent stays uncertain until readback matches.
                const char* answer = reply == ProxyInitializationReply::Reject ? "denied" :
                    reply == ProxyInitializationReply::Authorize ? "authorized" : "received";
                const auto feedback = gbrowser::webview::ProxyFeedback(*proxy_, restore_probe_, restore_fallback_.has_value());
                // Persist the verified choice before acknowledging it to JS.
                // Only that acknowledgement permits updating ZeroOmega startup.
                if (feedback == gbrowser::webview::ProxyUiResult::Matched && !proxy_recorded_ &&
                    proxy_->action() != ProxyAction::Initialize) {
                  proxy_preference_->Matched(proxy_->action(),proxy_custom_name_); proxy_recorded_ = true;
                }
                const auto response = "window.__gbrowserProxyReply(" + body.substr(0, split) + ",\"" + answer + "\")";
                Check(views_[2].core->ExecuteScript(Utf8ToWide(response).c_str(), nullptr));
                proxy_verified_ = proxy_->observing();
                if (feedback == gbrowser::webview::ProxyUiResult::Matched) {
                  const bool reload_pages = !restore_probe_ && proxy_->action() != ProxyAction::Initialize && !proxy_timed_;
                  if (!proxy_timed_) {
                    timings_.Mark("proxy-settings-readback-matched", proxy_started_); proxy_timed_ = true;
                    phase_ = L"游戏运行";

                  }
                  if (proxy_verified_ && !SetTimer(window_, 2, 2000, nullptr)) throw std::runtime_error("无法保持代理观察");
                  // Actual selections refresh once, including startup restoration.
                  // Read-only probes and observation heartbeats never reload.
                  if (reload_pages) ReloadPagesAfterProxySelection();
                } else if (feedback != gbrowser::webview::ProxyUiResult::Waiting) {
                  const auto state = proxy_->state();
                  if (!restore_probe_ && !proxy_uncertain_before_ && (state == ProxyInitializationState::PreflightDifferent ||
                      state == ProxyInitializationState::PreflightUnavailable)) proxy_preference_->NoWrite();
                  if (feedback == gbrowser::webview::ProxyUiResult::RestoreSaved) {
                    const auto action = *restore_fallback_; restore_fallback_.reset();
                    // Post after the read-only reply. One generation-bound
                    // restoration, never an interval retry or default reset.
                    if (!PostMessageW(window_, kRestoreProxy, static_cast<WPARAM>(operation_), static_cast<LPARAM>(action)))
                      throw std::runtime_error("无法接续上次代理选择");
                    timings_.Mark("proxy-saved-choice-restore-queued", proxy_started_);
                    proxy_timed_ = true;
                  } else {
                    if (!proxy_timed_) {
                      timings_.Mark("proxy-operation-unmatched", proxy_started_);
                      timings_.Value("proxy-terminal-state", static_cast<int>(state));
                      RecordProxyDiagnostic(operation, generation);
                      proxy_timed_ = true;
                    }
                  }
                }
              }); Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); polling_ = false; Check(result); }
  }
  void ReloadPagesAfterProxySelection() {
    // Only the first successful readback after applying a selection reaches here.
    // Refresh the visible pages in place; never reload the proxy controller,
    // helper panels or an active authentication flow. Reload failure is a page
    // failure, not a reason to discard a successfully verified proxy choice.
    if (closing_ || failed_ || !proxy_verified_ || login_window_) return;
    for (const size_t index : {active_, kLeftGame}) {
      if (index == kLeftGame && !panes_.dual) continue;
      if (!gbrowser::webview::GameSurface(index) || settings_tabs_[index] || !views_[index].core) continue;
      LPWSTR source = nullptr;
      const auto result = views_[index].core->get_Source(&source);
      const auto url = source ? WideToUtf8(source) : std::string{};
      CoTaskMemFree(source);
      if (FAILED(result) || !IsBrowserNetworkUrl(url) || IsGbfLoginUrl(url)) continue;
      const auto reloaded = views_[index].core->Reload();
      timings_.Mark(SUCCEEDED(reloaded) ? "proxy-selected-page-reload" : "proxy-selected-page-reload-failed");
    }
  }
  void RecordProxyDiagnostic(uint64_t operation, uint64_t generation) {
    const bool initialize = proxy_ && proxy_->action() == ProxyAction::Initialize;
    const auto script = initialize ? std::wstring(L"window.__gbrowserProxyInitDiagnostic || 0") :
        L"window.__gbrowserProxySelectionDiagnostic && window.__gbrowserProxySelectionDiagnostic.operation === " +
        std::to_wstring(operation) + L" ? window.__gbrowserProxySelectionDiagnostic.category : 0";
    const auto callback = callbacks_.Track();
    const auto result = views_[2].core->ExecuteScript(script.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, callback, operation, generation, initialize](HRESULT error, LPCWSTR value) -> HRESULT {
              if (!callback.Complete()) return S_OK;
              if (SUCCEEDED(error) && !closing_ && operation == operation_ && generation == document_generation_ && value) {
                const std::wstring_view code(value);
                // Accept a fixed numeric category only, never an error string,
                // option key, proxy destination, credential or page contents.
                int category = -1;
                if (code.size() == 1 && code[0] >= L'0' && code[0] <= L'6') category = code[0] - L'0';
                if (code == L"10") category = 10;
                if (code == L"11") category = 11;
                if (!initialize && code.size() == 2 && (code[0] == L'1' || code[0] == L'2') && code[1] >= L'0' && code[1] <= L'9')
                  category = (code[0] - L'0') * 10 + code[1] - L'0';
                if (category >= 0) timings_.Value(initialize ? "proxy-initialization-diagnostic" : "proxy-selection-diagnostic", category);
              }
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); Drain(); }
  }
  void SavePanes() noexcept {
    if (dual_transition_.active()) return;
    try {
      auto stored = panes_;
      stored.group_percent = panel_percent_;
      if (arcarum_detached_) stored.arcarum = false; // Never restore a detached helper.
      pane_file_->Save(gbrowser::webview::EncodePaneState(stored));
    } catch (...) { Notice(L"面板选择未能保存；原有设置保留"); }
  }
  void DestroyArcarumWindow() noexcept {
    const auto old = arcarum_window_; arcarum_window_ = nullptr; arcarum_dock_button_ = nullptr;
    gbrowser::webview::DestroyPaneWindow(old);
  }
  gbrowser::webview::PaneLayout PaneGeometry() const {
    RECT rect{}; GetClientRect(window_, &rect);
    return gbrowser::webview::ComputePaneLayout(rect.right, rect.bottom,
        Dpi(), panel_percent_, panes_, arcarum_detached_, browser_layout_);
  }
  RECT ResizeWorkArea(RECT outer, RECT work) const {
    RECT visible{};
    if (FAILED(DwmGetWindowAttribute(window_, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible))) ||
        visible.right <= visible.left || visible.left < outer.left || visible.right > outer.right) {
      const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, Dpi()) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi());
      visible = outer;
      visible.left += border; visible.right -= border;
    }
    return gbrowser::webview::OuterResizeWorkArea(outer, visible, work);
  }
  int MeasuredSecondaryWidth(size_t preferred) const {
    if (menu_css_[preferred] > 0 && views_[preferred].controller) {
      double zoom = 1; Check(views_[preferred].controller->get_ZoomFactor(&zoom));
      return static_cast<int>(menu_css_[preferred] * zoom * Dpi() / 96.0 + 0.5);
    }
    if (last_secondary_menu_width_ > 0)
      return MulDiv(last_secondary_menu_width_, static_cast<int>(Dpi()), static_cast<int>(last_secondary_menu_dpi_));
    return MulDiv(panes_.secondary_menu_width, static_cast<int>(Dpi()), static_cast<int>(panes_.secondary_menu_dpi));
  }
  void LayoutGameView(size_t index) {
    // Display-adapter messages are local to this renderer, and cannot move the
    // outer window, primary frame, helpers or chrome. Defer during the commit.
    if (dual_transition_.active() || !views_[index].controller ||
        !(index == active_ || (index == kLeftGame && panes_.dual))) return;
    const auto geometry = PaneGeometry();
    const auto game = index == kLeftGame ? geometry.left : geometry.right;
    const int cropped = !panes_.moba_visible ? MobaPixels(index) : 0;
    views_[index].Bounds(gbrowser::webview::GameViewBounds(
        {game.x, game.y, geometry.right.width, game.height}, cropped));
  }
  void CommitDualLayout() {
    if (!dual_transition_.active() || closing_ || failed_) return;
    const auto generation = dual_transition_.generation();
    const bool committed = dual_transition_.Commit(generation, [&](const auto& plan) {
      dual_positioning_ = true;
      struct Reset { bool& value; ~Reset() { value = false; } } reset{dual_positioning_};
      // Exactly one outer resize. Suppress its recursive WM_SIZE layout, then
      // place all native containers once before allowing secondary creation.
      const bool moved = SetWindowPos(window_, nullptr, plan.to.left, plan.to.top,
          plan.to.right - plan.to.left, plan.to.bottom - plan.to.top,
          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS) != FALSE;
      if (!moved) {
        panes_ = dual_previous_panes_; panes_.dual = false;
        CloseLeftGame(); single_window_.reset();
        Notice(L"窗口尺寸调整未完成，已保留单窗");
      }
      if (closing_ || failed_) return false;
      Layout(); PaintChromeNow();
      return moved;
    });
    if (closing_ || failed_) return;
    SaveLayout(); SavePanes(); RefreshCooldown();
    if (committed) EnsurePaneView(kLeftGame);
  }
  void ChangeDualLayout() {
    single_window_.reset();
  }
  void ReconcileDisplay(RECT proposed) {
    dual_transition_.Cancel(); single_window_.reset();
    if (IsIconic(window_)) return;
    CancelSplit();
    RECT outer{}; GetWindowRect(window_, &outer);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST), &monitor) && !IsZoomed(window_)) {
      const auto work = ResizeWorkArea(outer, monitor.rcWork);
      if (panes_.dual && MinimumWindowWidth() > work.right - work.left) {
        panes_.dual = false; CloseLeftGame();
        Notice(L"当前屏幕无法容纳双窗，已保留单窗");
      }
      const int width = (std::min)(static_cast<int>(work.right - work.left),
          (std::max)(MinimumWindowWidth(), static_cast<int>(proposed.right - proposed.left)));
      proposed.left = (std::clamp)(proposed.left, work.left, work.right - width);
      proposed.right = proposed.left + width;
    }
    dual_positioning_ = true;
    SetWindowPos(window_, nullptr, proposed.left, proposed.top, proposed.right - proposed.left,
        proposed.bottom - proposed.top, SWP_NOZORDER | SWP_NOACTIVATE);
    dual_positioning_ = false;
    UpdateFont(); Layout(); RefreshCooldown(); SaveLayout(); SavePanes(); EnsurePaneView(kLeftGame);
  }
  void SecondaryCreationFailed() {
    // A secondary creation/navigation failure must not stop the primary game.
    CloseLeftGame();
    if (panes_.dual && !closing_ && !failed_) ToggleDual();
    Notice(L"副窗口创建未完成，已返回单窗；主游戏保持运行");
  }
  int MobaPixels(size_t index) const {
    if (!moba_css_[index] || !views_[index].controller) return 0;
    double zoom = 1.0; Check(views_[index].controller->get_ZoomFactor(&zoom));
    return static_cast<int>(moba_css_[index] * zoom * Dpi() / 96.0 + 0.5);
  }
  std::wstring GameSource(size_t index) const {
    return views_[index].core ? ComText([&](LPWSTR* value) { return views_[index].core->get_Source(value); }) : L"";
  }
  void CopyPrimaryToSecondary() {
    // One user action, one native dispatch. No source observers, timer, queue
    // or persisted switch can cause a later navigation or refresh.
    if (!dual_transition_.CanCreate(panes_.dual) || !BrowsingAvailable() || !bound_game_id_) return;
    if (!views_[active_].core || !views_[kLeftGame].core) { Notice(L"请等待两个游戏窗口就绪，再点击箭头"); return; }
    const auto source = GameSource(active_);
    if (!Document(WideToUtf8(source))) return;
    switch (gbrowser::webview::PlanGameCopy(source, GameSource(kLeftGame))) {
      case gbrowser::webview::GameCopy::Reload: Check(views_[kLeftGame].core->Reload()); break;
      case gbrowser::webview::GameCopy::Navigate: Check(views_[kLeftGame].core->Navigate(source.c_str())); break;
      default: Notice(L"主窗口当前不是可复制的游戏页面"); break;
    }
  }
  void ChangeWindowWidth(int delta) {
    ChangeDualLayout();
    // Helper/Moba visibility changes resize the right edge. Saved game
    // width stays independent of the space available to docked helpers.
    if (IsZoomed(window_)) { Layout(); SaveLayout(); return; }
    RECT outer{}; GetWindowRect(window_, &outer);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const int minimum = UiPixels(browser_layout_ ? 600 : 400, Dpi());
    const int maximum = (std::max)(minimum, static_cast<int>(monitor.rcWork.right - outer.left));
    const int width = (std::clamp)(static_cast<int>(outer.right - outer.left) + delta, minimum, maximum);
    SetWindowPos(window_, nullptr, outer.left, outer.top, width, outer.bottom - outer.top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout(); SaveLayout();
  }
  void CloseLeftGame() {
    ++view_generations_[kLeftGame]; // Invalidate callbacks without releasing pending creation.
    capture_observers_[kLeftGame].reset(); capture_game_[kLeftGame] = false;
    skill_snapshots_[kLeftGame] = gbrowser::webview::cooldown::Snapshot{}; skill_routes_[kLeftGame].clear();
    if (views_[kLeftGame].controller) Check(views_[kLeftGame].controller->Close());
    views_[kLeftGame] = {};
    left_requested_source_.clear();
  }
  void EnsurePaneView(size_t index) {
    const bool desired = index == kLeftGame ? panes_.dual : panes_.arcarum;
    if (!BrowsingAvailable() || !bound_game_id_ || !desired) return;
    if (index == kLeftGame && !dual_transition_.CanCreate(panes_.dual)) return;
    const auto source = index == kLeftGame && !left_requested_source_.empty() ? left_requested_source_ :
        ComText([&](LPWSTR* text) { return views_[0].core->get_Source(text); });
    if (!IsGbfPageUrl(WideToUtf8(source))) return;
    // A remembered double-pane setting must never clone a transient
    // authentication callback URL. Only the actual game hash route is copied.
    if (index == kLeftGame && source.find(L"/#") == std::wstring::npos) return;
    if (views_[index].core) {
      if (index == kArcarumView && ComText([&](LPWSTR* text) { return views_[index].core->get_Source(text); }) == L"about:blank")
        Check(views_[index].core->Navigate(Utf8ToWide(pages_[2]->url()).c_str()));
      if (index == kLeftGame) LayoutGameView(index); else Layout();
      return;
    }
    if (pane_pending_[index - kLeftGame]) return;
    const auto generation = ++view_generations_[index];
    pane_pending_[index - kLeftGame] = true; ++pending_;
    const auto parent = index == kLeftGame ? left_frame_ : arcarum_frame_;
    const auto result = environment_->CreateCoreWebView2Controller(parent,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, index, generation, source](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
              --pending_; pane_pending_[index - kLeftGame] = false;
              const bool desired_now = index == kLeftGame ? panes_.dual : panes_.arcarum;
              if (!BrowsingAvailable() || !desired_now || view_generations_[index] != generation ||
                  (index == kLeftGame && !dual_transition_.CanCreate(panes_.dual))) {
                if (controller) controller->Close();
                Guard([&] { EnsurePaneView(index); }); Drain(); return S_OK;
              }
              const auto configure = [&] {
                Check(error); if (!controller) throw std::runtime_error("便利视图创建失败");
                views_[index].controller = controller;
                Check(controller->get_CoreWebView2(&views_[index].core));
                if (index == kLeftGame) {
                  double zoom = 1; Check(views_[0].controller->get_ZoomFactor(&zoom));
                  Check(controller->put_ZoomFactor(zoom));
                }
                Configure(index); Layout();
                Check(views_[index].core->Navigate(index == kLeftGame ? source.c_str() : Utf8ToWide(pages_[2]->url()).c_str()));
              };
              if (index == kLeftGame) {
                try { configure(); }
                catch (...) {
                  if (controller && !views_[index].controller) controller->Close();
                  Guard([&] { SecondaryCreationFailed(); });
                }
              } else Guard(configure);
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) {
      --pending_; pane_pending_[index - kLeftGame] = false;
      if (index == kLeftGame) SecondaryCreationFailed(); else Check(result);
    }
  }
  void PinArcarumTarget() {
    if (!BrowsingAvailable() || !bound_game_id_ || !panes_.arcarum || !views_[kArcarumView].core) return;
    const auto source = WideToUtf8(ComText([&](LPWSTR* text) { return views_[kArcarumView].core->get_Source(text); }));
    if (!pages_[2]->AllowsPage(source)) return;
    // Explicit original API with the already verified right game. Initial
    // listener setup is once per real right/panel document, not timer replay.
    const auto epoch = std::make_pair(game_source_epoch_, navigation_epochs_[kArcarumView]);
    if (arcarum_pin_epoch_ == epoch) return;
    arcarum_pin_epoch_ = epoch;
    const auto script = std::string(kArcarumBindingScript) + '(' + std::to_string(bound_game_id_) +
        ",\"" + pages_[2]->url() + "\");";
    Check(views_[kArcarumView].core->ExecuteScript(Utf8ToWide(script).c_str(), nullptr));
  }
  void UpdateSecondaryMenu(bool hidden, int width) {
    // Only an explicit menu action adopts a new measured crop. Page reloads
    // update the measurement cache, never the current reserved pane width.
    if (IsZoomed(window_) || IsIconic(window_)) return;
    ChangeDualLayout();
    if (panes_.secondary_menu_hidden == hidden && panes_.secondary_menu_width == width && panes_.secondary_menu_dpi == Dpi()) return;
    CancelSplit();
    const auto before = PaneGeometry();
    const auto previous = panes_;
    panes_.secondary_menu_hidden = hidden;
    panes_.secondary_menu_width = width; panes_.secondary_menu_dpi = Dpi();
    if (panes_.dual && !IsZoomed(window_)) {
      const int change = gbrowser::webview::SecondaryMenuCut(previous, Dpi(), before.right.width) -
          gbrowser::webview::SecondaryMenuCut(panes_, Dpi(), before.right.width);
      RECT outer{}; GetWindowRect(window_, &outer);
      MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
      if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) { panes_ = previous; return; }
      const auto placement = gbrowser::webview::PrimaryAnchoredResize(outer, change, change,
          ResizeWorkArea(outer, monitor.rcWork), UiPixels(browser_layout_ ? 600 : 400, Dpi()));
      if (!placement) { panes_ = previous; Notice(L"当前屏幕宽度不足，副窗口侧栏设置保持不变"); return; }
      SetWindowPos(window_, nullptr, placement->left, placement->top, placement->right - placement->left,
          placement->bottom - placement->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (views_[kLeftGame].core) Check(views_[kLeftGame].core->ExecuteScript(
        Utf8ToWide(std::string(kGameRoleScript) + (hidden ? "(true,true);" : "(true,false);")).c_str(), nullptr));
    Layout(); SaveLayout(); SavePanes();
  }
  void ToggleDual() {
    if (dual_transition_.active() || closing_ || failed_) return;
    CancelSplit();
    if (IsZoomed(window_) || IsIconic(window_)) { Notice(L"请先还原窗口，再切换双窗"); return; }
    const auto before = PaneGeometry();
    RECT outer{}, client{}; GetWindowRect(window_, &outer); GetClientRect(window_, &client);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const auto previous = panes_;
    panes_.dual = !panes_.dual;
    SetGameWidth(before.right.width);
    if (panes_.dual) {
      panes_.secondary_menu_width = MeasuredSecondaryWidth(active_);
      panes_.secondary_menu_dpi = Dpi();
    }
    const int change = panes_.dual ? before.right.width -
        gbrowser::webview::SecondaryMenuCut(panes_, Dpi(), before.right.width) + UiPixels(6, Dpi()) :
        -(before.left.width + UiPixels(6, Dpi()));
    auto next = gbrowser::webview::ComputePaneLayout(client.right + change,
        client.bottom, Dpi(), panel_percent_, panes_, arcarum_detached_, browser_layout_);
    auto placement = gbrowser::webview::PrimaryAnchoredResize(outer, change,
        next.right.x - before.right.x, ResizeWorkArea(outer, monitor.rcWork), UiPixels(browser_layout_ ? 600 : 400, Dpi()));
    if (!placement) {
      panes_ = previous;
      Notice(L"当前屏幕工作区宽度不足以容纳双窗；请先调整游戏或助手宽度"); return;
    }
    if (!panes_.dual && single_window_) placement = *single_window_;
    next = gbrowser::webview::ComputePaneLayout(client.right +
        (placement->right - placement->left) - (outer.right - outer.left), client.bottom,
        Dpi(), panel_percent_, panes_, arcarum_detached_, browser_layout_);
    if (panes_.dual) single_window_ = outer;
    if (panes_.dual) {
      const auto target = panes_;
      panes_ = previous; // Selecting the permanent tab must still use single geometry.
      if (views_[active_].core) {
        const auto current = ComText([&](LPWSTR* text) { return views_[active_].core->get_Source(text); });
        if (IsGbfPageUrl(WideToUtf8(current)) && current.find(L"/#") != std::wstring::npos) {
          left_requested_source_ = current; // Actual URL, never the address draft.
          if (active_ != 0) Check(views_[0].core->Navigate(current.c_str()));
        }
      }
      SelectTab(0); // The verified permanent right ID continues to serve Tarou.
      panes_ = target;
    }
    dual_previous_panes_ = previous;
    dual_transition_.Begin({outer, *placement, before, next, panes_.dual});
    KillTimer(window_, 4); // Persist only after the complete layout commit.
    if (!panes_.dual) {
      // Unload at the existing outer width; shrink only after Close returns.
      ShowWindow(left_frame_, SW_HIDE); ShowWindow(buttons_[8], SW_HIDE);
      CloseLeftGame(); single_window_.reset(); RefreshCooldown();
    }
    CommitDualLayout();
  }
  void ResizeTarouDock(const gbrowser::webview::PaneLayout& before, bool was_docked) {
    const bool docked = panes_.tarou && !panes_.tarou_detached;
    if (was_docked == docked) return;
    if (was_docked) { panes_.tarou_width = before.tarou.width; panes_.tarou_dpi = Dpi(); }
    const int remembered = panes_.tarou_width ? MulDiv(panes_.tarou_width,
        static_cast<int>(Dpi()), static_cast<int>(panes_.tarou_dpi)) : before.right.width;
    const int delta = gbrowser::webview::TarouWidthChange(before, was_docked, docked,
        remembered, panes_.tarou_share, Dpi());
    SetGameWidth(before.right.width);
    ChangeWindowWidth(delta);
  }
  void SetTarou(bool visible) {
    ChangeDualLayout();
    const auto before = PaneGeometry();
    const bool was_docked = panes_.tarou && !panes_.tarou_detached;
    if (!visible) {
      if (views_[1].controller) views_[1].Visible(FALSE);
      for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) ShowWindow(control, SW_HIDE);
    }
    if (!visible && tarou_window_) {
      SaveTarouPlacement();
      views_[1].Visible(FALSE);
      views_[1].Reparent(tarou_frame_);
      for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) SetParent(control, window_);
      DestroyTarouWindow(); // Visibility and the user's floating preference are independent.
    }
    CancelSplit(); panes_.tarou = visible; ResizeTarouDock(before, was_docked); Layout(); SavePanes();
    if (visible) BindGame();
  }
  void SaveTarouPlacement() noexcept {
    if (!tarou_window_) return;
    try {
      WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
      if (!GetWindowPlacement(tarou_window_, &placement)) throw std::runtime_error("无法读取太郎窗口位置");
      const auto& r = placement.rcNormalPosition;
      gbrowser::webview::WindowState state{r.left, r.top, r.right - r.left, r.bottom - r.top,
          GetDpiForWindow(tarou_window_), 50, IsZoomed(tarou_window_) != FALSE};
      tarou_window_file_->Save(gbrowser::webview::EncodeWindowState(state)); tarou_window_state_ = state;
    } catch (...) { Notice(L"太郎窗口位置未保存，原记录保留"); }
  }
  void DestroyTarouWindow() noexcept {
    const auto old = tarou_window_; tarou_window_ = nullptr;
    gbrowser::webview::DestroyPaneWindow(old);
  }
  void EnsureTarouWindow() {
    if (!panes_.tarou || !panes_.tarou_detached || tarou_window_ || !views_[1].controller || closing_) return;
    tarou_window_ = CreateWindowExW(0, L"GbrowserTarouPane", L"StarryFish · Tarou", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 800, window_, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)), this);
    if (!tarou_window_) throw std::runtime_error("无法创建太郎独立窗口");
    views_[1].Visible(FALSE);
    views_[1].Reparent(tarou_window_);
    for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) SetParent(control, tarou_window_);
    if (tarou_window_file_->bytes()) {
      const auto& saved = tarou_window_state_;
      WINDOWPLACEMENT placement{}; placement.length = sizeof(placement); placement.showCmd = SW_SHOWNORMAL;
      placement.rcNormalPosition = {saved.x, saved.y, saved.x + saved.width, saved.y + saved.height};
      if (!SetWindowPlacement(tarou_window_, &placement) || !GetWindowPlacement(tarou_window_, &placement))
        throw std::runtime_error("无法恢复太郎窗口位置");
      MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
      if (!GetMonitorInfoW(MonitorFromWindow(tarou_window_, MONITOR_DEFAULTTONEAREST), &monitor))
        throw std::runtime_error("无法核对太郎窗口屏幕");
      const auto dpi = GetDpiForWindow(tarou_window_);
      const auto width = (std::min)(static_cast<int>(monitor.rcWork.right - monitor.rcWork.left),
          (std::max)(UiPixels(400, dpi), MulDiv(saved.width, static_cast<int>(dpi), static_cast<int>(saved.dpi))));
      const auto height = (std::min)(static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top),
          (std::max)(UiPixels(300, dpi), MulDiv(saved.height, static_cast<int>(dpi), static_cast<int>(saved.dpi))));
      placement.rcNormalPosition = gbrowser::webview::VisiblePlacement(placement.rcNormalPosition,
          monitor.rcMonitor, monitor.rcWork, width, height);
      placement.showCmd = saved.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
      if (!SetWindowPlacement(tarou_window_, &placement)) throw std::runtime_error("无法恢复太郎窗口大小");
    } else ShowWindow(tarou_window_, SW_SHOWNORMAL);
  }
  void DetachTarou() {
    if (!panes_.tarou || !views_[1].controller) return;
    ChangeDualLayout();
    const auto before = PaneGeometry();
    const bool was_docked = !panes_.tarou_detached;
    CancelSplit();
    if (panes_.tarou_detached) {
      SaveTarouPlacement();
      views_[1].Visible(FALSE);
      views_[1].Reparent(tarou_frame_);
      for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) SetParent(control, window_);
      panes_.tarou_detached = false; DestroyTarouWindow();
    } else { panes_.tarou_detached = true; EnsureTarouWindow(); }
    ResizeTarouDock(before, was_docked); Layout(); SavePanes();
  }
  void LayoutTarouHeader(Rect panel, unsigned dpi) {
    const auto header = gbrowser::webview::HeaderGeometry(panel, dpi);
    // The title must never paint underneath its sibling action buttons.
    PositionControl(tarou_title_, header.title);
    PositionControl(buttons_[10], header.close);
    PositionControl(buttons_[11], header.pin);
    for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) {
      SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      ShowWindow(control, panes_.tarou ? SW_SHOWNA : SW_HIDE);
      InvalidateRect(control, nullptr, FALSE);
    }
  }
  void LayoutTarouWindow() {
    if (!tarou_window_ || !views_[1].controller) return;
    RECT rect{}; GetClientRect(tarou_window_, &rect); const auto dpi = GetDpiForWindow(tarou_window_);
    LayoutTarouHeader({0, 0, rect.right, rect.bottom}, dpi);
    rect.top = (std::min)(rect.bottom, static_cast<LONG>(UiPixels(40, dpi)));
    views_[1].Bounds(rect);
    views_[1].Visible(panes_.tarou && !closing_ && !IsIconic(tarou_window_));
  }
  void SetArcarum(bool visible) {
    ChangeDualLayout();
    const auto geometry = PaneGeometry();
    const int width = panes_.arcarum && !arcarum_detached_ ? geometry.arcarum.width :
        geometry.tarou.width > 0 ? static_cast<int>(static_cast<int64_t>(geometry.tarou.width) *
            (10000 - panes_.tarou_share) / panes_.tarou_share) + UiPixels(6, Dpi()) : geometry.right.width;
    const bool was_docked = panes_.arcarum && !arcarum_detached_;
    panes_.arcarum = visible;
    if (!visible && arcarum_window_) {
      if (views_[kArcarumView].controller) {
        views_[kArcarumView].Visible(FALSE);
        Check(views_[kArcarumView].controller->put_ParentWindow(arcarum_frame_));
      }
      arcarum_detached_ = false; DestroyArcarumWindow();
    }
    if (was_docked != (visible && !arcarum_detached_)) {
      SetGameWidth(geometry.right.width);
      ChangeWindowWidth(visible ? width : -width);
    }
    Layout(); SavePanes(); if (visible) EnsurePaneView(kArcarumView);
  }
  void DetachArcarum() {
    if (!panes_.arcarum || !views_[kArcarumView].controller) return;
    ChangeDualLayout();
    if (arcarum_detached_) {
      const auto geometry = PaneGeometry();
      views_[kArcarumView].Visible(FALSE);
      Check(views_[kArcarumView].controller->put_ParentWindow(arcarum_frame_));
      arcarum_detached_ = false; DestroyArcarumWindow();
      const int width = geometry.tarou.width > 0 ? static_cast<int>(static_cast<int64_t>(geometry.tarou.width) *
          (10000 - panes_.tarou_share) / panes_.tarou_share) + UiPixels(6, Dpi()) : geometry.right.width;
      SetGameWidth(geometry.right.width);
      ChangeWindowWidth(width);
    } else {
      const auto geometry = PaneGeometry();
      const auto width = geometry.arcarum.width + geometry.helper_divider.width;
      arcarum_window_ = CreateWindowExW(0, L"GbrowserArcarumPane", L"StarryFish · 肉鸽",
          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, (std::max)(500, width), 800, window_, nullptr,
          reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)), this);
      if (!arcarum_window_) throw std::runtime_error("无法创建独立肉鸽窗口");
      arcarum_dock_button_ = CreateWindowW(L"BUTTON", L"停靠", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
          8, 4, 80, 28, arcarum_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDetach)),
          reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)), nullptr);
      gbrowser::webview::UiTheme::AttachButton(arcarum_dock_button_);
      views_[kArcarumView].Visible(FALSE);
      Check(views_[kArcarumView].controller->put_ParentWindow(arcarum_window_));
      arcarum_detached_ = true;
      SetGameWidth(geometry.right.width);
      ChangeWindowWidth(-width);
      ShowWindow(arcarum_window_, SW_SHOWNORMAL);
    }
    Layout(); SavePanes();
  }
  void LayoutArcarumWindow() {
    if (!arcarum_window_ || !views_[kArcarumView].controller) return;
    RECT rect{}; GetClientRect(arcarum_window_, &rect);
    const auto dpi = GetDpiForWindow(arcarum_window_);
    rect.top = (std::min)(rect.bottom, static_cast<LONG>(UiPixels(36, dpi)));
    if (arcarum_dock_button_) MoveWindow(arcarum_dock_button_, UiPixels(8, dpi), UiPixels(4, dpi),
        UiPixels(80, dpi), UiPixels(28, dpi), TRUE);
    views_[kArcarumView].Bounds(rect);
    views_[kArcarumView].Visible(panes_.arcarum && !closing_ && !IsIconic(arcarum_window_));
  }
  bool CurrentView(size_t index, uint64_t generation) const {
    return index < views_.size() && view_generations_[index] == generation && views_[index].core;
  }
  void RequestTabFavicon(size_t index, uint64_t generation) {
    if (closing_ || !CurrentView(index, generation) || favicon_loading_[index] ||
        std::find(tab_order_.begin(), tab_order_.end(), index) == tab_order_.end()) return;
    ComPtr<ICoreWebView2_15> core;
    if (FAILED(views_[index].core.As(&core))) return;
    const auto epoch = favicon_epochs_[index];
    // Coalesce icon changes to one request per slot. A reused slot never accepts
    // an old page's result; closing/profile switch revokes its callback ticket.
    favicon_loading_[index] = true; const auto callback = callbacks_.Track();
    const auto result = core->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
        Callback<ICoreWebView2GetFaviconCompletedHandler>(
            [this, callback, index, generation, epoch](HRESULT error, IStream* stream) -> HRESULT {
              if (!callback.Complete()) return S_OK; favicon_loading_[index] = false;
              if (!closing_ && CurrentView(index, generation) && favicon_epochs_[index] == epoch &&
                  std::find(tab_order_.begin(), tab_order_.end(), index) != tab_order_.end()) {
                try { theme_.SetTabIcon(index, SUCCEEDED(error) ? stream : nullptr); }
                catch (...) { theme_.ClearTabIcon(index); }
                InvalidateRect(tabs_, nullptr, FALSE);
              }
              if (!closing_ && (favicon_epochs_[index] != epoch || view_generations_[index] != generation))
                RequestTabFavicon(index, view_generations_[index]);
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) { callback.Complete(); favicon_loading_[index] = false; Drain(); }
  }
  void RebuildTabs() {
    if (!tabs_) return;
    SetWindowTextW(tabs_, titles_[active_].c_str());
    tooltip_tab_ = -1;
    if (tabs_tip_) SendMessageW(tabs_tip_, TTM_POP, 0, 0);
    // Publish the strip before Layout/MoveFocus/controller Close can occupy
    // this UI thread. Queued WM_PAINT otherwise leaves a deleted tab visible
    // after the adjacent WebView is already shown. Redraw only this control;
    // do not pump arbitrary messages or release a pending controller early.
    if (!RedrawWindow(tabs_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW))
      timings_.Mark("tab-strip-redraw-deferred"); // Paint failure must not stop game networking.
  }
  void FocusActive() {
    if (closing_ || !views_[active_].controller) return;
    const auto result = gbrowser::webview::FocusEnabledHost(window_, closing_, [&] {
      return views_[active_].controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    });
    // Cosmetic/input focus failure must not invalidate a verified proxy.
    if (FAILED(result)) timings_.Value("active-view-focus-unavailable", static_cast<int>(result));
  }
  void UpdateAddress() {
    if (!address_) return;
    updating_address_ = true;
    SendMessageW(address_, EM_SETREADONLY, settings_tabs_[active_], 0);
    auto text = drafts_[active_] ? draft_text_[active_] : views_[active_].core ?
        ComText([&](LPWSTR* value) { return views_[active_].core->get_Source(value); }) : L"about:blank";
    SetWindowTextW(address_, text.c_str());
    updating_address_ = false;
  }
  void SelectTab(size_t index, bool focus = true) {
    if (std::find(tab_order_.begin(), tab_order_.end(), index) == tab_order_.end()) return;
    if (dual_transition_.active()) return;
    active_ = index; RebuildTabs(); UpdateAddress(); Layout();
    if (focus) FocusActive();
  }
  void OpenSettingsTab() {
    // Settings close independently of the private observation/target-binding
    // controller. Never expose/reload it as a tab or reinitialize defaults.
    for (auto index : tab_order_) if (settings_tabs_[index]) { SelectTab(index); return; }
    NewTab(true);
  }
  bool OpenBookmark(size_t index, ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) {
    ComPtr<ICoreWebView2NewWindowRequestedEventArgs2> named;
    if (FAILED(args->QueryInterface(IID_PPV_ARGS(&named)))) return false;
    const auto name = ComText([&](LPWSTR* text) { return named->get_Name(text); });
    if (!gbrowser::webview::IsBookmarkWindowName(name)) return false;
    // Consume classified requests without falling through to login. A name
    // alone grants no capability, nor do helper/login/hidden controllers.
    const bool game_origin = index == kLeftGame && panes_.dual;
    if (!BrowsingAvailable() || !bound_game_id_ || (index != active_ && !game_origin) ||
        (index != 0 && !game_origin && (index < gbrowser::webview::TabSlots::kFirst || IsAuxiliary(index)))) return true;
    BOOL gesture = FALSE; Check(args->get_IsUserInitiated(&gesture));
    ComPtr<ICoreWebView2NewWindowRequestedEventArgs3> detail;
    ComPtr<ICoreWebView2FrameInfo> frame;
    ComPtr<ICoreWebView2FrameInfo2> frame_detail;
    if (FAILED(args->QueryInterface(IID_PPV_ARGS(&detail))) ||
        FAILED(detail->get_OriginalSourceFrameInfo(&frame)) || !frame || FAILED(frame.As(&frame_detail))) {
      Notice(L"书签来源无法核对；当前页面保留"); return true;
    }
    COREWEBVIEW2_FRAME_KIND kind{}; Check(frame_detail->get_FrameKind(&kind));
    const auto source = WideToUtf8(ComText([&](LPWSTR* text) { return sender->get_Source(text); }));
    const auto original = WideToUtf8(ComText([&](LPWSTR* text) { return frame->get_Source(text); }));
    const auto target = WideToUtf8(ComText([&](LPWSTR* text) { return args->get_Uri(text); }));
    if (original != source || !gbrowser::webview::IsBookmarkNavigation(mode_, source, target,
        gesture != FALSE, kind == COREWEBVIEW2_FRAME_KIND_MAIN_FRAME) || !Document(target)) {
      Notice(L"该书签新标签请求未接续；当前页面保留"); return true;
    }
    auto bookmark = std::make_shared<BookmarkRequest>(BookmarkRequest{
        std::make_shared<WindowRequest>(args), {index, view_generations_[index]}, navigation_epochs_[index], source});
    NewTab(false, std::move(bookmark)); return true;
  }
  bool BookmarkCurrent(const BookmarkRequest& request) const {
    return BrowsingAvailable() && bound_game_id_ &&
        CurrentView(request.origin.index, request.origin.generation) &&
        navigation_epochs_[request.origin.index] == request.navigation_epoch &&
        WideToUtf8(ComText([&](LPWSTR* text) { return views_[request.origin.index].core->get_Source(text); })) == request.source;
  }
  void NewTab(bool settings = false, std::shared_ptr<BookmarkRequest> bookmark = {}) {
    if (closing_ || failed_ || loaded_ != 4) return;
    const auto ticket = tab_slots_.Reserve();
    if (!ticket) { Notice(L"最多同时打开8个页面；请先关闭一个普通标签"); return; }
    const auto index = ticket->index;
    view_generations_[index] = ticket->generation;
    settings_tabs_[index] = settings;
    ++favicon_epochs_[index]; theme_.ClearTabIcon(index);
    titles_[index] = settings ? L"ZeroOmega设置" : L"新标签";
    drafts_[index] = false; draft_text_[index].clear();
    tab_order_.push_back(index);
    if (bookmark) RebuildTabs(); else SelectTab(index, false);
    ++pending_;
    const auto result = environment_->CreateCoreWebView2Controller(right_frame_,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, ticket = *ticket, bookmark](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
              --pending_;
              const bool visible = tab_slots_.Complete(ticket);
              bool wanted = false;
              Guard([&] { wanted = visible && !closing_ && !failed_ && (!bookmark || BookmarkCurrent(*bookmark)); });
              if (!wanted || FAILED(error) || !controller) {
                if (controller) controller->Close();
                tab_slots_.Release(ticket);
                if (!closing_) Guard([&] {
                  RemoveTabLabel(ticket.index);
                  if (!failed_) Notice(L"新标签未能创建；已有页面保留");
                });
                if (bookmark) Guard([&] { bookmark->window->Complete(); });
                Drain(); return S_OK;
              }
              Guard([&] {
                views_[ticket.index].controller = controller;
                Check(controller->get_CoreWebView2(&views_[ticket.index].core));
                Configure(ticket.index, bookmark != nullptr); Layout();
                if (bookmark) {
                  // Bind the runtime's real request before its initial
                  // navigation. Preserve opener and configure replacement
                  // web contents before completing the deferral.
                  Check(bookmark->window->args->put_NewWindow(views_[ticket.index].core.Get()));
                  ConfigureResources(ticket.index);
                } else Check(views_[ticket.index].core->Navigate(settings_tabs_[ticket.index] ?
                    Utf8ToWide(pages_[0]->url()).c_str() : L"about:blank"));
                if (!bookmark && active_ == ticket.index) {
                  UpdateAddress();
                  if (settings_tabs_[ticket.index]) FocusActive();
                  else { SetFocus(address_); SendMessageW(address_, EM_SETSEL, 0, -1); }
                }
              });
              if (bookmark) Guard([&] { bookmark->window->Complete(); });
              if (bookmark && !closing_ && !failed_ && active_ == ticket.index)
                Guard([&] { FocusActive(); }); // Only if the user selected this pending background tab.
              Drain(); return S_OK;
            }).Get());
    if (FAILED(result)) {
      --pending_; tab_slots_.Complete(*ticket); tab_slots_.Release(*ticket);
      RemoveTabLabel(index); Notice(L"新标签未能创建；已有页面保留");
    }
  }
  void RemoveTabLabel(size_t index) {
    const auto item = std::find(tab_order_.begin(), tab_order_.end(), index);
    if (item == tab_order_.end()) return;
    const auto position = static_cast<size_t>(item - tab_order_.begin());
    tab_order_.erase(item);
    ++favicon_epochs_[index]; theme_.ClearTabIcon(index);
    if (active_ == index) SelectTab(tab_order_[(std::min)(position, tab_order_.size() - 1)]);
    else RebuildTabs();
  }
  void CloseTab(size_t index, uint64_t generation) {
    if (closing_) return;
    if (IsAuxiliary(index)) { CloseAuxiliary(index, generation); return; }
    if (!index) { Notice(L"GBF主标签保留，用于游戏及助手绑定；关闭窗口可退出"); return; }
    if (index < gbrowser::webview::TabSlots::kFirst || index >= views_.size() || view_generations_[index] != generation) return;
    const gbrowser::webview::TabSlots::Ticket ticket{index, generation};
    if (!tab_slots_.Current(ticket)) return;
    capture_observers_[index].reset(); capture_game_[index] = false;
    skill_snapshots_[index] = gbrowser::webview::cooldown::Snapshot{}; skill_routes_[index].clear();
    RemoveTabLabel(index);
    if (tab_slots_.Cancel(ticket)) {
      const auto started = GetTickCount64();
      if (views_[index].controller) Check(views_[index].controller->Close());
      timings_.Mark("ordinary-tab-controller-close", started);
      views_[index].core.Reset(); views_[index].controller.Reset();
      tab_slots_.Release(ticket);
    }
    Layout();
  }
  void SubmitAddress() {
    if (closing_ || failed_ || !views_[active_].core || settings_tabs_[active_]) return;
    auto text = WindowText(address_);
    const auto first = text.find_first_not_of(L" ");
    if (first == std::wstring::npos) return;
    text = text.substr(first, text.find_last_not_of(L" ") - first + 1);
    if (text != L"about:blank" && text.find(L"://") == std::wstring::npos) text = L"https://" + text;
    const auto url = WideToUtf8(text);
    if ((url != "about:blank" && !IsBrowserNetworkUrl(url)) || !Document(url)) {
      Notice(L"此地址当前不可打开；网页地址需要使用HTTP或HTTPS"); return;
    }
    drafts_[active_] = false;
    Check(views_[active_].core->Navigate(text.c_str())); FocusActive();
  }
  static std::wstring WindowText(HWND control) {
    const auto size = GetWindowTextLengthW(control);
    if (size < 0 || size > 8192) throw std::runtime_error("地址输入超限");
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    const auto copied = GetWindowTextW(control, text.data(), size + 1);
    text.resize(static_cast<size_t>(copied)); return text;
  }
  static void PositionControl(HWND control, Rect area) {
    RECT previous{}; GetWindowRect(control, &previous);
    MapWindowPoints(nullptr, GetParent(control), reinterpret_cast<POINT*>(&previous), 2);
    if (previous.left != area.x || previous.top != area.y || previous.right - previous.left != area.width ||
        previous.bottom - previous.top != area.height)
      SetWindowPos(control, nullptr, area.x, area.y, area.width, area.height,
          SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
  }
  static void ShowControl(HWND control, bool visible) {
    if (((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) != visible)
      ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
  }
  void NotifyViewPositions() {
    for (auto& view : views_) view.NotifyPosition();
  }
  static void PositionControls(std::vector<std::pair<HWND, Rect>>& positions) {
    auto batch = BeginDeferWindowPos(static_cast<int>(positions.size()));
    for (const auto& [control, area] : positions) {
      if (!batch) break;
      RECT previous{}; GetWindowRect(control, &previous);
      MapWindowPoints(nullptr, GetParent(control), reinterpret_cast<POINT*>(&previous), 2);
      if (previous.left == area.x && previous.top == area.y && previous.right - previous.left == area.width &&
          previous.bottom - previous.top == area.height) continue;
      batch = DeferWindowPos(batch, control, nullptr, area.x, area.y, area.width, area.height,
          SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOREDRAW);
    }
    if (!batch || !EndDeferWindowPos(batch))
      for (const auto& [control, area] : positions) PositionControl(control, area);
    positions.clear();
  }
  void InvalidateChrome() const {
    // Sequential child moves can expose an old sibling's background. Repaint
    // every native control after placement instead of trusting copied pixels
    // or waiting for hover. Do not invalidate the game/helper renderers.
    for (const auto button : buttons_) if (button) InvalidateRect(button, nullptr, FALSE);
    for (const auto control : {tarou_title_, tabs_, address_, divider_, helper_divider_})
      if (control) InvalidateRect(control, nullptr, FALSE);
    if (window_) InvalidateRect(window_, nullptr, FALSE);
  }
  void PaintChromeNow() const {
    // Paint only host-owned chrome; never erase/invalidate renderer children.
    for (const auto button : buttons_) if (button) UpdateWindow(button);
    for (const auto control : {tarou_title_, tabs_, address_, divider_, helper_divider_, window_})
      if (control) UpdateWindow(control);
  }
  void Layout() {
    if (!window_ || !address_ || closing_ || layout_applying_) return;
    if (dual_transition_.active() && !dual_positioning_) return;
    layout_applying_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{layout_applying_};
    RECT rect{}; GetClientRect(window_, &rect);
    const int width = rect.right, height = rect.bottom;
    const auto px = [&](int value) { return UiPixels(value, Dpi()); };
    const auto geometry = PaneGeometry();
    const auto& layout = geometry.chrome;
    std::vector<std::pair<HWND, Rect>> positions;
    const auto move = [&](HWND control, Rect area) { positions.emplace_back(control, area); };
    auto top = gbrowser::webview::ToolbarGeometry(width, Dpi(), tab_order_.size());
    if (browser_layout_) {
      const auto browser = gbrowser::webview::BrowserToolbarGeometry(width, Dpi(), tab_order_.size());
      top = {browser.address, browser.copy, browser.tabs, browser.functions[6],
          browser.minimize, browser.maximize, browser.close, browser.add, browser.reload};
      const std::array<size_t, 8> actions{2, 3, 1, 17, 4, 6, 0, 7};
      for (size_t i = 0; i < actions.size(); ++i) move(buttons_[actions[i]], browser.functions[i]);
    }
    move(buttons_[15], top.reload); ShowControl(buttons_[15], true);
    move(buttons_[16], top.add); ShowControl(buttons_[16], true);
    auto address = top.address;
    address.x += px(7); address.y += px(4); address.width -= px(14); address.height -= px(8);
    move(address_, address); move(tabs_, top.tabs); move(buttons_[5], top.copy);
    move(buttons_[0], top.profile); move(buttons_[12], top.minimize);
    move(buttons_[13], top.maximize); move(buttons_[14], top.close);
    const int rail_x = layout.content.rail.x + px(4), rail_top = layout.content.rail.y;
    const std::array<size_t, 6> rail_buttons{6, 2, 1, 3, 17, 4};
    if (!browser_layout_) {
      for (size_t row = 0; row < rail_buttons.size(); ++row)
        move(buttons_[rail_buttons[row]], {rail_x, rail_top + px(4 + static_cast<int>(row) * 44), px(40), px(40)});
      move(buttons_[7], {rail_x, height - gbrowser::webview::LeftChromeInset(Dpi()) - px(44), px(40), px(40)});
    }
    move(divider_, geometry.divider);
    ShowControl(divider_, geometry.divider.width != 0);
    move(helper_divider_, geometry.helper_divider);
    ShowControl(helper_divider_, geometry.helper_divider.width != 0);
    move(right_frame_, geometry.right); ShowControl(right_frame_, true);
    const bool secondary_visible = panes_.dual;
    move(left_frame_, geometry.left); ShowControl(left_frame_, secondary_visible != 0);
    move(buttons_[8], {geometry.link.x, geometry.link.y + (std::max)(0, (geometry.link.height - px(40)) / 2),
        geometry.link.width, px(40)});
    ShowControl(buttons_[8], secondary_visible != 0);
    auto panel = geometry.tarou;
    const auto header = gbrowser::webview::HeaderGeometry(panel, Dpi());
    if (!panes_.tarou_detached) {
      move(tarou_title_, header.title); move(buttons_[10], header.close); move(buttons_[11], header.pin);
      for (const auto control : {tarou_title_, buttons_[10], buttons_[11]}) ShowControl(control, panes_.tarou);
    }
    panel.y += px(40); panel.height = (std::max)(0, panel.height - px(40));
    move(tarou_frame_, panel); ShowControl(tarou_frame_, panes_.tarou && !panes_.tarou_detached);
    auto arcarum = geometry.arcarum;
    move(buttons_[9], {arcarum.x + (std::max)(0, arcarum.width - px(64)), arcarum.y, px(60), px(28)});
    arcarum.y += px(30); arcarum.height = (std::max)(0, arcarum.height - px(30));
    move(arcarum_frame_, arcarum);
    ShowControl(arcarum_frame_, panes_.arcarum && !arcarum_detached_);
    ShowControl(buttons_[9], panes_.arcarum && !arcarum_detached_);
    PositionControls(positions);
    SetWindowPos(buttons_[8], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (size_t index = 0; index < views_.size(); ++index) {
      if (index == 1 || index == 3 || index == kArcarumView || IsAuxiliary(index) || !views_[index].controller) continue;
      const int cropped = !panes_.moba_visible && gbrowser::webview::GameSurface(index) ? MobaPixels(index) : 0;
      const auto game = index == kLeftGame ? geometry.left : geometry.right;
      const bool visible = index == active_ || (index == kLeftGame && secondary_visible);
      // Background tabs get their new bounds when selected, avoiding work for every hidden renderer during drag.
      if (visible || index == 2) views_[index].Bounds(index == 2 ? RECT{0, 0, px(800), px(600)} :
          gbrowser::webview::GameViewBounds(index == kLeftGame ?
              Rect{game.x, game.y, geometry.right.width, game.height} : game, cropped));
      views_[index].Visible(visible);
    }
    if (panes_.tarou_detached) { EnsureTarouWindow(); LayoutTarouWindow(); }
    else if (views_[1].controller) {
      views_[1].Bounds({0, 0, panel.width, panel.height});
      views_[1].Visible(panes_.tarou);
    }
    if (views_[kArcarumView].controller) {
      if (arcarum_detached_) LayoutArcarumWindow();
      else {
        views_[kArcarumView].Bounds({0, 0, arcarum.width, arcarum.height});
        views_[kArcarumView].Visible(panes_.arcarum);
      }
    }
    NotifyViewPositions(); InvalidateChrome();
  }
  unsigned Dpi() const { const auto value = GetDpiForWindow(window_); return value ? value : 96; }
  void SetGameWidth(int width) {
    panes_.game_width = (std::clamp)(width, 1, 32768); panes_.game_dpi = Dpi();
  }
  void SetHelperWidth(int width) {
    const auto geometry = PaneGeometry();
    const int room = geometry.tarou.width + geometry.arcarum.width;
    if (room > 0) panes_.tarou_share = static_cast<unsigned>((std::clamp)(
        (static_cast<int64_t>(width) * 10000 + room / 2) / room, int64_t{1}, int64_t{9999}));
  }
  int MinimumWindowWidth() const {
    const int minimum = UiPixels(browser_layout_ ? 600 : 400, Dpi());
    if (dual_transition_.active()) {
      const auto& plan = dual_transition_.plan();
      return (std::min)(plan.from.right - plan.from.left, plan.to.right - plan.to.left);
    }
    if (!panes_.game_width || !window_) return minimum;
    RECT client{}, outer{}; GetClientRect(window_, &client); GetWindowRect(window_, &outer);
    const auto geometry = PaneGeometry();
    const bool tarou = panes_.tarou && !panes_.tarou_detached;
    const bool arcarum = panes_.arcarum && !arcarum_detached_;
    const int helpers = (tarou || arcarum) ? UiPixels(tarou && arcarum ? 252 : 126, Dpi()) : 0;
    return (std::max)(minimum, gbrowser::webview::SavedGameWidth(panes_, Dpi()) *
        (panes_.dual ? 2 : 1) - gbrowser::webview::SecondaryMenuCut(panes_, Dpi(),
            gbrowser::webview::SavedGameWidth(panes_, Dpi())) + (panes_.dual ? UiPixels(6, Dpi()) : 0) + helpers +
        gbrowser::webview::LeftChromeInset(Dpi()) +
        static_cast<int>(client.right - geometry.chrome.content.rail.x + (outer.right - outer.left - client.right)));
  }
  void UpdateFont() {
    LOGFONTW description{}; description.lfHeight = -UiPixels(14, Dpi());
    description.lfWeight = FW_NORMAL;
    wcscpy_s(description.lfFaceName, L"Segoe UI");
    HFONT next = CreateFontIndirectW(&description);
    if (!next) throw std::runtime_error("无法创建界面字体");
    for (HWND control : {address_, tabs_, tarou_title_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
    for (HWND control : buttons_) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
    if (font_) DeleteObject(font_);
    font_ = next;
  }
  static Shortcut PageKey(UINT key) {
    if (key == VK_BROWSER_BACK) return Shortcut::Back;
    if (key == VK_BROWSER_FORWARD) return Shortcut::Forward;
    const auto action = MatchShortcut(static_cast<int>(key), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_MENU) & 0x8000) != 0);
    switch (action) {
      case Shortcut::Back: case Shortcut::Forward: case Shortcut::Reload:
      case Shortcut::Address: case Shortcut::NewTab: case Shortcut::CloseTab:
      case Shortcut::NextTab: case Shortcut::PreviousTab:
      case Shortcut::ZoomIn: case Shortcut::ZoomOut: case Shortcut::ZoomReset: return action;
      default: return Shortcut::None;
    }
  }
  void PageAction(Shortcut action) {
    if (closing_ || failed_) return;
    if (action == Shortcut::Address) { SetFocus(address_); SendMessageW(address_, EM_SETSEL, 0, -1); return; }
    if (action == Shortcut::NewTab) { NewTab(); return; }
    if (action == Shortcut::CloseTab) { CloseTab(active_, view_generations_[active_]); return; }
    if (action == Shortcut::NextTab || action == Shortcut::PreviousTab) {
      const auto position = static_cast<size_t>(std::find(tab_order_.begin(), tab_order_.end(), active_) - tab_order_.begin());
      const auto next = (position + (action == Shortcut::NextTab ? 1 : tab_order_.size() - 1)) % tab_order_.size();
      SelectTab(tab_order_[next]); return;
    }
    POINT pointer{}; GetCursorPos(&pointer);
    const auto hit = WindowFromPoint(pointer);
    const bool left_visible_hit = hit == left_frame_ || IsChild(left_frame_, hit);
    ScreenToClient(window_, &pointer);
    const auto area = PaneGeometry().left;
    const bool in_left = panes_.dual && left_visible_hit && pointer.x >= area.x && pointer.x < area.x + area.width &&
        pointer.y >= area.y && pointer.y < area.y + area.height;
    // The operated page owns its history. Actual native navigation/hash events
    // synchronize the peer once, including game/ bookmark actions, not clicks.
    const auto targets = gbrowser::webview::GameCommandTargets(active_, in_left ? kLeftGame : 0, panes_.dual);
    if (action == Shortcut::Reload || action == Shortcut::Back || action == Shortcut::Forward) {
      for (const auto index : targets) {
        if (index == SIZE_MAX || !views_[index].core) continue;
        auto& core = views_[index].core;
        if (action == Shortcut::Reload) Check(core->Reload());
        else if (const auto script = gbrowser::webview::GameHistoryScript(
            ComText([&](LPWSTR* source) { return core->get_Source(source); }), action == Shortcut::Forward)) {
          // CanGoBack/GoBack use Chromium's UI skipping rules. GBF can redirect
          // a just-restored supporter route back to the battle without another
          // user activation, making the next UI back unavailable. Traverse the
          // real session history for each explicit game back/forward instead.
          Check(core->ExecuteScript(script, nullptr));
        } else { BOOL can = FALSE;
          Check(action == Shortcut::Back ? core->get_CanGoBack(&can) : core->get_CanGoForward(&can));
          if (can) Check(action == Shortcut::Back ? core->GoBack() : core->GoForward()); }
      }
      return;
    }
    const auto zoom_index = in_left ? kLeftGame : active_;
    if (!views_[zoom_index].core) return;
    auto& view = views_[zoom_index];
    double value = 1; Check(view.controller->get_ZoomFactor(&value));
    if (action == Shortcut::ZoomIn) value = (std::min)(2.0, value * 1.1);
    else if (action == Shortcut::ZoomOut) value = (std::max)(0.5, value / 1.1);
    else if (action == Shortcut::ZoomReset) value = 1;
    else return;
    Check(view.controller->put_ZoomFactor(value));
    Layout();
  }
  void RefreshProfileTitle() {
    if (!window_) return;
    const auto title = std::wstring(L"StarryFish · ") + profiles_->Find(active_profile_).name +
        (mode_ == RunMode::Offline ? L" · 离线验证" : L"") + capture_.Label();
    SetWindowTextW(window_, title.c_str());
    SetWindowTextW(buttons_[0], title.c_str());
  }
  gbrowser::webview::transfer::JsonObject ProfileSnapshot() {
    using namespace gbrowser::webview;
    using namespace winrt::Windows::Data::Json;
    if (!shortcut_file_ || !loading_display_file_ || !cache_setting_)
      throw std::runtime_error("存在不可读配置，不能导出不完整的资料");
    SaveLayout(true); SaveTarouPlacement(); SavePanes();
    JsonObject document, settings;
    document.Insert(L"version", JsonValue::CreateNumberValue(1));
    document.Insert(L"name", JsonValue::CreateStringValue(profiles_->Find(active_profile_).name));
    auto put = [&](const wchar_t* name, const std::string& bytes) { settings.Insert(name, JsonValue::CreateStringValue(Utf8ToWide(bytes))); };
    put(L"window-layout.state", EncodeWindowState(window_state_)); put(L"game-panes.state", EncodePaneState(panes_));
    put(L"tarou-window.state", EncodeWindowState(tarou_window_state_));
    put(L"appearance.state", EncodeDarkAppearance(theme_.dark()));
    put(L"layout-choice.state", EncodeBrowserLayout(browser_layout_)); put(L"loading-display.state", EncodeHiddenLoading(hide_loading_));
    put(L"static-cache.state", cache_enabled_ ? "STARRYFISH-CACHE-ON\n" : "STARRYFISH-CACHE-OFF\n");
    put(L"shortcut-keys.state", characters::Encode(shortcut_keys_)); put(L"skill-overlay.state", skill_enabled_ ? "ON\n" : "OFF\n");
    document.Insert(L"settings", settings); document.Insert(L"proxy", JsonValue::CreateStringValue(Utf8ToWide(EncodeProxyPreference(proxy_preference_->state()))));
    return document;
  }
  void RunProfileTransfer(bool importing, gbrowser::webview::transfer::JsonObject document,
      gbrowser::webview::transfer::BrowserTransfer::Done done) {
    CancelCharacterSwitch(); character_tracker_ = {}; transfer_busy_ = true; EnableWindow(window_, FALSE);
    try {
    profile_transfer_ = std::make_unique<gbrowser::webview::transfer::BrowserTransfer>();
    std::vector<std::string> pages; for (auto& page : pages_) pages.push_back(page->url());
    if (!SetTimer(window_, 14, 120000, nullptr)) { transfer_busy_ = false; EnableWindow(window_, TRUE); throw std::runtime_error("无法启动迁移计时器"); }
    profile_transfer_->Start(environment_.Get(), window_, pages, importing, document,
      [this, done = std::move(done)](bool success, gbrowser::webview::transfer::JsonObject result) {
        KillTimer(window_, 14); transfer_busy_ = false; character_tracker_ = {}; EnableWindow(window_, TRUE);
        if (transfer_close_requested_) {
          if (pending_profile_import && pending_profile_import->id == active_profile_ && !pending_profile_import->restored)
            try { profiles_->Retire(active_profile_, pending_profile_import->previous); } catch (...) {}
          pending_profile_import.reset(); Close(); return;
        }
        try { done(success, result); }
        catch (...) { Notice(L"资料迁移未完成，原资料保留；请检查密码、文件名或运行时支持情况。"); }
      });
    } catch (...) {
      KillTimer(window_, 14); transfer_busy_ = false; EnableWindow(window_, TRUE);
      profile_transfer_.reset(); throw;
    }
  }
  void TransferProfile(bool importing) {
    using namespace gbrowser::webview;
    using namespace winrt::Windows::Data::Json;
    if (closing_ || failed_ || transfer_busy_ || loaded_ != 4) return;
    transfer::Request request;
    std::string created_profile;
    const wchar_t* stage = L"选择文件与输入密码";
    try {
      transfer_busy_ = true;
      const bool chosen = transfer::Choose(window_, importing, request);
      transfer_busy_ = false; character_tracker_ = {};
      if (!chosen) return;
      if (importing) {
        stage = L"读取资料文件";
        const auto archive = transfer::ReadArchive(request.path);
        stage = L"验证密码与文件完整性";
        auto plain = transfer::Decrypt(archive, request.password);
        SecureZeroMemory(request.password.data(), request.password.size() * sizeof(wchar_t)); request.password.clear();
        stage = L"解析资料内容";
        auto document = JsonObject::Parse(Utf8ToWide(plain)); SecureZeroMemory(plain.data(), plain.size());
        document = transfer::ConfigurationOnly(document);
        stage = L"新建资料并保存配置（密码已验证通过）";
        const auto id = transfer::PrepareImport(*profiles_, installation_state_, profile_leaf_, active_profile_, document);
        created_profile = id;
        pending_profile_import = std::make_shared<PendingProfileImport>(PendingProfileImport{id, active_profile_, false, document});
        next_profile_ = id; Close();
      } else {
        auto held = std::make_shared<transfer::Request>(std::move(request));
        RunProfileTransfer(false, ProfileSnapshot(), [this, held](bool success, JsonObject document) {
          try {
            if (!success) throw std::runtime_error("存储读取未完成");
            transfer::Validate(document);
            auto plain = WideToUtf8(std::wstring(document.Stringify()));
            auto encrypted = transfer::Encrypt(plain, held->password); SecureZeroMemory(plain.data(), plain.size());
            transfer::WriteArchive(held->path, encrypted);
            Notice(L"当前资料已导出为加密文件，包含布局、快捷键、扩展配置、书签和 Cookie。请单独保存密码。");
          } catch (...) { Notice(L"导出未完成；不会覆盖已有文件。请检查新文件名和存储接口支持情况。"); }
          SecureZeroMemory(held->password.data(), held->password.size() * sizeof(wchar_t)); held->password.clear();
        });
      }
    } catch (...) {
      transfer_busy_ = false; character_tracker_ = {};
      if (!created_profile.empty()) try { profiles_->Retire(created_profile, active_profile_); } catch (...) {}
      SecureZeroMemory(request.password.data(), request.password.size() * sizeof(wchar_t));
      const auto message = std::wstring(L"资料迁移未完成。失败阶段：") + stage + L"。\n现有资料保留。";
      Notice(message.c_str());
    }
  }
  void RestoreProfile() {
    auto pending = pending_profile_import;
    RunProfileTransfer(true, pending->document, [this, pending](bool success, gbrowser::webview::transfer::JsonObject) {
      if (success) {
        // Close the browser process to discard extension worker caches. The next
        // startup activates the imported profile only after normal verification.
        pending->restored = true; next_profile_ = active_profile_;
        Notice(L"配置已恢复。此次未迁移登录态，请在重新打开后登录游戏。原资料保留。");
      } else {
        pending_profile_import.reset();
        profiles_->Retire(active_profile_, pending->previous);
        next_profile_ = pending->previous;
        const auto message = std::wstring(L"导入未完成。失败阶段：") + profile_transfer_->Failure() + L"。\n正在返回原资料。新建的导入资料保留供排查，原资料未改变。";
        MessageBoxW(window_, message.c_str(), L"StarryFish", MB_OK | MB_ICONWARNING);
      }
      Close();
    });
  }
  void OpenProfiles() {
    if (closing_ || failed_ || loaded_ != 4) { Notice(L"请等待资料启动完成后切换"); return; }
    CancelSplit(); tab_close_.Cancel();
    if (GetCapture() == tabs_) ReleaseCapture();
    try {
      if (!profile_dialog_) profile_dialog_ = std::make_unique<gbrowser::webview::ProfileDialog>(
          *profiles_, active_profile_, theme_, [this](std::string id) {
            profiles_->Find(id); next_profile_ = std::move(id); Close();
          }, [this](bool importing) { TransferProfile(importing); });
      profile_dialog_->Open(window_);
    } catch (...) { Notice(L"资料窗口未打开；现有资料和代理保留"); }
  }
  void Action(int id) {
    if (closing_ || transfer_busy_) return;
    if (id == kCharacterKeys) {
      CancelCharacterSwitch();
      if (!shortcut_dialog_) shortcut_dialog_ = std::make_unique<gbrowser::webview::ShortcutDialog>(theme_,
          [this](const gbrowser::webview::characters::Bindings& keys, bool enabled) {
            if (!shortcut_file_) throw std::runtime_error("原快捷键配置不可读取，已保留原文件；无法保存");
            RAWINPUTDEVICE keyboard{0x01, 0x06, enabled ? 0u : RIDEV_REMOVE, enabled ? window_ : nullptr};
            if (enabled != character_enabled_ && !RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)))
              throw std::runtime_error("无法启用键盘输入");
            auto saved_keys=keys; saved_keys.master=enabled;
            try { shortcut_file_->Save(gbrowser::webview::characters::Encode(saved_keys)); }
            catch (...) {
              keyboard.dwFlags = character_enabled_ ? 0u : RIDEV_REMOVE; keyboard.hwndTarget = character_enabled_ ? window_ : nullptr;
              RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)); throw;
            }
            shortcut_keys_ = saved_keys; character_enabled_ = enabled; character_tracker_ = {}; CancelCharacterSwitch();
            for (size_t key = 0; key < character_tracker_.down.size(); ++key)
              character_tracker_.down[key] = (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
            InvalidateRect(buttons_[17], nullptr, TRUE);
          });
      shortcut_dialog_->Open(window_, shortcut_keys_, character_enabled_); return;
    }
    if (id == kSkillCooldown) {
      if (!skill_enabled_ && (failed_ || loaded_ != 4)) return;
      if (!skill_overlay_) skill_overlay_ = std::make_unique<gbrowser::webview::cooldown::Overlay>(window_, theme_, kSkillCooldown);
      gbrowser::webview::UserStateFile(settings_root_ / L"skill-overlay.state").Save(skill_enabled_ ? "OFF\n" : "ON\n");
      skill_enabled_ = !skill_enabled_;
      for (size_t index = 0; index < capture_observers_.size(); ++index) ConfigureCooldown(index);
      RefreshCooldown(); return;
    }
    if (id == kCaptureStop) { capture_.Stop(); CaptureTick(); return; }
    if (id == kCaptureClear) { capture_.Clear(); CaptureTick(); return; }
    if (id == kCaptureInfo) {
      const auto message = L"本机 MCP：以本程序路径启动 --capture-mcp --browser-pid " + std::to_wstring(GetCurrentProcessId()) +
          L"\nAgent 显式启动后采集 GBF 游戏视图；正文先脱敏，仅在内存保留。"
          L"\n默认限时 2 分钟，最长 10 分钟；mode=rolling 持续采集并保留最近 10 分钟（仍受容量上限约束）。"
          L"\n菜单可停止或清除；停止后保留当前快照，直到清除、下次采集或退出。";
      MessageBoxW(window_, message.c_str(), L"GBF 网络抓取", MB_OK | MB_ICONINFORMATION); return;
    }
    if (id == kCacheToggle) {
      try {
        if (!cache_setting_) throw std::runtime_error("cache setting unavailable");
        cache_setting_->Save(cache_enabled_ ? "STARRYFISH-CACHE-OFF\n" : "STARRYFISH-CACHE-ON\n");
        cache_enabled_ = !cache_enabled_; ++cache_epoch_;
      } catch (...) { Notice(L"缓存开关未能保存；现有连接和资料保持不变。"); }
      return;
    }
    if (id == kCacheClear) {
      if (!resource_cache_ || cache_clearing_) { Notice(L"缓存尚未就绪或正在清理。"); return; }
      const auto id_ = ++cache_sequence_;
      if (resource_cache_->Clear(id_)) {
        cache_clear_id_ = id_; cache_clearing_ = true; ++cache_epoch_; ++cache_jobs_; ++pending_;
      } else Notice(L"缓存正在忙，请稍后再清理。");
      return;
    }
    if (id == kCacheInfo) {
      const auto stats = resource_cache_ ? resource_cache_->stats() : gbrowser::webview::cache::Stats{};
      const auto text = std::wstring(cache_enabled_ ? L"本地静态缓存：已开启" : L"本地静态缓存：已关闭") +
          (stats.available ? L"\n" : L"（暂不可用，使用正常网络）\n") +
          L"本次从本地返回：" + std::to_wstring(cache_served_) + L" 个资源，" + std::to_wstring(cache_served_bytes_ / 1024) + L" KiB\n" +
          L"已保存：" + std::to_wstring(stats.files) + L" 个，" + std::to_wstring(stats.bytes / 1024 / 1024) + L" MiB / 10240 MiB\n" +
          L"本次写入：" + std::to_wstring(stats.writes) + L"；缓存异常回退：" + std::to_wstring(stats.errors) +
          L"\n\n超过一周未命中的缓存自动清理。只复用服务器允许缓存且未过期的公开静态资源；接口和登录数据不缓存。\n本地返回量不等于新增省流量，浏览器和加速器也可能已缓存。\n目录：" + (root_ / L"Cache" / L"Static-v1").native();
      MessageBoxW(window_, text.c_str(), L"StarryFish 静态资源缓存", MB_OK | MB_ICONINFORMATION); return;
    }
    if (id == kExit) { Close(); return; }
    if (id == kMinimize || id == kMaximize) {
      SendMessageW(window_, WM_SYSCOMMAND, id == kMinimize ? SC_MINIMIZE : IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE, 0);
      return;
    }
    if (id == kLayout1 || id == kLayout2) {
      ChangeDualLayout();
      try { layout_choice_file_->Save(gbrowser::webview::EncodeBrowserLayout(id == kLayout2)); }
      catch (...) { Notice(L"布局未保存，请稍后重试；当前布局保持不变"); return; }
      CancelSplit(); browser_layout_ = id == kLayout2; UpdateFont();
      if (!IsZoomed(window_)) {
        RECT outer{}; GetWindowRect(window_, &outer);
        SetWindowPos(window_, nullptr, 0, 0, (std::max)(static_cast<int>(outer.right - outer.left), MinimumWindowWidth()),
            outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      } else {
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (GetWindowPlacement(window_, &placement)) {
          auto& normal = placement.rcNormalPosition;
          normal.right = normal.left + (std::max)(static_cast<int>(normal.right - normal.left), MinimumWindowWidth());
          Check(SetWindowPlacement(window_, &placement) ? S_OK : E_FAIL);
        }
      }
      Layout(); SaveLayout();
      RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
      return;
    }
    if (id == kLightTheme || id == kDarkTheme) {
      // Cosmetic persistence failure must not stop an otherwise valid game session.
      try { appearance_file_->Save(gbrowser::webview::EncodeDarkAppearance(id == kDarkTheme)); }
      catch (...) { Notice(L"外观未保存，请稍后重试；当前主题保持不变"); return; }
      theme_.SetDark(id == kDarkTheme);
      for (HWND owned : {window_, login_window_, tarou_window_, arcarum_window_}) {
        if (owned) { theme_.Window(owned); RedrawWindow(owned, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); }
      }
      for (auto owned : auxiliary_windows_) if (owned) {
        theme_.Window(owned); RedrawWindow(owned, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
      }
      return;
    }
    if (id == kReturnProfile) {
      const auto previous = profiles_->state().selected;
      if (!closing_ && failed_ && previous != active_profile_ &&
          MessageBoxW(window_, L"正常关闭这个未就绪的资料，再返回上次成功资料？\n没有复制或重置任何资料。",
              L"返回原资料", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        profiles_->Find(previous); next_profile_ = previous; return_profile_ = true; Close();
      }
      return;
    }
    if (id == kProfiles) { OpenProfiles(); return; }
    if (id == kMenu) {
      HMENU menu = CreatePopupMenu();
      if (!menu) throw std::runtime_error("无法创建主菜单");
      // A component failure can leave loaded_ below four. Keep recovery and
      // normal shutdown reachable without enabling commands on a failed host.
      if (failed_) {
        if (profiles_->state().selected != active_profile_)
          AppendMenuW(menu, MF_STRING, kReturnProfile, L"返回上次成功资料");
      } else if (loaded_ == 4) {
        AppendMenuW(menu, MF_STRING, kHome, L"打开GBF主页");
        AppendMenuW(menu, MF_STRING, kProfiles, L"用户资料");
        AppendMenuW(menu, MF_STRING, kNewTab, L"新建标签\tCtrl+T");
        AppendMenuW(menu, MF_STRING, kProxySettings, L"ZeroOmega设置");
        AppendMenuW(menu, MF_STRING, kDownloads, L"下载");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      }
      HMENU features = CreatePopupMenu();
      if (!features) { DestroyMenu(menu); throw std::runtime_error("无法创建功能菜单"); }
      const UINT available = failed_ || loaded_ != 4 ? MF_GRAYED : 0;
      AppendMenuW(features, MF_STRING | available | (skill_enabled_ ? MF_CHECKED : 0), kSkillCooldown, L"技能 CD 悬浮条");
      AppendMenuW(features, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(features, MF_STRING | available, kMoba, panes_.moba_visible ? L"关闭左侧moba栏" : L"开启左侧moba栏");
      AppendMenuW(features, MF_STRING | available, kSecondaryMenu, panes_.secondary_menu_hidden ?
          L"显示副窗口右侧菜单" : L"隐藏副窗口右侧菜单并收窄");
      AppendMenuW(features, MF_STRING | available | (hide_loading_ ? MF_CHECKED : 0), kHideLoading, L"隐藏GBF加载小人和文字");
      AppendMenuW(features, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(features, MF_STRING, kCaptureInfo, L"网络抓取（MCP）");
      AppendMenuW(features, MF_STRING, kCaptureStop, L"停止网络抓取");
      AppendMenuW(features, MF_STRING, kCaptureClear, L"清除网络抓取记录");
      AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(features), L"功能");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING | (!theme_.dark() ? MF_CHECKED : 0), kLightTheme, L"浅色天蓝");
      AppendMenuW(menu, MF_STRING | (theme_.dark() ? MF_CHECKED : 0), kDarkTheme, L"深色群青");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING | (!browser_layout_ ? MF_CHECKED : 0), kLayout1, L"布局1 · 紧凑侧栏");
      AppendMenuW(menu, MF_STRING | (browser_layout_ ? MF_CHECKED : 0), kLayout2, L"布局2 · 经典浏览器");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, kExit, L"关闭Gbrowser");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING | (cache_enabled_ ? MF_CHECKED : 0), kCacheToggle, L"本地静态资源缓存");
      AppendMenuW(menu, MF_STRING, kCacheInfo, L"缓存状态");
      AppendMenuW(menu, MF_STRING, kCacheClear, L"清理静态资源缓存");
      const auto menu_art = theme_.PrepareMenu(menu);
      RECT button{}; GetWindowRect(buttons_[7], &button);
      const int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN,
          button.right, button.top, 0, window_, nullptr);
      DestroyMenu(menu); if (selected) Action(selected); return;
    }
    if (failed_ || loaded_ != 4) { Notice(L"当前操作尚不可用，请等待准备完成；若已停止运行，请正常关闭后重开"); return; }
    if (id == kHideLoading) {
      if (!loading_display_file_) { Notice(L"加载画面设置文件不可用，请保留原文件；当前显示保持不变"); return; }
      try { loading_display_file_->Save(gbrowser::webview::EncodeHiddenLoading(!hide_loading_)); }
      catch (...) { Notice(L"加载画面设置未保存，请稍后重试；当前显示保持不变"); return; }
      hide_loading_ = !hide_loading_;
      for (size_t index = 0; index < views_.size(); ++index) ApplyLoadingDisplay(index);
      return;
    }
    if (id == kCopy) { CopyCurrentLink(); return; }
    if (id == kReload) {
      auto& core = views_[active_].core;
      if (core) Check(core->Reload());
      return;
    }
    if (id == kNewTab) { NewTab(); return; }
    if (id == kProxySettings) { OpenSettingsTab(); return; }
    if (id == kDownloads) { OpenDownloads(); return; }
    if (id == kDual) { ToggleDual(); return; }
    if (id == kLink) { CopyPrimaryToSecondary(); return; }
    if (id == kDetach) { DetachArcarum(); return; }
    if (id == kTarouClose) { SetTarou(false); return; }
    if (id == kTarouDetach) { DetachTarou(); return; }
    if (id == kSecondaryMenu) {
      if (IsZoomed(window_)) { Notice(L"请先还原窗口，再调整副窗口侧栏宽度"); return; }
      UpdateSecondaryMenu(!panes_.secondary_menu_hidden, MeasuredSecondaryWidth(kLeftGame)); return;
    }
    if (id == kMoba) {
      ChangeDualLayout();
      const int inset = MobaPixels(active_);
      if (inset <= 0) { Notice(L"当前页面的Moba左栏尚未就绪；请在已加载的游戏页切换"); return; }
      const auto geometry = PaneGeometry();
      // Keep equal game frames; the visible right page determines the width
      // change. Each page still crops its own inset at its own zoom factor.
      const int change = panes_.moba_visible ? -inset : inset;
      const int game = (std::max)(1, geometry.right.width + change);
      const int columns = panes_.dual ? 2 : 1;
      SetGameWidth(game);
      panes_.moba_visible = !panes_.moba_visible;
      ChangeWindowWidth(change * columns); SavePanes(); return;
    }
    if (id == kExtensions) {
      MessageBoxW(window_, L"已自动加载：ZeroOmega、太郎、肉鸽、内置书签与汉化。\n\nZeroOmega设置由代理菜单打开。两个助手的更新为：关闭StarryFish，覆盖组件目录，再运行同构建安装器的重新加载入口。\n不自动覆盖资料或重新启用已停用扩展。",
          L"StarryFish 指定组件", MB_OK | MB_ICONINFORMATION); return;
    }
    if (id == kProxy) {
      ReadProxyMenu();return;
    }
    if (id == kHome) {
      if (mode_ == RunMode::Offline) {
        // Explicit user choice, never a saved/default open-network switch.
        // Both profile and application pins drain before relaunching.
        if (failed_) { Notice(L"离线候选已停止，请正常关闭后重新启动"); return; }
        const auto answer = MessageBoxW(window_,
            L"将关闭当前离线候选，并打开联网的游戏候选。\n游戏使用独立资料，已有CEF和离线资料保留。\n请先启用所选加速器或选择直连；配置匹配后才进入GBF。\n\n是否继续？",
            L"打开GBF游戏候选", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer == IDYES) { restart_gbf_ = true; Close(); }
        return;
      }
      if (!BrowsingAvailable()) return;
      OpenHome(); return;
    }
    // EDIT focus/change notifications and unknown commands are not panel
    // selections; keep the user's existing helper view untouched.
    if (id != kTarou && id != kArcarum) return;
    if (id == kTarou) SetTarou(!panes_.tarou);
    else SetArcarum(!panes_.arcarum);
  }
  void OpenHome(bool focus = true) {
    timings_.Mark("game-home-navigation-started");

    SelectTab(0, focus);
    Check(views_[0].core->Navigate(Utf8ToWide(GbfHomeUrl()).c_str()));
  }
  void CopyCurrentLink() {
    // Actual active page, never an unsubmitted draft, auth popup or hidden
    // control URL. Clipboard ownership transfers only on successful publication.
    if (settings_tabs_[active_] || !views_[active_].core) { Notice(L"当前没有可复制的网页链接"); return; }
    const auto source = ComText([&](LPWSTR* value) { return views_[active_].core->get_Source(value); });
    const auto url = WideToUtf8(source);
    if ((url != "about:blank" && !IsBrowserNetworkUrl(url)) || IsGbfLoginUrl(url)) {
      Notice(L"准备页和登录接续地址不复制"); return;
    }
    const auto bytes = (source.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) throw std::runtime_error("无法准备复制链接");
    auto content = GlobalLock(memory);
    if (!content) { GlobalFree(memory); throw std::runtime_error("无法锁定复制链接"); }
    memcpy(content, source.c_str(), bytes); GlobalUnlock(memory);
    if (!OpenClipboard(window_)) { GlobalFree(memory); Notice(L"剪贴板忙，请稍后再复制"); return; }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) { GlobalFree(memory); Notice(L"链接未复制，请重试"); return; }

  }
  void OpenDownloads() {
    if (!views_[active_].core) { Notice(L"请等待当前页面就绪后打开下载"); return; }
    // The official dialog owns progress, failures and user cancellation. Its
    // profile history also exposes exports started from an assistant pane.
    // A UI-only failure must not invalidate an otherwise valid game proxy.
    ComPtr<ICoreWebView2_9> downloads;
    BOOL open = FALSE;
    if (FAILED(views_[active_].core.As(&downloads)) || FAILED(downloads->get_IsDefaultDownloadDialogOpen(&open)) ||
        FAILED(open ? downloads->CloseDefaultDownloadDialog() : downloads->OpenDefaultDownloadDialog()))
      Notice(L"下载窗口未切换，请稍后重试");
  }
  void Close() {
    if (closing_) { Drain(); return; }
    if (transfer_busy_) { transfer_close_requested_ = true; if (profile_transfer_) profile_transfer_->Cancel(); return; }
    dual_transition_.Cancel();
    CancelCharacterSwitch();
    if (character_enabled_) {
      RAWINPUTDEVICE keyboard{0x01, 0x06, RIDEV_REMOVE, nullptr};
      RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard));
    }
    character_enabled_ = false;
    timings_.Mark("normal-close-requested");
    CancelSplit(); SaveLayout(true); SaveTarouPlacement(); SavePanes();
    closing_ = true; cache_stopping_ = true; // Block reentrant Drain until setup completes.
    callbacks_.Cancel(); // Renderer results are optional; do not wait for page JS.
    ShowWindow(window_, SW_HIDE);
    // Independent of the cache timer: keep observing the retained process
    // handle even after the disk worker has completed or the exit event is late.
    SetTimer(window_, kShutdownTimer, 50, nullptr);
    skill_enabled_ = false; skill_overlay_.reset();
    capture_server_.reset(); capture_.Clear();
    for (auto& observer : capture_observers_) observer.reset();
    KillTimer(window_, 9);
    if (shortcut_dialog_) shortcut_dialog_->Close();
    if (profile_dialog_) profile_dialog_->Close();
    proxy_verified_ = false; ++cache_network_epoch_; ++cache_epoch_;
    for (auto& pair : cache_pending_) {
      auto& held = pair.second;
      if (!held.deferral) continue;
      ComPtr<ICoreWebView2WebResourceResponse> denied;
      if (environment_ && SUCCEEDED(environment_->CreateWebResourceResponse(nullptr, 403, L"Forbidden", L"", &denied)))
        held.args->put_Response(denied.Get());
      held.deferral->Complete(); held.deferral.Reset(); held.args.Reset();
    }
    if (resource_cache_) resource_cache_->Shutdown();
    if (arcarum_window_) ShowWindow(arcarum_window_, SW_HIDE);
    if (tarou_window_) ShowWindow(tarou_window_, SW_HIDE);
    if (proxy_) proxy_->Invalidate(3);
    KillTimer(window_, 2);
    KillTimer(window_, 3);
    KillTimer(window_, 4); KillTimer(window_, 5); split_layout_pending_ = false;
    KillTimer(window_,7);proxy_menu_loading_=false;
    KillTimer(window_, 8); ++login_attempt_;
    CloseLogin();
    for (size_t index = gbrowser::webview::TabSlots::kAuxFirst; index < views_.size(); ++index)
      Guard([&] { CloseAuxiliary(index, view_generations_[index]); });
    cache_stopping_ = false;
    Drain();
  }
  void Drain() {
    if (!closing_ || draining_ || cache_stopping_) return;
    draining_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{draining_};
    // Close first. Waiting for pending script/content results before Close can
    // leave a hung renderer holding the whole application open forever.
    // Creation callbacks still have mandatory tickets and close any late view.
    for (auto& view : views_) {
      auto controller = std::move(view.controller);
      view.core.Reset();
      if (controller) controller->Close();
    }
    for (auto& extension : extensions_) extension.Reset();
    profile_.Reset();
    if (pending_) return; // Only controller/environment creation and disk jobs.
    if (resource_cache_) {
      // Periodic pruning and hit acknowledgments run without UI request IDs.
      // Signal shutdown and keep pumping; never join during background disk IO.
      if (!resource_cache_->Shutdown()) return;
      resource_cache_.reset(); KillTimer(window_, 6);
    }
    // Never release profile/source pins while our runtime process is still
    // alive. No terminate/kill fallback, no unrelated browser process handling.
    const bool process_signaled = browser_process_ && WaitForSingleObject(browser_process_, 0) == WAIT_OBJECT_0;
    if (browser_started_ && !browser_exited_ && !process_signaled) {
       return;
    }
    environment_.Reset();
    KillTimer(window_, kShutdownTimer);
    timings_.Mark("normal-close-complete");
    DestroyArcarumWindow();
    DestroyTarouWindow();
    DestroyWindow(window_); window_ = nullptr;
  }
  void RestoreLayout(int show) {
    restoring_layout_ = true;
    if (window_file_->bytes()) {
      WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
      placement.showCmd = SW_SHOWNORMAL;
      placement.rcNormalPosition = {window_state_.x, window_state_.y,
          window_state_.x + window_state_.width, window_state_.y + window_state_.height};
      // rcNormalPosition uses workspace coordinates. Keep Get/SetWindowPlacement
      // paired; never pass these coordinates to SetWindowPos.
      if (!SetWindowPlacement(window_, &placement)) throw std::runtime_error("无法恢复窗口布局");
      // Read Windows' adjustment for detached monitors, then also constrain
      // partial overhangs. Do not reuse the original saved off-screen origin.
      if (!GetWindowPlacement(window_, &placement)) throw std::runtime_error("无法核对恢复后的窗口位置");
      const auto dpi = Dpi();
      MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
      if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor))
        throw std::runtime_error("无法核对窗口所在屏幕");
      const int width = (std::min)(static_cast<int>(monitor.rcWork.right - monitor.rcWork.left),
          (std::max)(UiPixels(browser_layout_ ? 600 : 400, dpi), MulDiv(window_state_.width, static_cast<int>(dpi), static_cast<int>(window_state_.dpi))));
      const int height = (std::min)(static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top),
          (std::max)(UiPixels(520, dpi), MulDiv(window_state_.height, static_cast<int>(dpi), static_cast<int>(window_state_.dpi))));
      placement.rcNormalPosition = gbrowser::webview::VisiblePlacement(placement.rcNormalPosition,
          monitor.rcMonitor, monitor.rcWork, width, height);
      placement.showCmd = window_state_.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
      if (!SetWindowPlacement(window_, &placement)) throw std::runtime_error("无法恢复缩放后的窗口布局");
      maximized_ = window_state_.maximized;
    } else ShowWindow(window_, show);
    restoring_layout_ = false; layout_ready_ = true;
    // Convert old percentage layouts once, after restoring the saved window.
    if (!panes_.game_width) {
      auto geometry = PaneGeometry();
      if (window_file_->bytes()) {
        RECT outer{}, client{}; GetWindowRect(window_, &outer); GetClientRect(window_, &client);
        // WINDOWPLACEMENT keeps normal dimensions even when maximized. Use
        // that saved width instead of today's screen size or monitor clamp.
        const int width = MulDiv(window_state_.width, static_cast<int>(Dpi()), static_cast<int>(window_state_.dpi)) -
            static_cast<int>(outer.right - outer.left - client.right);
        geometry = gbrowser::webview::ComputePaneLayout((std::max)(0, width), client.bottom,
            Dpi(), panel_percent_, panes_, arcarum_detached_, browser_layout_);
      }
      SetGameWidth(geometry.right.width); SavePanes();
    }
    UpdateFont(); Layout();
  }
  void QueueLayoutSave() {
    if (layout_ready_ && !restoring_layout_ && !closing_ && !layout_failed_ && !dual_transition_.active() &&
        !split_.dragging() && !helper_split_.dragging())
      if (!SetTimer(window_, 4, 350, nullptr)) { layout_failed_ = true; Notice(L"本次窗口布局无法自动保存，原设置保留"); }
  }
  void SaveLayout(bool report = false) noexcept {
    if (!layout_ready_ || restoring_layout_ || !window_ || dual_transition_.active() ||
        split_.dragging() || helper_split_.dragging()) return;
    try {
      if (layout_failed_) throw std::runtime_error("上次布局保存失败");
      WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
      if (!GetWindowPlacement(window_, &placement)) throw std::runtime_error("无法读取窗口布局");
      const auto& normal = placement.rcNormalPosition;
      gbrowser::webview::WindowState next{normal.left, normal.top, normal.right - normal.left,
          normal.bottom - normal.top, Dpi(), window_state_.panel_percent, maximized_};
      // Keep the legacy ratio slot unchanged: pane state owns the new layout.
      // A failed pane save must not reinterpret an old schema1 ratio twice.
      window_file_->Save(gbrowser::webview::EncodeWindowState(next)); window_state_ = next;
    } catch (...) {
      layout_failed_ = true;
      Notice(L"本次窗口布局未保存；原设置及账号资料保留");
      if (report) MessageBoxW(window_, L"本次窗口布局未保存，原设置和账号资料均保留。\n请保留设置文件，不要反复尝试覆盖。",
          L"StarryFish 布局保存未完成", MB_OK | MB_ICONWARNING);
    }
  }
  void CancelSplit() noexcept {
    KillTimer(window_, 5); split_layout_pending_ = false;
    const bool dragging = split_.dragging() || helper_split_.dragging();
    if (split_.dragging()) { split_.Cancel(); panes_.game_width = drag_game_width_; panes_.game_dpi = drag_game_dpi_; }
    if (helper_split_.dragging()) { helper_split_.Cancel(); panes_.tarou_share = drag_helper_share_; }
    if (dragging) Guard([&] { Layout(); });
    if (GetCapture() == divider_ || GetCapture() == helper_divider_) ReleaseCapture();
  }
  int PointerX() const {
    POINT point{};
    if (!GetCursorPos(&point) || !ScreenToClient(window_, &point)) throw std::runtime_error("无法读取分屏手势");
    return point.x;
  }
  static LRESULT CALLBACK DividerProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                          UINT_PTR subclass, DWORD_PTR reference) {
    auto host = reinterpret_cast<Host*>(reference);
    const bool inner = window == host->helper_divider_;
    auto& gesture = inner ? host->helper_split_ : host->split_;
    const auto apply = [&] {
      const int value = gesture.Move(host->PointerX());
      if (inner) host->SetHelperWidth(value);
      else host->SetGameWidth(value / (host->panes_.dual ? 2 : 1));
    };
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, DividerProcedure, subclass);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT rect{}; GetClientRect(window, &rect);
      host->theme_.Background(dc, rect);
      const int middle = (rect.top + rect.bottom) / 2, grip = UiPixels(24, host->Dpi());
      rect.left = (rect.left + rect.right) / 2; rect.right = rect.left + (std::max)(1, UiPixels(1, host->Dpi()));
      rect.top = middle - grip; rect.bottom = middle + grip;
      host->theme_.Fill(dc, rect, host->theme_.colors().border); EndPaint(window, &paint); return 0;
    }
    if (message == WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
    if (message == WM_LBUTTONDOWN) {
      host->Guard([&] {
        if (host->closing_ || host->split_.dragging() || host->helper_split_.dragging()) return;
        host->ChangeDualLayout();
        const auto geometry = host->PaneGeometry();
        const int count = host->panes_.dual ? 2 : 1;
        const int body = inner ? geometry.tarou.width + geometry.arcarum.width :
            geometry.chrome.content.rail.x - gbrowser::webview::LeftChromeInset(host->Dpi()) -
            (host->panes_.dual ? UiPixels(6, host->Dpi()) : 0) - geometry.divider.width +
            gbrowser::webview::SecondaryMenuCut(host->panes_, host->Dpi(), geometry.right.width);
        const int minimum = (std::min)(body / 2, UiPixels(inner ? 120 : 160 * count, host->Dpi()));
        const int reserve = (std::min)(body / 2, UiPixels(inner ? 120 : 120 * (host->panes_.arcarum && !host->arcarum_detached_ &&
            host->panes_.tarou && !host->panes_.tarou_detached ? 2 : 1), host->Dpi()));
        if (gesture.Begin(host->PointerX(), inner ? geometry.tarou.width : geometry.right.width * count, minimum, body - reserve)) {
          host->drag_game_width_ = host->panes_.game_width; host->drag_game_dpi_ = host->panes_.game_dpi;
          host->drag_helper_share_ = host->panes_.tarou_share;
          KillTimer(host->window_, 4); SetCapture(window);
          if (GetCapture() != window) host->CancelSplit();
        }
      }); return 0;
    }
    if (message == WM_MOUSEMOVE && gesture.dragging()) {
      host->Guard([&] {
        apply();
        // Coalesce mouse moves; do not restart the timer and postpone rendering
        // indefinitely. Release/cancel always flushes the final geometry.
        if (!host->split_layout_pending_) {
          host->split_layout_pending_ = SetTimer(host->window_, 5, 16, nullptr) != 0;
          if (!host->split_layout_pending_) host->Layout();
        }
      }); return 0;
    }
    if (message == WM_LBUTTONUP && gesture.dragging()) {
      host->Guard([&] {
        apply(); gesture.Finish();
        KillTimer(host->window_, 5); host->split_layout_pending_ = false;
        if (GetCapture() == window) ReleaseCapture();
        host->Layout(); host->SaveLayout(); host->SavePanes();
      }); return 0;
    }
    if (message == WM_LBUTTONDBLCLK) {
      host->Guard([&] { host->ChangeDualLayout(); host->CancelSplit();
        if (inner) host->panes_.tarou_share = 5000;
        else { const auto geometry = host->PaneGeometry();
          host->SetGameWidth((geometry.chrome.content.rail.x - gbrowser::webview::LeftChromeInset(host->Dpi()) - geometry.divider.width -
              (host->panes_.dual ? UiPixels(6, host->Dpi()) : 0) + 2 *
              gbrowser::webview::SecondaryMenuCut(host->panes_, host->Dpi(), geometry.right.width)) / (host->panes_.dual ? 4 : 2)); }
        host->Layout(); host->SaveLayout(); host->SavePanes(); }); return 0;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || (message == WM_KEYDOWN && wparam == VK_ESCAPE))
      host->CancelSplit();
    return DefSubclassProc(window, message, wparam, lparam);
  }
  int TabPosition(HWND control, POINT point) const {
    RECT rect{}; GetClientRect(control, &rect);
    return gbrowser::webview::IconTabLayout(rect.right, rect.bottom, Dpi(), tab_order_.size()).Hit(point.x, point.y);
  }
  std::optional<gbrowser::webview::TabSlots::Ticket> TabHit(HWND control, LPARAM point) const {
    const int position = TabPosition(control, {static_cast<short>(LOWORD(point)), static_cast<short>(HIWORD(point))});
    if (position < 0 || static_cast<size_t>(position) >= tab_order_.size()) return {};
    const auto index = tab_order_[static_cast<size_t>(position)];
    if (!index) return {}; // Fixed game tab is intentionally ineligible.
    return gbrowser::webview::TabSlots::Ticket{index, view_generations_[index]};
  }
  static LRESULT CALLBACK TabProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR subclass, DWORD_PTR reference) {
    auto host = reinterpret_cast<Host*>(reference);
    if (message == WM_NCDESTROY) {
      host->tab_close_.Cancel(); RemoveWindowSubclass(window, TabProcedure, subclass);
    } else if (message == WM_NCHITTEST) {
      POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
      ScreenToClient(window, &point);
      // Unoccupied tab-strip space remains a native drag/double-click region.
      if (host->TabPosition(window, point) < 0) return HTTRANSPARENT;
      return HTCLIENT;
    } else if (message == WM_ERASEBKGND) { return 1;
    } else if (message == WM_PAINT) {
      PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
      RECT rect{}; GetClientRect(window, &rect);
      HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, (std::max)(1L, rect.right), (std::max)(1L, rect.bottom));
      if (buffer && bitmap) {
        const auto old = SelectObject(buffer, bitmap); host->theme_.Tabs(window, buffer, host->tab_order_, host->active_);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, buffer, 0, 0, SRCCOPY); SelectObject(buffer, old);
      } else host->theme_.Tabs(window, dc, host->tab_order_, host->active_);
      if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer); EndPaint(window, &paint); return 0;
    } else if (message == WM_MOUSEMOVE) {
      const int position = host->TabPosition(window, {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))});
      if (position != host->tooltip_tab_) {
        host->tooltip_tab_ = position;
        if (host->tabs_tip_) {
          SendMessageW(host->tabs_tip_, TTM_POP, 0, 0);
          SendMessageW(host->tabs_tip_, TTM_UPDATE, 0, 0);
        }
      }
    } else if (message == WM_LBUTTONDOWN) {
      const int position = host->TabPosition(window, {static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))});
      if (!host->closing_ && position >= 0) host->Guard([&] {
        SendMessageW(window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
        host->SelectTab(host->tab_order_[static_cast<size_t>(position)]);
      });
      return 0;
    } else if (message == WM_MBUTTONDOWN) {
      if (!host->closing_) {
        host->tab_close_.Down(host->TabHit(window, lparam)); SetCapture(window);
        if (GetCapture() != window) host->tab_close_.Cancel();
      }
      return 0;
    } else if (message == WM_MBUTTONUP) {
      const auto ticket = GetCapture() == window ? host->tab_close_.Up(host->TabHit(window, lparam)) : std::nullopt;
      host->tab_close_.Cancel(); if (GetCapture() == window) ReleaseCapture();
      // Defer controller destruction out of the control's synchronous handler.
      if (ticket) PostMessageW(host->window_, kCloseOrdinary, static_cast<WPARAM>(ticket->index),
          static_cast<LPARAM>(ticket->generation));
      return 0;
    } else if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE ||
               (message == WM_KEYDOWN && wparam == VK_ESCAPE)) {
      host->tab_close_.Cancel(); if (GetCapture() == window) ReleaseCapture();
    } else if (message == WM_GETDLGCODE) { return DLGC_WANTARROWS | DLGC_WANTCHARS;
    } else if (message == WM_KEYDOWN && (wparam == VK_LEFT || wparam == VK_RIGHT || wparam == VK_HOME || wparam == VK_END)) {
      if (!host->closing_) host->Guard([&] {
        const auto current = static_cast<size_t>(std::find(host->tab_order_.begin(), host->tab_order_.end(), host->active_) - host->tab_order_.begin());
        const auto count = host->tab_order_.size();
        const auto next = wparam == VK_HOME ? size_t{0} : wparam == VK_END ? count - 1 :
            (current + (wparam == VK_RIGHT ? 1 : count - 1)) % count;
        SendMessageW(window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        host->SelectTab(host->tab_order_[next], false); SetFocus(window);
      });
      return 0;
    } else if (message == WM_KEYDOWN && (wparam == VK_RETURN || wparam == VK_SPACE)) {
      if (!host->closing_) host->FocusActive(); return 0;
    } else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
      InvalidateRect(window, nullptr, FALSE);
    }
    return DefSubclassProc(window, message, wparam, lparam);
  }
  static LRESULT CALLBACK FrameProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
      UINT_PTR subclass, DWORD_PTR reference) {
    auto host = reinterpret_cast<Host*>(reference);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FrameProcedure, subclass);
    if (message == WM_WINDOWPOSCHANGED && !host->dual_positioning_ && !host->closing_ && !host->layout_applying_ &&
        !(reinterpret_cast<WINDOWPOS*>(lparam)->flags & SWP_NOMOVE))
      host->Guard([&] { host->NotifyViewPositions(); });

    return DefSubclassProc(window, message, wparam, lparam);
  }
  void PaintButton(const DRAWITEMSTRUCT& draw) const {
    using gbrowser::webview::UiIcon;
    const std::array<UiIcon, 18> icons{UiIcon::Profile, UiIcon::Proxy, UiIcon::Tarou, UiIcon::Arcarum,
        UiIcon::Dual, UiIcon::Copy, UiIcon::Components, UiIcon::More, UiIcon::Arrow, UiIcon::Detach, UiIcon::Close, UiIcon::Detach,
        UiIcon::Minimize, IsZoomed(window_) ? UiIcon::Restore : UiIcon::Maximize, UiIcon::Close,
        UiIcon::Reload, UiIcon::Plus, UiIcon::Keyboard};
    const auto found = std::find(buttons_.begin(), buttons_.end(), draw.hwndItem);
    if (found == buttons_.end()) { theme_.Button(draw, font_, UiIcon::Detach); return; }
    const auto i = static_cast<size_t>(found - buttons_.begin());
    const bool selected = (i == 2 && panes_.tarou) || (i == 3 && panes_.arcarum) || (i == 4 && panes_.dual) ||
        (i == 11 && panes_.tarou_detached) || (i == 17 && character_enabled_);
    const bool profile_icon_only = i == 0 && draw.rcItem.right - draw.rcItem.left < UiPixels(80, Dpi());
    theme_.Button(draw, font_, icons[i], false, selected, i != 0 || profile_icon_only, 0, i != 8);
  }
  static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (!host) return DefWindowProcW(window, message, wparam, lparam);
    wchar_t class_name[64]{}; GetClassNameW(window, class_name, 64);
    const bool main_frame = wcscmp(class_name, L"GbrowserWebView2Candidate") == 0;
    if (main_frame && message == WM_NCCALCSIZE) {
      MONITORINFO monitor{sizeof(monitor)};
      const bool zoomed = IsZoomed(window) && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
      const auto dpi = GetDpiForWindow(window);
      const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
      gbrowser::webview::FrameClientRect(lparam, wparam != 0, zoomed, monitor.rcWork, border);
      return 0;
    }
    if (main_frame && message == WM_NCHITTEST) {
      POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))}; ScreenToClient(window, &point);
      RECT client{}; GetClientRect(window, &client);
      const auto edge = gbrowser::webview::FrameEdge(point, client, UiPixels(5, GetDpiForWindow(window)), IsZoomed(window) != FALSE);
      if (edge != HTCLIENT) return edge;
      if (point.y < UiPixels(44, GetDpiForWindow(window))) {
        const auto child = ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!child || child == window) return HTCAPTION;
        if (child == host->tabs_) {
          POINT tab_point = point; MapWindowPoints(window, child, &tab_point, 1);
          if (host->TabPosition(child, tab_point) < 0) return HTCAPTION;
        }
      }
      return HTCLIENT;
    }
    if (message == WM_CREATE) host->theme_.Window(window);
    if (main_frame && message == WM_WINDOWPOSCHANGED) {
      const auto flags = reinterpret_cast<WINDOWPOS*>(lparam)->flags;
      if (!host->dual_positioning_ && (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE))) {
        host->Guard([&] { host->ChangeDualLayout(); });
        host->InvalidateChrome();
      }
      if (!host->dual_positioning_) host->Guard([&] { host->RefreshCooldown(); });
      // Continue to DefWindowProc: it emits the normal WM_SIZE/WM_MOVE events.
    }
    if (message == WM_ERASEBKGND) {
      RECT rect{}; GetClientRect(window, &rect); host->theme_.Background(reinterpret_cast<HDC>(wparam), rect); return 1;
    }
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
      RECT rect{}; GetClientRect(window, &rect); host->theme_.Background(dc, rect);
      if (window == host->window_ && host->address_) {
        GetWindowRect(host->address_, &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
        InflateRect(&rect, UiPixels(6, host->Dpi()), UiPixels(5, host->Dpi()));
        host->theme_.Round(dc, rect, UiPixels(9, host->Dpi()), host->theme_.colors().surface, host->theme_.colors().border);
      }
      EndPaint(window, &paint); return 0;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
      return reinterpret_cast<LRESULT>(host->theme_.Control(reinterpret_cast<HDC>(wparam),
          message != WM_CTLCOLORSTATIC || reinterpret_cast<HWND>(lparam) == host->address_));
    if (message == WM_MEASUREITEM && reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlType == ODT_MENU) {
      host->theme_.MeasureMenu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam), window, host->font_); return TRUE;
    }
    if (message == WM_WINDOWPOSCHANGED && (window == host->tarou_window_ || window == host->arcarum_window_)) {
      host->Guard([&] { const auto index = window == host->tarou_window_ ? 1 : kArcarumView;
        if (host->views_[index].controller) Check(host->views_[index].controller->NotifyParentWindowPositionChanged()); });
    }
    if (message == WM_DRAWITEM) {
      const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (draw.CtlType == ODT_MENU) { host->theme_.Menu(draw, host->font_, GetDpiForWindow(window)); return TRUE; }
      if (draw.hwndItem == host->tarou_title_) { host->theme_.Header(draw, host->font_, UiPixels(22, GetDpiForWindow(window))); return TRUE; }
      if (draw.CtlType == ODT_BUTTON) { host->PaintButton(draw); return TRUE; }
    }
    if (window == host->tarou_window_) {
      if (message == WM_DPICHANGED) {
        const auto rect = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        host->Guard([&] { host->LayoutTarouWindow(); }); return 0;
      }
      if (message == WM_SIZE) { host->Guard([&] { host->LayoutTarouWindow(); }); return 0; }
      if (message == WM_EXITSIZEMOVE) { host->SaveTarouPlacement(); return 0; }
      if (message == WM_CLOSE) { host->Guard([&] { host->SetTarou(false); }); return 0; }
      if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) { host->Guard([&] { host->Action(LOWORD(wparam)); }); return 0; }
      if (message == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {UiPixels(400, GetDpiForWindow(window)), UiPixels(300, GetDpiForWindow(window))}; return 0;
      }
      if (message == WM_DESTROY) return 0;
      return DefWindowProcW(window, message, wparam, lparam);
    }
    if (window == host->arcarum_window_) {
      if (message == WM_DPICHANGED) {
        const auto rect = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        host->Guard([&] { host->LayoutArcarumWindow(); }); return 0;
      }
      if (message == WM_SIZE) { host->Guard([&] { host->LayoutArcarumWindow(); }); return 0; }
      if (message == WM_CLOSE) { host->Guard([&] { host->SetArcarum(false); }); return 0; }
      if (message == WM_COMMAND && LOWORD(wparam) == kDetach) { host->Guard([&] { host->DetachArcarum(); }); return 0; }
      if (message == WM_DESTROY) return 0;
      return DefWindowProcW(window, message, wparam, lparam);
    }
    if (window != host->window_) {
      const auto auxiliary = host->AuxiliaryIndex(window);
      if (IsAuxiliary(auxiliary)) {
        if (message == WM_SIZE) { host->Guard([&] { host->LayoutAuxiliary(auxiliary); }); return 0; }
        if (message == WM_CLOSE) { host->Guard([&] { host->CloseAuxiliary(auxiliary, host->view_generations_[auxiliary]); }); return 0; }
        if (message == WM_DPICHANGED) {
          const auto bounds = reinterpret_cast<RECT*>(lparam);
          SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
              bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
          host->Guard([&] { host->LayoutAuxiliary(auxiliary); }); return 0;
        }
        if (message == WM_GETMINMAXINFO) {
          const auto dpi = GetDpiForWindow(window);
          reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {UiPixels(400, dpi ? dpi : 96), UiPixels(320, dpi ? dpi : 96)};
          return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
      }
      if (message == kFocusLogin) { host->Guard([&] { host->FocusLogin(); }); return 0; }
      if (message == WM_SETFOCUS || (message == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE)) {
        // Defer out of the activation callback before entering the controller.
        PostMessageW(window, kFocusLogin, 0, 0);
      }
      if (message == WM_SIZE) { host->Guard([&] { host->LayoutLogin(); }); return 0; }
      if (message == WM_CLOSE) { host->CloseLogin(true); return 0; }
      if (message == WM_DESTROY) { if (window == host->login_window_) host->login_window_ = nullptr; return 0; }
      if (message == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto dpi = GetDpiForWindow(window);
        info->ptMinTrackSize = {UiPixels(480, dpi ? dpi : 96), UiPixels(520, dpi ? dpi : 96)}; return 0;
      }
    }
    if (message == kRestoreProxy) {
      host->Guard([&] {
        if (!host->closing_ && !host->failed_ && host->restore_probe_ &&
            host->operation_ == static_cast<uint64_t>(wparam) && host->proxy_ &&
            host->proxy_->state() == ProxyInitializationState::PreflightDifferent)
          host->StartProxy(static_cast<ProxyAction>(lparam),false,host->proxy_custom_name_);
      }); return 0;
    }
    if (message == kCloseLogin) {
      if (host->CurrentView(3, static_cast<uint64_t>(lparam))) host->CloseLogin(true);
      return 0;
    }
    if (message == kProxyMenuReady) {host->Guard([&]{host->ShowProxyMenu(static_cast<uint64_t>(wparam));});return 0;}
    if (message == kMeasureSecondaryMenu) {
      host->Guard([&] {
        if (!host->closing_ && !host->failed_ && host->dual_transition_.CanCreate(host->panes_.dual) &&
            !IsZoomed(window) && !IsIconic(window) && host->views_[kLeftGame].core)
          Check(host->views_[kLeftGame].core->ExecuteScript(Utf8ToWide(std::string(kGameUiScript) + "();").c_str(), nullptr));
      }); return 0;
    }
    if (message == kHideTarou) { host->Guard([&] { host->SetTarou(false); }); return 0; }
    if (message == kHideArcarum) { host->Guard([&] { host->SetArcarum(false); }); return 0; }
    if (message == kCloseOrdinary) {
      host->Guard([&] { host->CloseTab(static_cast<size_t>(wparam), static_cast<uint64_t>(lparam)); }); return 0;
    }
    if (message == kPageShortcut) {
      host->Guard([&] {
        const auto origin = static_cast<uint64_t>(lparam);
        const auto index = static_cast<size_t>(origin & 255);
        if ((index == host->active_ || index == 1 || index == kArcarumView ||
            (index == kLeftGame && host->panes_.dual)) && host->CurrentView(index, origin >> 8))
          host->PageAction(static_cast<Shortcut>(wparam));
      }); return 0;
    }
    if (message == WM_DPICHANGED) {
      const auto bounds = reinterpret_cast<RECT*>(lparam);
      if (main_frame) { host->Guard([&] { host->ReconcileDisplay(*bounds); }); return 0; }
      SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
          bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
      host->Guard([&] { if (window == host->window_) { host->UpdateFont(); host->Layout(); }
                       else host->LayoutLogin(); }); return 0;
    }
    if (main_frame && message == WM_DISPLAYCHANGE) {
      host->Guard([&] { RECT bounds{}; GetWindowRect(window, &bounds); host->ReconcileDisplay(bounds); }); return 0;
    }
    if (main_frame && (message == WM_ENTERSIZEMOVE || (message == WM_SYSCOMMAND &&
        ((wparam & 0xfff0) == SC_MAXIMIZE || (wparam & 0xfff0) == SC_MINIMIZE || (wparam & 0xfff0) == SC_RESTORE))))
      host->Guard([&] { host->ChangeDualLayout(); });
    if (message == WM_SIZE) {
      if (main_frame && host->dual_positioning_) return 0; // Explicit placement lays out once after SetWindowPos.
      host->Guard([&] {
        if (!host->dual_transition_.active()) host->CancelSplit();
        host->Layout();
        if (wparam != SIZE_MINIMIZED) {
          if (wparam == SIZE_RESTORED && !host->dual_positioning_ && !host->dual_transition_.active())
            PostMessageW(window, kMeasureSecondaryMenu, 0, 0);
          host->maximized_ = wparam == SIZE_MAXIMIZED;
          host->QueueLayoutSave();
        }
      }); return 0;
    }
    if (main_frame && message == WM_MOVE && !host->dual_positioning_ && !host->closing_)
      host->Guard([&] { host->NotifyViewPositions(); });
    if (message == WM_EXITSIZEMOVE) { host->SaveLayout(); return 0; }
    if (message == WM_CANCELMODE) { host->Guard([&] { host->CancelSplit(); }); }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lparam)->hwndFrom == host->tabs_tip_ &&
        reinterpret_cast<NMHDR*>(lparam)->code == TTN_GETDISPINFOW) {
      POINT point{}; GetCursorPos(&point); ScreenToClient(host->tabs_, &point);
      const int position = host->TabPosition(host->tabs_, point);
      auto& tip = *reinterpret_cast<NMTTDISPINFOW*>(lparam);
      tip.lpszText = position >= 0 ? host->titles_[host->tab_order_[static_cast<size_t>(position)]].data() : const_cast<wchar_t*>(L"");
      return 0;
    }
    if (message == WM_COMMAND) {
      host->Guard([&] {
        if (reinterpret_cast<HWND>(lparam) == host->address_ && HIWORD(wparam) == EN_CHANGE && !host->updating_address_) {
          host->draft_text_[host->active_] = WindowText(host->address_);
          host->drafts_[host->active_] = true;
        } else if (HIWORD(wparam) == BN_CLICKED) host->Action(LOWORD(wparam));
      }); return 0;
    }
    if (message == kCacheResult) { host->CompleteCacheJobs(); return 0; }
    if (message == kShowNotice) {
      const auto text = host->notice_pending_; host->notice_pending_ = nullptr;
      if (text && !host->closing_) {
        host->notice_open_ = true;
        MessageBoxW(window, text, L"StarryFish", MB_OK | MB_ICONINFORMATION);
        host->notice_open_ = false;
      }
      return 0;
    }
    if (message == WM_ENABLE && wparam) host->Guard([&] { host->RefreshProfileTitle(); });
    if (message == WM_CLOSE) { host->Close(); return 0; }
    if (message == WM_ACTIVATEAPP) {
      host->CancelCharacterSwitch(); host->character_tracker_.modifier_candidate = 0;
      if (wparam && host->character_enabled_) {
        for (size_t key = 0; key < host->character_tracker_.down.size(); ++key)
          host->character_tracker_.down[key] = (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
      }
    }
    if (message == WM_INPUT) {
      host->CharacterKeyboardInput(reinterpret_cast<HRAWINPUT>(lparam));
      return DefWindowProcW(window, message, wparam, lparam); // release foreground input resources
    }
    if (message == WM_TIMER) {
      if (wparam == 14) { KillTimer(window, 14); if (host->profile_transfer_) host->profile_transfer_->Cancel(); return 0; }
      if (wparam == kShutdownTimer) host->Drain();
      if (wparam == kCharacterTimer) host->PollCharacterSwitch();
      if (wparam == 9) host->CaptureTick();
      if (wparam == 8) host->Guard([&] { host->ResumeLogin(); });
      if (wparam == 6) host->CompleteCacheJobs();
      if (wparam == 7) host->PollProxyMenu();
      if (wparam == 2) host->Guard([&] { host->PollProxy(); });
      if (wparam == 3) host->Guard([&] { host->PollGameTarget(); });
      if (wparam == 4) { KillTimer(window, 4); host->SaveLayout(); }
      if (wparam == 5) {
        KillTimer(window, 5);
        if (host->split_layout_pending_) { host->split_layout_pending_ = false;
          host->Guard([&] { if (host->split_.dragging() || host->helper_split_.dragging()) host->Layout(); }); }
      }
      return 0;
    }
    if (message == WM_GETMINMAXINFO) {
      auto info = reinterpret_cast<MINMAXINFO*>(lparam);
      MONITORINFO monitor{sizeof(monitor)};
      if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
        info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
      }
      info->ptMinTrackSize = {host->MinimumWindowWidth(), UiPixels(520, host->Dpi())}; return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(host->failed_ && !host->return_profile_ ? 3 : 0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  gbrowser::webview::CallbackGate callbacks_, lifecycle_callbacks_;
  bool draining_ = false, layout_applying_ = false;
  RunMode mode_;
  const wchar_t*& phase_;
  std::filesystem::path root_, payload_, data_, installation_state_;
  std::unique_ptr<gbrowser::webview::UserStateFile> cache_setting_;
  std::unique_ptr<gbrowser::webview::cache::Worker> resource_cache_;
  std::map<uint64_t, CachePending> cache_pending_;
  uint64_t cache_sequence_ = 0, cache_epoch_ = 0, cache_network_epoch_ = 0, cache_clear_id_ = 0;
  uint64_t cache_served_ = 0, cache_served_bytes_ = 0;
  unsigned cache_captures_ = 0, cache_jobs_ = 0;
  bool cache_enabled_ = true, cache_clearing_ = false, cache_initializing_ = false, cache_stopping_ = false;
  std::unique_ptr<gbrowser::webview::ProfileCatalogue> profiles_;
  std::unique_ptr<gbrowser::webview::ProfileDialog> profile_dialog_;
  std::string active_profile_, next_profile_;
  gbrowser::webview::DirectoryPins directories_;
  std::unique_ptr<gbrowser::webview::ComponentCatalogue> catalogue_;
  std::unique_ptr<gbrowser::webview::ComponentRegistrations> registrations_;
  gbrowser::webview::AcceptedComponent scripts_component_;
  std::unique_ptr<DataRootLock> data_lock_;
  std::unique_ptr<DataRootLock> application_lock_;
  gbrowser::webview::TimingLog timings_;
  std::vector<std::filesystem::path> sources_;
  std::vector<std::shared_ptr<const ExtensionPage>> pages_;
  void CaptureTick() noexcept {
    try {
      capture_.Tick(); // Sweep rolling retention even with no views, events, or MCP clients.
      for (auto& observer : capture_observers_) if (observer) observer->Tick();
      RefreshCooldown();
      const auto label = capture_.Label();
      if (label != capture_label_) { capture_label_ = label; RefreshProfileTitle(); }
    } catch (...) { capture_.Stop(); }
  }
  void ConfigureCooldown(size_t index) {
    if (!capture_observers_[index]) return;
    const auto generation = view_generations_[index];
    capture_observers_[index]->Listen(skill_enabled_, [this, index, generation](const gbrowser::webview::cooldown::Snapshot& snapshot) {
      if (closing_ || !CurrentView(index, generation)) return;
      skill_snapshots_[index] = skill_routes_[index].empty() ? gbrowser::webview::cooldown::Snapshot{} : snapshot;
      RefreshCooldown();
    }, [this, index, generation](const std::string& document) {
      if (closing_ || !CurrentView(index, generation) || skill_routes_[index].empty()) return false;
      const auto route = gbrowser::webview::cooldown::BattleRoute(document);
      return route.empty() || route == skill_routes_[index];
    });
  }
  void RefreshCooldown() {
    if (!skill_overlay_ && skill_enabled_ && window_ && !closing_) skill_overlay_ = std::make_unique<gbrowser::webview::cooldown::Overlay>(window_, theme_, kSkillCooldown);
    if (!skill_overlay_) return;
    if ((skill_view_ == kLeftGame && !panes_.dual) || (skill_view_ != kLeftGame && skill_view_ != active_))
      skill_view_ = gbrowser::webview::GameSurface(active_) ? active_ : 0;
    skill_overlay_->Update(skill_snapshots_[skill_view_], skill_view_ == kLeftGame);
    skill_overlay_->Align(skill_enabled_ && !closing_ && !failed_);
  }
  std::filesystem::path settings_root_;
  std::wstring profile_leaf_;
  std::unique_ptr<gbrowser::webview::transfer::BrowserTransfer> profile_transfer_;
  bool transfer_busy_ = false, transfer_close_requested_ = false;
  bool skill_enabled_ = false;
  size_t skill_view_ = 0;
  std::unique_ptr<gbrowser::webview::cooldown::Overlay> skill_overlay_;
  std::array<gbrowser::webview::cooldown::Snapshot, gbrowser::webview::TabSlots::kViewCount> skill_snapshots_;
  std::array<std::string, gbrowser::webview::TabSlots::kViewCount> skill_routes_;
  gbrowser::webview::capture::Session capture_;
  std::unique_ptr<gbrowser::webview::capture::Server> capture_server_;
  std::array<std::unique_ptr<gbrowser::webview::capture::Observer>, gbrowser::webview::TabSlots::kViewCount> capture_observers_;
  std::array<bool, gbrowser::webview::TabSlots::kViewCount> capture_game_{};
  std::wstring capture_label_;
  std::array<ComPtr<ICoreWebView2BrowserExtension>, 4> extensions_;
  std::array<View, gbrowser::webview::TabSlots::kViewCount> views_;
  std::array<uint64_t, gbrowser::webview::TabSlots::kViewCount> view_generations_{};
  std::array<uint64_t, gbrowser::webview::TabSlots::kViewCount> navigation_epochs_{};
  std::array<uint64_t, gbrowser::webview::TabSlots::kViewCount> favicon_epochs_{};
  std::array<bool, gbrowser::webview::TabSlots::kViewCount> favicon_loading_{};
  std::array<HWND, gbrowser::webview::TabSlots::kAuxCount> auxiliary_windows_{};
  std::array<std::shared_ptr<const ExtensionPage>, gbrowser::webview::TabSlots::kAuxCount> auxiliary_pages_{};
  gbrowser::webview::TabSlots tab_slots_;
  std::vector<size_t> tab_order_{0};
  std::array<std::wstring, gbrowser::webview::TabSlots::kViewCount> titles_{}, draft_text_{};
  std::array<bool, gbrowser::webview::TabSlots::kViewCount> drafts_{}, settings_tabs_{};
  gbrowser::webview::TabPointerClose tab_close_;
  gbrowser::webview::SplitPointerGesture split_;
  gbrowser::webview::SplitPointerGesture helper_split_;
  bool split_layout_pending_ = false;
  int drag_game_width_ = 0;
  unsigned drag_game_dpi_ = 96, drag_helper_share_ = 3300;
  unsigned panel_percent_ = 50;
  std::unique_ptr<gbrowser::webview::UserStateFile> window_file_;
  gbrowser::webview::WindowState window_state_;
  bool layout_ready_ = false, layout_failed_ = false, restoring_layout_ = false, maximized_ = false;
  size_t active_ = 0;
  bool updating_address_ = false;
  std::shared_ptr<const ExtensionPage> bound_tarou_;
  ComPtr<ICoreWebView2Environment> environment_;
  ComPtr<ICoreWebView2Profile7> profile_;
  HWND window_ = nullptr, address_ = nullptr, login_window_ = nullptr;
  const wchar_t* notice_pending_ = nullptr;
  bool notice_open_ = false;
  HWND tabs_ = nullptr, tabs_tip_ = nullptr, divider_ = nullptr;
  int tooltip_tab_ = -1;
  HFONT font_ = nullptr;
  std::array<HWND, 18> buttons_{};
  HANDLE browser_process_ = nullptr;
  UINT32 browser_pid_ = 0;
  std::unique_ptr<gbrowser::webview::ProxyPreference> proxy_preference_;
  std::optional<ProxyAction> restore_fallback_;
  std::string proxy_custom_name_;
  gbrowser::webview::ProxyProfileMenu proxy_menu_;
  uint64_t proxy_menu_request_=0,proxy_menu_generation_=0,proxy_menu_started_=0;
  bool proxy_menu_loading_=false,proxy_menu_polling_=false;
  bool restore_started_ = false, restore_probe_ = false, proxy_recorded_ = false;
  bool proxy_explicit_selection_ = false, proxy_uncertain_before_ = false;
  std::unique_ptr<ProxyInitialization> proxy_;
  uint64_t operation_ = 0, document_generation_ = 0;
  uint64_t proxy_started_ = 0;
  uint64_t target_generation_ = 0, target_started_ = 0;
  std::string target_source_;
  int bound_game_id_ = 0;
  int pending_ = 0, loaded_ = 0;
  bool closing_ = false, proxy_verified_ = false, failed_ = false;
  gbrowser::webview::PaneState panes_;
  std::unique_ptr<gbrowser::webview::UserStateFile> pane_file_;
  std::unique_ptr<gbrowser::webview::UserStateFile> tarou_window_file_;
  gbrowser::webview::WindowState tarou_window_state_{0, 0, 600, 800, 96, 50, false};
  bool arcarum_detached_ = false;
  std::array<int, gbrowser::webview::TabSlots::kViewCount> moba_css_{};
  std::array<int, gbrowser::webview::TabSlots::kViewCount> menu_css_{};
  int last_secondary_menu_width_ = 0;
  unsigned last_secondary_menu_dpi_ = 96;
  gbrowser::webview::DualTransition dual_transition_;
  gbrowser::webview::PaneState dual_previous_panes_;
  std::optional<RECT> single_window_;
  bool dual_positioning_ = false;
  std::array<bool, 2> pane_pending_{};
  std::pair<uint64_t, uint64_t> arcarum_pin_epoch_{UINT64_MAX, UINT64_MAX};
  uint64_t game_source_epoch_ = 0;
  std::wstring left_requested_source_;
  HWND right_frame_ = nullptr, left_frame_ = nullptr, arcarum_frame_ = nullptr, tarou_frame_ = nullptr;
  HWND arcarum_window_ = nullptr, arcarum_dock_button_ = nullptr, tarou_title_ = nullptr;
  HWND tarou_window_ = nullptr, helper_divider_ = nullptr;
  gbrowser::webview::UiTheme theme_;
  std::unique_ptr<gbrowser::webview::UserStateFile> appearance_file_;
  std::unique_ptr<gbrowser::webview::UserStateFile> layout_choice_file_;
  std::unique_ptr<gbrowser::webview::UserStateFile> loading_display_file_;
  bool hide_loading_ = false;
  bool browser_layout_ = false;
  bool controller_ready_ = false, polling_ = false, initialize_new_zero_ = false;
  bool proxy_timed_ = false;
  bool restart_gbf_ = false, return_profile_ = false;
  ULONGLONG target_polling_ = 0;
  bool target_rebind_pending_ = false;
  bool browser_started_ = false, browser_exited_ = false, login_pending_ = false, login_closing_ = false;
  uint64_t login_attempt_ = 0;
  gbrowser::webview::LoginResume login_resume_;
};

bool StartGameCandidate() noexcept {
  try {
  std::array<wchar_t, MAX_PATH> executable{};
  const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (!length || length >= executable.size()) return false;
  // This is the same compiled app's normal product entry point, no
  // shell, arbitrary URL/arguments, inherited handles or migrated profile.
  std::wstring command = L"\"" + std::wstring(executable.data()) + L"\"";
  const auto directory = std::filesystem::path(executable.data()).parent_path();
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
      directory.c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
  } catch (...) { return false; }
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
  const auto command = std::wstring_view(GetCommandLineW());
  int argument_count = 0;
  auto arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments && argument_count >= 2 && std::wstring_view(arguments[1]) == L"--capture-mcp") {
    DWORD browser_pid = 0;
    if (argument_count == 4 && std::wstring_view(arguments[2]) == L"--browser-pid") {
      wchar_t* end = nullptr; const auto parsed = wcstoul(arguments[3], &end, 10);
      if (end && !*end && parsed) browser_pid = parsed;
    }
    LocalFree(arguments); return gbrowser::webview::capture::RunMcp(browser_pid);
  }
  LocalFree(arguments);
  const auto plan = gbrowser::webview::NativeLaunchPlan(command);
  // The candidate has not ported CEF's ancestry-aware fixed-site policy. Do
  // not silently reinterpret its fixed-mode switch as open networking.
  if (!plan) return 2;
  if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 3;
  int result = 3;
  bool restart = false;
  const wchar_t* phase = L"启动候选";
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::string requested;
    do {
    std::string next;
    {
    Host host(*plan, phase, requested); host.Create(instance, show);
    MSG message{};
    bool recovering = false;
    for (;;) {
      // A message-pump failure or unrelated WM_QUIT is not runtime exit.
      // Keep the host/pins alive and pump its pending completion callbacks.
      if (recovering && host.ShutdownComplete()) { result = 3; break; }
      if (recovering) {
        if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
          continue;
        }
      } else {
        const auto received = GetMessageW(&message, nullptr, 0, 0);
        if (received == -1) { recovering = true; host.MessageLoopFailed(); continue; }
      }
      if (message.message == WM_QUIT) {
        if (!recovering && host.ShutdownComplete()) { result = static_cast<int>(message.wParam); break; }
        if (!recovering) { recovering = true; host.MessageLoopFailed(); }
        continue;
      }
      if (!host.PreTranslate(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    restart = result == 0 && host.RestartIntoGame();
    if (result == 0 && !restart) next = host.NextProfile();
    }
    requested = std::move(next);
    } while (!requested.empty());
  } catch (const std::exception&) {
    const auto explanation = std::wstring(L"无法启动，停止阶段：") + phase +
        L"。\n若刚切换资料失败，关闭本提示后重新打开程序会返回上次成功资料。\n请从自己的普通PowerShell或文件资源管理器启动。\n若提示运行库，请安装微软稳定版WebView2 Runtime：\nhttps://developer.microsoft.com/microsoft-edge/webview2/\n\n现有CEF及资料保留，不绕过资料目录核验。";
    MessageBoxW(nullptr, explanation.c_str(), L"StarryFish 启动未完成", MB_OK | MB_ICONERROR);
  }
  RoUninitialize();
  // Host destruction above releases every runtime/source/application lock.
  // If shutdown was unknown or failed, execution never reaches a relaunch.
  if (restart && !StartGameCandidate()) {
    MessageBoxW(nullptr, L"游戏候选未能启动；资料已保留，可稍后重新打开。", L"StarryFish", MB_OK | MB_ICONERROR);
    return 3;
  }
  return result;
}
