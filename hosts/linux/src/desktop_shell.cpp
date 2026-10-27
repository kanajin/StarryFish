#include "desktop_shell.h"
#include "platform_paths.h"
#include "shell_model.h"
#include "include/cef_app.h"
#include "include/cef_color_ids.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_parser.h"
#include "include/cef_menu_model.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace starryfish::linux_host {
namespace {
enum Command { Reload = 1, Add, Minimize, Maximize, Close, Theme, Menu, Title,
               CloseTab, NextTab, PreviousTab, Address, LayoutOne, LayoutTwo,
               Light, Dark, Back, Forward, FirstTab = 1000 };
CefRect Bounds(const shell::Rect& r) { return {r.x, r.y, r.width, r.height}; }
bool WebUrl(const std::string& value) {
  CefURLParts parts;
  if (value.size() > 8192 || !CefParseURL(value, parts)) return false;
  const auto scheme = CefString(&parts.scheme).ToString();
  return (scheme == "https" || scheme == "http") && parts.host.length > 0 &&
         parts.username.length == 0 && parts.password.length == 0;
}
std::string Origin(const std::string& value) {
  CefURLParts parts;
  return CefParseURL(value, parts) ? CefString(&parts.origin).ToString() : std::string();
}

class ViewDelegate final : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
 private:
  IMPLEMENT_REFCOUNTING(ViewDelegate);
};

class ImageResult final : public CefDownloadImageCallback {
 public:
  explicit ImageResult(std::function<void(CefRefPtr<CefImage>)> complete)
      : complete_(std::move(complete)) {}
  void OnDownloadImageFinished(const CefString&, int status, CefRefPtr<CefImage> image) override {
    if (image && (status == 0 || status == 200) && image->GetWidth() <= 32 && image->GetHeight() <= 32)
      complete_(image);
  }
 private:
  std::function<void(CefRefPtr<CefImage>)> complete_;
  IMPLEMENT_REFCOUNTING(ImageResult);
};

