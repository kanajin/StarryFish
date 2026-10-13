#include "profile_dialog.h"
#include "core.h"
#include <algorithm>
#include <stdexcept>

namespace gbrowser::webview {
namespace {
constexpr int kCreate = 201, kRename = 202, kUse = 203, kDelete = 204, kRecycle = 205, kRestore = 206, kDone = 207;
std::wstring Text(HWND window) {
  const auto length = GetWindowTextLengthW(window); std::wstring result(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(window, result.data(), length + 1); result.resize(static_cast<size_t>(length)); return result;
}
}
ProfileDialog::ProfileDialog(ProfileCatalogue& catalogue, std::string active, UiTheme& theme, std::function<void(std::string)> select)
    : catalogue_(catalogue), theme_(theme), active_(std::move(active)), select_(std::move(select)) {}
ProfileDialog::~ProfileDialog() { Close(); if (font_) DeleteObject(font_); }
bool ProfileDialog::Translate(MSG& message) const { return window_ && IsDialogMessageW(window_, &message); }
void ProfileDialog::Open(HWND owner) {
  if (window_) { SetForegroundWindow(window_); return; }
  owner_ = owner;
  WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"GbrowserLocalProfiles";
  cls.lpfnWndProc = Procedure; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
  auto dpi = GetDpiForWindow(owner); if (!dpi) dpi = 96;
  RECT parent{}; GetWindowRect(owner, &parent);
  window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"StarryFish · 用户资料",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, parent.left + UiPixels(40, dpi), parent.top + UiPixels(40, dpi),
      UiPixels(540, dpi), UiPixels(410, dpi), owner, nullptr, cls.hInstance, this);
  if (!window_) throw std::runtime_error("无法打开资料窗口");
  theme_.Window(window_);
  try { CreateControls(); Refresh(active_); Layout(); EnableWindow(owner_, FALSE); ShowWindow(window_, SW_SHOW); SetFocus(list_); }
  catch (...) { Close(); throw; }
}
void ProfileDialog::Close() {
  if (!window_) return;
  const auto window = window_; window_ = nullptr;
  EnableWindow(owner_, TRUE); DestroyWindow(window);
  list_ = name_ = hint_ = nullptr; buttons_ = {};
}
void ProfileDialog::CreateControls() {
  const auto instance = GetModuleHandleW(nullptr);
  list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
      0, 0, 10, 10, window_, nullptr, instance, nullptr);
  name_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
      0, 0, 10, 10, window_, nullptr, instance, nullptr);
  hint_ = CreateWindowW(L"STATIC", L"每份资料的登录态、扩展设置和代理选择独立。切换会正常关闭所有页面。", WS_CHILD | WS_VISIBLE,
      0, 0, 10, 10, window_, nullptr, instance, nullptr);
  constexpr const wchar_t* labels[]{L"新建", L"重命名", L"切换", L"删除", L"已删除", L"恢复", L"关闭"};
  for (size_t i = 0; i < buttons_.size(); ++i)
    buttons_[i] = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCreate + i)), instance, nullptr);
  if (!list_ || !name_ || !hint_ || std::any_of(buttons_.begin(), buttons_.end(), [](HWND value) { return !value; }))
    throw std::runtime_error("无法创建资料控件");
  for (auto button : buttons_) UiTheme::AttachButton(button);
  SendMessageW(name_, EM_SETLIMITTEXT, 64, 0);
}
void ProfileDialog::Layout() {
  if (!window_ || !list_) return;
  auto dpi = GetDpiForWindow(window_); if (!dpi) dpi = 96;
  const auto next = CreateFontW(-UiPixels(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  if (!next) throw std::runtime_error("无法创建资料字体");
  for (HWND control : {list_, name_, hint_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
  for (auto button : buttons_) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
  if (font_) DeleteObject(font_); font_ = next;
  RECT client{}; GetClientRect(window_, &client);
  const int margin = UiPixels(16, dpi), width = (std::max)(1L, client.right - 2 * margin);
  const int name_y = (std::max)(margin + UiPixels(100, dpi), static_cast<int>(client.bottom) - UiPixels(124, dpi));
  MoveWindow(hint_, margin, margin, width, UiPixels(40, dpi), TRUE);
  MoveWindow(list_, margin, margin + UiPixels(48, dpi), width, name_y - margin - UiPixels(56, dpi), TRUE);
  MoveWindow(name_, margin, name_y, width, UiPixels(28, dpi), TRUE);
  const int button_width = (width - 3 * UiPixels(6, dpi)) / 4;
  for (size_t i = 0; i < buttons_.size(); ++i)
    MoveWindow(buttons_[i], margin + static_cast<int>(i % 4) * (button_width + UiPixels(6, dpi)),
        name_y + UiPixels(36 + static_cast<int>(i / 4) * 34, dpi), button_width, UiPixels(28, dpi), TRUE);
}
std::string ProfileDialog::Selection() const {
  const auto row = SendMessageW(list_, LB_GETCURSEL, 0, 0);
  if (row == LB_ERR || static_cast<size_t>(row) >= rows_.size()) throw std::runtime_error("请先选择资料");
  return rows_[static_cast<size_t>(row)];
}
void ProfileDialog::Refresh(const std::string& selected) {
  SendMessageW(list_, LB_RESETCONTENT, 0, 0); rows_.clear();
  for (const auto& entry : catalogue_.state().entries) if (entry.retired == recycled_) {
    rows_.push_back(entry.id); auto label = entry.name + (entry.id == active_ ? L"（当前）" : L"");
    if (SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) == LB_ERR)
      throw std::runtime_error("无法刷新资料列表");
  }
  const auto found = std::find(rows_.begin(), rows_.end(), selected);
  if (!rows_.empty()) SendMessageW(list_, LB_SETCURSEL, found == rows_.end() ? 0 : found - rows_.begin(), 0);
  SetWindowTextW(buttons_[4], recycled_ ? L"现有资料" : L"已删除");
  EnableWindow(buttons_[0], !recycled_); EnableWindow(buttons_[2], !recycled_);
  EnableWindow(buttons_[3], !recycled_); EnableWindow(buttons_[5], recycled_);
  SetWindowTextW(name_, rows_.empty() ? L"" : catalogue_.Find(Selection(), true).name.c_str());
}
void ProfileDialog::Action(int action) {
  if (action == kDone) { Close(); return; }
  if (action == kRecycle) { recycled_ = !recycled_; Refresh(); return; }
  if (action == kCreate) { const auto id = catalogue_.Create(Text(name_)); Refresh(id); return; }
  const auto id = Selection();
  if (action == kRename) { catalogue_.Rename(id, Text(name_)); Refresh(id); return; }
  if (action == kRestore) { catalogue_.Restore(id); Refresh(); return; }
  if (action == kDelete) {
    if (id == active_) throw std::runtime_error("不能删除当前资料，请先切换");
    if (MessageBoxW(window_, L"将这份资料移入已删除列表？\n登录态和扩展数据保留，可从“已删除”恢复。",
        L"删除资料", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) { catalogue_.Retire(id, active_); Refresh(); }
    return;
  }
  if (action == kUse && id != active_ && !catalogue_.Find(id).retired &&
      MessageBoxW(window_, L"将正常关闭所有标签、登录窗和助手，再进入所选资料。\n未提交的表单不会恢复；窗口布局保留。是否切换？",
          L"切换资料", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
    Close(); select_(id);
  }
}
LRESULT CALLBACK ProfileDialog::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto dialog = reinterpret_cast<ProfileDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    dialog = static_cast<ProfileDialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
  }
  if (!dialog) return DefWindowProcW(window, message, wparam, lparam);
  try {
    if (message == WM_ERASEBKGND) { RECT rect{}; GetClientRect(window, &rect); dialog->theme_.Background(reinterpret_cast<HDC>(wparam), rect); return 1; }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
      return reinterpret_cast<LRESULT>(dialog->theme_.Control(reinterpret_cast<HDC>(wparam), message != WM_CTLCOLORSTATIC));
    if (message == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT*>(lparam)->CtlType == ODT_BUTTON) {
      dialog->theme_.Button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam), dialog->font_); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { dialog->Close(); return 0; }
    if (message == WM_SIZE) { dialog->Layout(); return 0; }
    if (message == WM_DPICHANGED) {
      const auto bounds = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
          bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE); dialog->Layout(); return 0;
    }
    if (message == WM_GETMINMAXINFO) {
      const auto dpi = GetDpiForWindow(window);
      reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {UiPixels(500, dpi), UiPixels(380, dpi)}; return 0;
    }
    if (message == WM_COMMAND) {
      if (reinterpret_cast<HWND>(lparam) == dialog->list_ && HIWORD(wparam) == LBN_SELCHANGE)
        SetWindowTextW(dialog->name_, dialog->catalogue_.Find(dialog->Selection(), true).name.c_str());
      else if (HIWORD(wparam) == BN_CLICKED) dialog->Action(LOWORD(wparam));
      return 0;
    }
  } catch (const std::exception& error) {
    const auto text = Utf8ToWide(error.what());
    MessageBoxW(window, text.c_str(), L"资料操作未完成", MB_OK | MB_ICONWARNING);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}
