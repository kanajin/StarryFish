#include "include/cef_app.h"
#include "include/base/cef_compiler_specific.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "platform_paths.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr char kHomeUrl[] = "https://game.granbluefantasy.jp/#mypage";

class BrowserClient final : public CefClient,
                            public CefDisplayHandler,
                            public CefLifeSpanHandler {
 public:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

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
    if (process_type.empty()) command_line->AppendSwitchWithValue("ozone-platform", "x11");
  }

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  CefRefPtr<CefClient> GetDefaultClient() override { return client_; }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    CefBrowserSettings settings;
    const auto command = CefCommandLine::GetGlobalCommandLine();
    const auto view = CefBrowserView::CreateBrowserView(
        client_, command->HasSwitch("smoke-test") ? "about:blank" : kHomeUrl, settings, nullptr, nullptr,
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
    for (const auto& option : switches) {
      if (option.first != "smoke-test" || !option.second.empty()) {
        std::cerr << "Only --smoke-test is supported by this development host\n";
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