class ShellWindow final : public DesktopShell, public CefWindowDelegate,
                          public CefMenuButtonDelegate, public CefTextfieldDelegate,
                          public CefMenuModelDelegate {
 public:
  ShellWindow(CefRefPtr<CefClient> client, std::string initial, std::filesystem::path config)
      : client_(client), initial_(std::move(initial)), config_(std::move(config)) { LoadAppearance(); }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    window_->SetTitle("StarryFish Linux");
    content_ = CefPanel::CreatePanel(nullptr);
    window_->AddChildView(content_);
    address_ = CefTextfield::CreateTextfield(this);
    address_->SetAccessibleName("网址"); address_->SetPlaceholderText("输入网址");
    address_->SetFontList("sans-serif, 14px");
    window_->AddChildView(address_);
    Button(Reload, "↻", "刷新当前标签 (F5)"); Button(Add, "+", "新建标签 (Ctrl+T)");
    Button(Minimize, "−", "最小化"); Button(Maximize, "□", "最大化／还原");
    Button(Close, "×", "关闭窗口"); Button(Theme, "◐", "切换天蓝／群青主题");
    Button(Title, "StarryFish", "StarryFish")->SetEnabled(false);
    menu_ = CefMenuButton::CreateMenuButton(this, "⋯"); menu_->SetID(Menu);
    menu_->SetTooltipText("菜单"); menu_->SetAccessibleName("菜单");
    window_->AddChildView(menu_);
    window_->SetAccelerator(Add, 'T', false, true, false, true);
    window_->SetAccelerator(CloseTab, 'W', false, true, false, true);
    window_->SetAccelerator(NextTab, 9, false, true, false, true);
    window_->SetAccelerator(PreviousTab, 9, true, true, false, true);
    window_->SetAccelerator(Address, 'L', false, true, false, true);
    window_->SetAccelerator(Menu, 'F', false, false, true, true);
    window_->SetAccelerator(Reload, 116, false, false, false, true);
    window_->SetAccelerator(Back, 37, false, false, true, true);
    window_->SetAccelerator(Forward, 39, false, false, true, true);
    AddTab(initial_);
    ApplyTheme(); Place(); window_->Show();
    if (auto browser = CurrentBrowser()) browser->GetHost()->SetFocus(true);
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    window_ = nullptr; content_ = nullptr; address_ = nullptr; menu_ = nullptr;
    buttons_.clear(); tabs_.clear(); client_ = nullptr;
    CefQuitMessageLoop();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (tabs_.empty()) return true;
    closing_window_ = true;
    std::vector<CefRefPtr<CefBrowser>> browsers;
    for (auto& tab : tabs_) {
      if (auto browser = tab.browser) browsers.push_back(browser);
      else tab.close_pending = true;
    }
    for (const auto& browser : browsers) browser->GetHost()->TryCloseBrowser();
    return false;  // Keep the window and profile alive through OnBeforeClose.
  }
  void DialogClosed() override { closing_window_ = false; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return view->AsPanel() && view->AsPanel()->AsWindow() ? CefSize(1100, 820) : CefSize();
  }
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override {
    return view->AsPanel() && view->AsPanel()->AsWindow() ? CefSize(640, 360) : CefSize();
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect&) override { Place(); }
  void OnLayoutChanged(CefRefPtr<CefView> view, const CefRect&) override {
    if (window_ && view->IsSame(window_)) Place();
  }
  void OnThemeColorsChanged(CefRefPtr<CefWindow>, bool) override { ApplyTheme(false); }
  void OnThemeChanged(CefRefPtr<CefView>) override { ApplyTheme(false); }

  void OnButtonPressed(CefRefPtr<CefButton> button) override { Perform(button->GetID()); }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command) override { Perform(command); return true; }
  bool OnKeyEvent(CefRefPtr<CefTextfield> field, const CefKeyEvent& event) override {
    if (event.type != KEYEVENT_RAWKEYDOWN || event.windows_key_code != 13 ||
        (event.modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN))) return false;
    auto url = field->GetText().ToString();
    if (url.find(":") == std::string::npos) url = "https://" + url;
    if (url == "about:blank" || WebUrl(url)) {
      if (auto browser = CurrentBrowser()) {
        browser->GetMainFrame()->LoadURL(url); browser->GetHost()->SetFocus(true);
      }
    } else {
      field->SetAccessibleName("网址无效：仅支持 HTTP、HTTPS 和 about:blank");
    }
    return true;
  }
  void OnMenuButtonPressed(CefRefPtr<CefMenuButton> button, const CefPoint& point,
                            CefRefPtr<CefMenuButtonPressedLock>) override {
    auto menu = CefMenuModel::CreateMenuModel(this);
    menu->AddItem(Back, "后退"); menu->AddItem(Forward, "前进"); menu->AddItem(Reload, "刷新");
    if (auto browser = CurrentBrowser()) {
      menu->SetEnabled(Back, browser->CanGoBack()); menu->SetEnabled(Forward, browser->CanGoForward());
    }
    menu->AddSeparator();
    menu->AddRadioItem(LayoutOne, "布局 1 · 右侧功能栏", 1);
    menu->AddRadioItem(LayoutTwo, "布局 2 · 双排顶部", 1);
    menu->SetChecked(two_rows_ ? LayoutTwo : LayoutOne, true);
    menu->AddRadioItem(Light, "浅色天蓝", 2); menu->AddRadioItem(Dark, "深色群青", 2);
    menu->SetChecked(dark_ ? Dark : Light, true);
    menu->AddSeparator(); menu->AddItem(Add, "新建标签"); menu->AddItem(CloseTab, "关闭当前标签");
    menu->SetEnabled(CloseTab, active_ != 1); menu->AddItem(Close, "退出");
    button->ShowMenu(menu, point, CEF_MENU_ANCHOR_TOPRIGHT);
  }
  void ExecuteCommand(CefRefPtr<CefMenuModel>, int command, cef_event_flags_t) override { Perform(command); }

  void BrowserCreated(CefRefPtr<CefBrowser> browser) override {
    if (auto* tab = Find(browser)) {
      tab->browser = browser;
      if (tab->close_pending || closing_window_) browser->GetHost()->CloseBrowser(false);
      else if (tab->id == active_) tab->view->RequestFocus();
    }
  }
  bool BrowserReadyToClose(CefRefPtr<CefBrowser> browser) override {
    auto* tab = Find(browser);
    if (!tab) return false;
    if (!tab->tearing_down) {
      tab->tearing_down = true;
      // Detach only this browser view after its unload approval. Returning true
      // prevents CEF from closing the entire tabbed top-level window.
      CefPostTask(TID_UI, base::BindOnce([](CefRefPtr<ShellWindow> self, CefRefPtr<CefBrowser> target) {
        if (auto* item = self->Find(target); item && item->view) {
          self->content_->RemoveChildView(item->view);
          item->view = nullptr;
        }
      }, CefRefPtr<ShellWindow>(this), browser));
    }
    return true;
  }
  void BrowserClosed(CefRefPtr<CefBrowser> browser) override {
    auto* tab = Find(browser);
    if (!tab) return;
    const auto id = tab->id; tab->epoch.Invalidate();
    if (tab->view) content_->RemoveChildView(tab->view);
    window_->RemoveChildView(tab->button);
    std::erase_if(tabs_, [id](const Tab& item) { return item.id == id; });
    if (tabs_.empty()) { window_->Close(); return; }
    if (active_ == id) active_ = tabs_.front().id;
    Select(active_); Place();
  }
  void TitleChanged(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    if (auto* tab = Find(browser)) {
      tab->title = title.ToString().substr(0, 1024);
      tab->button->SetTooltipText(tab->title); tab->button->SetAccessibleName(tab->title);
      if (tab->id == active_ && window_) window_->SetTitle("StarryFish — " + tab->title);
    }
  }
  void AddressChanged(CefRefPtr<CefBrowser> browser, const CefString& url) override {
    if (auto* tab = Find(browser); tab && tab->id == active_ && address_) {
      address_->SetText(url.ToString().starts_with("data:") ? "about:blank" : url);
      address_->SetAccessibleName("网址");
    }
  }
  void NavigationStarted(CefRefPtr<CefBrowser> browser) override {
    if (auto* tab = Find(browser)) {
      const auto frame = browser->GetMainFrame();
      tab->ticket = tab->epoch.Navigate(frame->GetIdentifier(), Origin(frame->GetURL()));
      tab->button->SetImage(CEF_BUTTON_STATE_NORMAL, nullptr); tab->button->SetText("◇");
    }
  }
  bool AllowNavigation(CefRefPtr<CefBrowser> browser, const std::string& url) override {
    const bool allowed = WebUrl(url) || url == "about:blank" || url == initial_;
    if (allowed) if (auto* tab = Find(browser)) tab->epoch.Invalidate();
    return allowed;
  }
  void FaviconChanged(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& urls) override {
    auto* tab = Find(browser);
    if (!tab || urls.empty() || !WebUrl(urls.front())) return;
    const auto id = tab->id; const auto ticket = tab->ticket;
    browser->GetHost()->DownloadImage(urls.front(), true, 16, false,
        new ImageResult([self = CefRefPtr<ShellWindow>(this), id, ticket](CefRefPtr<CefImage> image) {
          if (!self->window_) return;
          for (auto& item : self->tabs_) if (item.id == id && item.epoch.Current(ticket)) {
            item.button->SetText(""); item.button->SetImage(CEF_BUTTON_STATE_NORMAL, image);
          }
        }));
  }

 private:
  struct Tab {
    int id;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefLabelButton> button;
    shell::DocumentEpoch epoch;
    shell::DocumentTicket ticket;
    std::string title;
    bool close_pending = false;
    bool tearing_down = false;
    CefRefPtr<CefBrowser> browser;
  };
  CefRefPtr<CefLabelButton> Button(int id, const char* text, const char* tooltip) {
    auto button = CefLabelButton::CreateLabelButton(this, text);
    button->SetID(id); button->SetTooltipText(tooltip); button->SetAccessibleName(tooltip);
    button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    button->SetFontList("sans-serif, 16px"); button->SetInkDropEnabled(true);
    window_->AddChildView(button); buttons_.push_back(button); return button;
  }
  Tab* Find(CefRefPtr<CefBrowser> browser) {
    const auto view = CefBrowserView::GetForBrowser(browser);
    for (auto& tab : tabs_)
      if ((tab.browser && tab.browser->IsSame(browser)) ||
          (view && tab.view && tab.view->IsSame(view))) return &tab;
    return nullptr;
  }
  CefRefPtr<CefBrowser> CurrentBrowser() {
    for (auto& tab : tabs_) if (tab.id == active_) return tab.browser;
    return nullptr;
  }
  void AddTab(const std::string& url) {
    if (!window_ || tabs_.size() >= 8 || closing_window_ ||
        next_id_ > std::numeric_limits<int>::max() - FirstTab) return;
    const int id = next_id_;
    auto button = CefLabelButton::CreateLabelButton(this, "◇");
    button->SetID(FirstTab + id); button->SetTooltipText(id == 1 ? "GBF · 固定标签" : "新标签");
    button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER); button->SetFontList("sans-serif, 16px");
    button->SetMinimumSize({0, 0});
    CefBrowserSettings settings;
    auto view = CefBrowserView::CreateBrowserView(client_, url, settings, nullptr, nullptr, new ViewDelegate());
    if (!view) return;
    ++next_id_;
    tabs_.push_back({id, view, button, shell::DocumentEpoch(1, static_cast<uint64_t>(id)), {}, "新标签"});
    window_->AddChildView(button); content_->AddChildView(view);
    Select(id); Place();
  }
  void Select(int id) {
    active_ = id;
    for (auto& tab : tabs_) {
      if (tab.view) tab.view->SetVisible(tab.id == active_);
      if (tab.id == active_) {
        window_->SetTitle("StarryFish — " + tab.title);
        if (auto browser = tab.browser) {
          AddressChanged(browser, browser->GetMainFrame()->GetURL());
          if (tab.view) tab.view->RequestFocus();
        }
      }
    }
    ApplyTheme();
  }
  void Perform(int command) {
    if (!window_) return;
    if (command >= FirstTab) { Select(command - FirstTab); return; }
    auto browser = CurrentBrowser();
    switch (command) {
      case Add: AddTab("about:blank"); break;
      case CloseTab:
        for (auto& tab : tabs_) if (tab.id == active_ && tab.id != 1) {
          if (browser) browser->GetHost()->CloseBrowser(false); else tab.close_pending = true;
          break;
        }
        break;
      case NextTab: case PreviousTab:
        for (size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == active_) {
          Select(tabs_[(i + (command == NextTab ? 1 : tabs_.size() - 1)) % tabs_.size()].id); break;
        }
        break;
      case Address: address_->RequestFocus(); address_->SelectAll(false); break;
      case Menu: menu_->TriggerMenu(); break;
      case Reload: if (browser) browser->Reload(); break;
      case Back: if (browser) browser->GoBack(); break;
      case Forward: if (browser) browser->GoForward(); break;
      case Minimize: window_->Minimize(); break;
      case Maximize: if (window_->IsMaximized()) window_->Restore(); else window_->Maximize(); break;
      case Close: window_->Close(); break;
      case Theme: dark_ = !dark_; ApplyTheme(); SaveAppearance(); break;
      case Light: case Dark: dark_ = command == Dark; ApplyTheme(); SaveAppearance(); break;
      case LayoutOne: case LayoutTwo: two_rows_ = command == LayoutTwo; Place(); SaveAppearance(); break;
      default: break;
    }
  }
  void LoadAppearance() {
    try {
      const auto contents = ReadPrivateFile(config_, "shell-settings.json", 4096);
      if (!contents) return;
      const auto value = CefParseJSON(*contents, JSON_PARSER_RFC);
      if (!value || value->GetType() != VTYPE_DICTIONARY) throw std::runtime_error("Invalid appearance");
      const auto object = value->GetDictionary();
      if (object->GetSize() != 3 || object->GetType("schema") != VTYPE_INT || object->GetInt("schema") != 1 ||
          object->GetType("dark") != VTYPE_BOOL || object->GetType("layout") != VTYPE_INT ||
          (object->GetInt("layout") != 1 && object->GetInt("layout") != 2))
        throw std::runtime_error("Unsupported appearance");
      dark_ = object->GetBool("dark"); two_rows_ = object->GetInt("layout") == 2;
    } catch (const std::exception&) {
      settings_writable_ = false;
      std::cerr << "Shell appearance is invalid or unsafe; original retained, using defaults\n";
    }
  }
  void SaveAppearance() {
    if (!settings_writable_) return;
    auto object = CefDictionaryValue::Create();
    object->SetInt("schema", 1); object->SetBool("dark", dark_); object->SetInt("layout", two_rows_ ? 2 : 1);
    auto value = CefValue::Create(); value->SetDictionary(object);
    try { AtomicWrite(config_, "shell-settings.json", CefWriteJSON(value, JSON_WRITER_DEFAULT)); }
    catch (const std::exception&) { std::cerr << "Shell appearance could not be saved\n"; }
  }
  void ApplyTheme(bool notify = true) {
    if (!window_ || !address_ || !menu_) return;
    const auto colors = shell::BluePalette(dark_);
    window_->SetBackgroundColor(colors.background); content_->SetBackgroundColor(colors.background);
    address_->SetBackgroundColor(colors.surface);
    window_->SetThemeColor(CEF_ColorTextfieldBackground, colors.surface);
    window_->SetThemeColor(CEF_ColorTextfieldForeground, colors.ink);
    window_->SetThemeColor(CEF_ColorTextfieldSelectionBackground, colors.selected);
    window_->SetThemeColor(CEF_ColorTextfieldSelectionForeground, colors.ink);
    for (auto& button : buttons_) {
      button->SetBackgroundColor(colors.background); button->SetEnabledTextColors(colors.ink);
      button->SetTextColor(CEF_BUTTON_STATE_DISABLED, colors.muted);
    }
    menu_->SetBackgroundColor(colors.background); menu_->SetEnabledTextColors(colors.ink);
    for (auto& tab : tabs_) {
      tab.button->SetBackgroundColor(tab.id == active_ ? colors.selected : colors.surface);
      tab.button->SetEnabledTextColors(colors.ink);
    }
    if (notify) window_->ThemeChanged();
  }
  void Place() {
    if (!window_ || !address_ || !menu_ || placing_) return;
    placing_ = true;
    const auto size = window_->GetSize();
    const auto layout = shell::Layout(size.width, size.height, two_rows_, tabs_.size());
    for (auto& button : buttons_) {
      shell::Rect bounds;
      switch (button->GetID()) {
        case Reload: bounds = layout.reload; break; case Add: bounds = layout.add; break;
        case Minimize: bounds = layout.minimize; break; case Maximize: bounds = layout.maximize; break;
        case Close: bounds = layout.close; break; case Theme: bounds = layout.theme; break;
        case Title: bounds = layout.title; break; default: break;
      }
      button->SetBounds(Bounds(bounds));
    }
    address_->SetBounds(Bounds(layout.address)); menu_->SetBounds(Bounds(layout.menu));
    content_->SetBounds(Bounds(layout.content));
    const auto icons = shell::IconTabs(layout.tabs.width, layout.tabs.height, tabs_.size());
    for (size_t i = 0; i < tabs_.size(); ++i) {
      auto bounds = icons[i]; bounds.x += layout.tabs.x; bounds.y += layout.tabs.y;
      tabs_[i].button->SetBounds(Bounds(bounds));
      if (tabs_[i].view) tabs_[i].view->SetBounds({0, 0, layout.content.width, layout.content.height});
    }
    CefDraggableRegion drag; drag.bounds = Bounds(layout.title); drag.draggable = true;
    window_->SetDraggableRegions({drag});
    placing_ = false;
  }

  CefRefPtr<CefClient> client_;
  std::string initial_;
  std::filesystem::path config_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefTextfield> address_;
  CefRefPtr<CefMenuButton> menu_;
  std::vector<CefRefPtr<CefLabelButton>> buttons_;
  std::vector<Tab> tabs_;
  int next_id_ = 1, active_ = 0;
  bool dark_ = false, two_rows_ = false, placing_ = false, closing_window_ = false;
  bool settings_writable_ = true;
  IMPLEMENT_REFCOUNTING(ShellWindow);
};
}  // namespace

CefRefPtr<DesktopShell> CreateDesktopShell(CefRefPtr<CefClient> client,
                                         const std::string& initial_url,
                                         const std::filesystem::path& config) {
  CefRefPtr<ShellWindow> shell = new ShellWindow(client, initial_url, config);
  CefWindow::CreateTopLevelWindow(shell);
  return shell;
}
}  // namespace starryfish::linux_host
