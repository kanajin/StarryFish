#include "include/cef_app.h"
#include "include/base/cef_compiler_specific.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "platform_paths.h"
#include "diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr char kHomeUrl[] = "https://game.granbluefantasy.jp/#mypage";

class BrowserClient final : public CefClient,
                            public CefDisplayHandler,
                            public CefLoadHandler,
                            public CefKeyboardHandler,
                            public CefLifeSpanHandler {
 public:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                     CefEventHandle, bool*) override {
    // Browser zoom only. Game automation and synthetic input are not connected.
    if (event.type != KEYEVENT_RAWKEYDOWN || !(event.modifiers & EVENTFLAG_CONTROL_DOWN) ||
        (event.modifiers & (EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN))) return false;
    const int key = event.windows_key_code;  // CEF's cross-platform virtual key.
    const bool more = key == 187 || key == 107;  // OEM_PLUS / ADD
    const bool less = key == 189 || key == 109;  // OEM_MINUS / SUBTRACT
    if (!more && !less && key != '0') return false;
    if ((event.modifiers & EVENTFLAG_SHIFT_DOWN) && !more) return false;
    constexpr std::array<double, 12> scales{0.5, 0.67, 0.8, 0.9, 1.0, 1.1,
                                          1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
    const double current = std::pow(1.2, browser->GetHost()->GetZoomLevel());
    double target = 1.0;
    if (more) {
      const auto next = std::upper_bound(scales.begin(), scales.end(), current + 0.001);
      target = next == scales.end() ? scales.back() : *next;
    } else if (less) {
      const auto next = std::lower_bound(scales.begin(), scales.end(), current - 0.001);
      target = next == scales.begin() ? scales.front() : *std::prev(next);
    }
    browser->GetHost()->SetZoomLevel(std::log(target) / std::log(1.2));
    if (CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("diagnostic-page") == "input") {
      const auto view = CefBrowserView::GetForBrowser(browser);
      if (view && view->GetWindow())
        view->GetWindow()->SetTitle("StarryFish 输入与缩放检查 — " +
                                    std::to_string(static_cast<int>(std::lround(target * 100))) + "%");
    }
    return true;
  }

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool loading,
                            bool, bool) override {
    if (!loading && CefCommandLine::GetGlobalCommandLine()->HasSwitch("diagnostic-page"))
      starryfish::linux_host::ReportDiagnosticPage(browser);
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    browsers_.push_back(browser);
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override {
    CEF_REQUIRE_UI_THREAD();
    const auto browser_view = CefBrowserView::GetForBrowser(browser);
    if (browser_view) {
      const auto window = browser_view->GetWindow();
      if (window) {
        window->SetTitle("StarryFish Linux — " + title.ToString());
      }
    }
  }

  bool DoClose(CefRefPtr<CefBrowser>) override {
    CEF_REQUIRE_UI_THREAD();
    return false;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    browsers_.erase(
        std::remove_if(browsers_.begin(), browsers_.end(),
                       [browser](const CefRefPtr<CefBrowser>& candidate) {
                         return candidate->IsSame(browser);
                       }),
        browsers_.end());
    if (browsers_.empty()) {
      CefQuitMessageLoop();
    }
  }

 private:
  std::vector<CefRefPtr<CefBrowser>> browsers_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};

class BrowserWindowDelegate final : public CefWindowDelegate {
 public:
  explicit BrowserWindowDelegate(CefRefPtr<CefBrowserView> browser_view)
      : browser_view_(browser_view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("StarryFish Linux — development");
    window->AddChildView(browser_view_);
    window->Show();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    browser_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    const auto browser = browser_view_ ? browser_view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(1280, 820);
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  CefRefPtr<CefBrowserView> browser_view_;
  IMPLEMENT_REFCOUNTING(BrowserWindowDelegate);
};

class BrowserViewDelegate final : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserViewDelegate);
};

class StarryFishApp final : public CefApp,
                            public CefBrowserProcessHandler {
 public:
  StarryFishApp() : client_(new BrowserClient) {}

  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    if (process_type.empty()) {
      command_line->AppendSwitchWithValue("ozone-platform", "x11");
      // Chrome bootstrap otherwise discovers system-installed extensions and
      // can start their native messaging hosts, even in a fresh XDG profile.
      // Linux helpers will use explicitly reviewed host adapters instead.
      command_line->AppendSwitch("disable-extensions");
    }
  }

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  CefRefPtr<CefClient> GetDefaultClient() override { return client_; }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    CefBrowserSettings settings;
    const auto command = CefCommandLine::GetGlobalCommandLine();
    const auto initial_url = command->HasSwitch("diagnostic-page")
        ? starryfish::linux_host::DiagnosticUrl(command->GetSwitchValue("diagnostic-page"))
        : (command->HasSwitch("smoke-test") ? "about:blank" : kHomeUrl);
    const auto view = CefBrowserView::CreateBrowserView(
        client_, initial_url, settings, nullptr, nullptr,
        new BrowserViewDelegate());
    CefWindow::CreateTopLevelWindow(new BrowserWindowDelegate(view));
  }

 private:
  CefRefPtr<BrowserClient> client_;
  IMPLEMENT_REFCOUNTING(StarryFishApp);
};

}  // namespace

NO_STACK_PROTECTOR int main(int argc, char* argv[]) {
  CefMainArgs main_args(argc, argv);
  CefRefPtr<StarryFishApp> app(new StarryFishApp);
  // Child processes must not prepare directories or compete for the profile lock.
  const int subprocess_result = CefExecuteProcess(main_args, app, nullptr);
  if (subprocess_result >= 0) {
    return subprocess_result;
  }

  try {
    const auto command = CefCommandLine::CreateCommandLine();
    command->InitFromArgv(argc, argv);
    CefCommandLine::SwitchMap switches;
    command->GetSwitches(switches);
    CefCommandLine::ArgumentList arguments;
    command->GetArguments(arguments);
    if (!arguments.empty() || (command->HasSwitch("smoke-test") &&
                              command->HasSwitch("diagnostic-page"))) return 2;
    for (const auto& option : switches) {
      const bool smoke = option.first == "smoke-test" && option.second.empty();
      const bool diagnostic = option.first == "diagnostic-page" &&
          (option.second == "input" || option.second == "gpu" || option.second == "sandbox");
      if (!smoke && !diagnostic) {
        std::cerr << "Supported: --smoke-test or --diagnostic-page=input|gpu|sandbox\n";
        return 2;
      }
    }
    const auto paths = starryfish::linux_host::ResolvePaths([](const char* key) {
      const auto* value = std::getenv(key);
      return value ? std::string(value) : std::string();
    });
    starryfish::linux_host::PreparePaths(paths);
    const starryfish::linux_host::ProfileLock profile_lock(paths.data);
    CefSettings settings;
    // Alloy style uses modern Chrome bootstrap and owns the browser chrome.
    settings.no_sandbox = false;
    // Chromium persistent profile data belongs in DATA_HOME, not disposable CACHE_HOME.
    CefString(&settings.root_cache_path) = (paths.data / "UserData").string();
    CefString(&settings.cache_path) = (paths.data / "UserData/Default").string();
    settings.log_severity = LOGSEVERITY_DISABLE;
    if (!CefInitialize(main_args, settings, app, nullptr)) {
      return CefGetExitCode();
    }

    CefRunMessageLoop();
    CefShutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "StarryFish Linux startup stopped: " << error.what() << '\n';
    return 3;
  }
}
