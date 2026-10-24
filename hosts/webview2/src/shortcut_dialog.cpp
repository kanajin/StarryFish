#include "shortcut_dialog.h"
#include "core.h"

namespace gbrowser::webview {
namespace {
constexpr const wchar_t* labels[]{L"打开 1 号位角色", L"打开 2 号位角色", L"打开 3 号位角色", L"打开 4 号位角色",
    L"刷新当前游戏页", L"战斗 Back", L"Quick 召唤", L"打开召唤石面板",
    L"点击第 1 个技能", L"点击第 2 个技能", L"点击第 3 个技能", L"点击第 4 个技能",
    L"治疗药水（绿色）", L"群体回复药水（蓝色）", L"回复药（Elixir）",
    L"打开回复道具", L"攻击 / 取消攻击", L"FC（含 OK 确认）", L"奥义发动 / 温存", L"打开战斗 Menu", L"活动药水 1", L"活动药水 2", L"活动药水 3"};
int Group(size_t i) { return i<4 ? 1 : i>=8&&i<12 ? 2 : (i>=12&&i<15)||i>=20 ? 3 : 0; }
constexpr const wchar_t* group_names[]{L"通用操作",L"角色选择",L"角色技能",L"回复道具"};
std::wstring KeyName(unsigned key) {
  if (!key) return L"未绑定";
  if (key == VK_SHIFT) return L"Shift";
  if (key == VK_CONTROL) return L"Ctrl";
  if (key == VK_MENU) return L"Alt";
  if (key == VK_LWIN) return L"Left Win";
  if (key == VK_RWIN) return L"Right Win";
  if (key == VK_OEM_3) return L"` / ~";
  if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return std::wstring(1, static_cast<wchar_t>(key));
  if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return L"Num " + std::to_wstring(key - VK_NUMPAD0);
  const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
  LONG bits = static_cast<LONG>((scan & 255) << 16);
  if (scan & 0xff00) bits |= 1 << 24;
  wchar_t text[80]{};
  if (GetKeyNameTextW(bits, text, 80)) return text;
  return L"Key " + std::to_wstring(key);
}
}
void ShortcutDialog::Open(HWND owner, characters::Bindings keys, bool enabled) {
  if (window_) { SetForegroundWindow(window_); return; }
  keys_ = keys; enabled_ = enabled; capture_ = -1; tracker_ = {}; group_ = 0;
  WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = Procedure;
  cls.lpszClassName = L"StarryFish.Shortcuts"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&cls);
  const auto dpi = GetDpiForWindow(owner); RECT r{}; GetWindowRect(owner, &r);
  const int width=UiPixels(620,dpi),height=UiPixels(650,dpi);
  int left=r.left+UiPixels(50,dpi),top=r.top+UiPixels(50,dpi);
  MONITORINFO monitor{sizeof(monitor)};
  if (GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)) {
    left=(std::max)(monitor.rcWork.left,(std::min)(static_cast<LONG>(left),monitor.rcWork.right-width));
    top=(std::max)(monitor.rcWork.top,(std::min)(static_cast<LONG>(top),monitor.rcWork.bottom-height));
  }
  window_ = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"StarryFish · 键盘快捷键",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, left, top, width, height, owner, nullptr, cls.hInstance, this);
  if (!window_) throw std::runtime_error("无法打开快捷键配置");
  try {
    const auto make = [&](const wchar_t* type, const wchar_t* text, int id, DWORD style) {
      HWND control = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
          window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
      if (!control) throw std::runtime_error("无法创建快捷键控件");
      if (std::wstring(type) == L"BUTTON") UiTheme::AttachButton(control);
      return control;
    };
    toggle_ = make(L"BUTTON", L"", 100, BS_OWNERDRAW | WS_TABSTOP);
    hint_ = make(L"STATIC", L"1–4 切换角色，QWER 点击技能；道具界面用数字键。\n点击键位录入单键；Fn / 组合键不支持。", 0, 0);
    for (size_t i=0;i<groups_.size();++i) groups_[i]=make(L"BUTTON",group_names[i],110+static_cast<int>(i),BS_OWNERDRAW|WS_TABSTOP);
    for (size_t i = 0; i < keys_.size(); ++i) {
      switches_[i] = make(L"BUTTON", L"", 400 + static_cast<int>(i), BS_OWNERDRAW | WS_TABSTOP);
      labels_[i] = make(L"STATIC", labels[i], 0, 0);
      keys_buttons_[i] = make(L"BUTTON", L"", 200 + static_cast<int>(i), BS_OWNERDRAW | WS_TABSTOP);
      clear_[i] = make(L"BUTTON", L"清除", 300 + static_cast<int>(i), BS_OWNERDRAW | WS_TABSTOP);
    }
    reset_ = make(L"BUTTON", L"恢复默认", 101, BS_OWNERDRAW | WS_TABSTOP);
    save_button_ = make(L"BUTTON", L"保存", 102, BS_OWNERDRAW | WS_TABSTOP);
    cancel_ = make(L"BUTTON", L"取消", 103, BS_OWNERDRAW | WS_TABSTOP);
    theme_.Window(window_); Refresh(); Layout(); if (IsWindowVisible(owner)) { ShowWindow(window_, SW_SHOW); SetFocus(toggle_); }
  } catch (...) { Close(); throw; }
}
void ShortcutDialog::Close() {
  if (window_) { auto old = window_; window_ = nullptr; DestroyWindow(old); }
  toggle_ = hint_ = reset_ = save_button_ = cancel_ = nullptr;
  labels_ = {}; keys_buttons_ = {}; clear_ = {}; switches_ = {}; groups_ = {}; capture_ = -1;
}
void ShortcutDialog::Layout() {
  if (!window_ || !toggle_) return;
  const auto dpi = GetDpiForWindow(window_); const auto px = [dpi](int n) { return UiPixels(n, dpi); };
  auto next = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  if (!next) throw std::runtime_error("无法创建快捷键字体");
  RECT r{}; GetClientRect(window_, &r); const int width = r.right;
  const auto move = [&](HWND h, int x, int y, int w, int height) {
    MoveWindow(h, px(x), px(y), w, px(height), TRUE); SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
  };
  move(toggle_, 20, 18, width-px(40), 36); move(hint_,20,62,width-px(40),52);
  for (size_t i=0;i<groups_.size();++i) move(groups_[i],20+static_cast<int>(i)*142,120,px(132),32);
  int row=0;
  for (size_t i=0;i<keys_.size();++i) {
    const bool show=Group(i)==group_;
    for (HWND h : {labels_[i],keys_buttons_[i],clear_[i],switches_[i]}) ShowWindow(h,show?SW_SHOW:SW_HIDE);
    if (!show) continue;
    const int y=172+row++*38;
    move(switches_[i],20,y,px(50),30); move(labels_[i],82,y+5,px(245),25);
    move(keys_buttons_[i],330,y,width-px(430),30); move(clear_[i],530,y,px(55),30);
  }
  move(reset_,20,548,px(112),34); move(save_button_,360,548,px(100),34); move(cancel_,478,548,px(100),34);
  if (font_) DeleteObject(font_); font_ = next;
}
void ShortcutDialog::Refresh() {
  SetWindowTextW(toggle_, enabled_ ? L"✓  启用键盘快捷键（保存后生效）" : L"启用键盘快捷键：关闭（保存后生效）");
  for (size_t i = 0; i < keys_.size(); ++i) {
    SetWindowTextW(switches_[i], keys_.enabled[i] ? L"开" : L"关");
    SetWindowTextW(keys_buttons_[i], capture_ == static_cast<int>(i) ? L"请按一个键…" : KeyName(keys_[i]).c_str());
  }
  for (size_t i=0;i<groups_.size();++i) SetWindowTextW(groups_[i], (std::wstring(group_==static_cast<int>(i)?L"● ":L"")+group_names[i]).c_str());
  InvalidateRect(window_, nullptr, TRUE);
}
void ShortcutDialog::Action(int id) {
  if (id >= 200 && id < 223) { capture_ = id - 200; tracker_ = {}; Refresh(); return; }
  capture_ = -1;
  if (id>=110 && id<114) {
    group_=id-110; Layout();
    SetWindowTextW(hint_, group_==3 ? L"1–3 普通药水；4–6 按活动药水编号 1–3 对应。\n只点击道具，确认和角色目标仍由你选择。" :
        group_==2 ? L"默认 QWER 点击技能；1–4 仍切换角色。\n技能与角色选择不可使用相同按键。" :
        L"开 / 关可单独控制每项；点击键位录入单键。\n总开关、单项开关及键位均保存，重启后恢复。");
  }
  if (id>=400 && id<423) {
    const size_t i=static_cast<size_t>(id-400); keys_.enabled[i]=!keys_.enabled[i];
    if (characters::Conflict(keys_,i,keys_[i])>=0) {keys_.enabled[i]=false;SetWindowTextW(hint_,L"同一界面已有相同按键；请先修改键位或关闭冲突项。");}
  }
  if (id >= 300 && id < 323) keys_[static_cast<size_t>(id - 300)] = 0;
  if (id == 100) enabled_ = !enabled_;
  if (id == 101) keys_ = characters::Defaults();
  if (id == 102) { save_(keys_, enabled_); Close(); return; }
  if (id == 103) { Close(); return; }
  Refresh();
}
void ShortcutDialog::Record(unsigned key) {
  if (!characters::AllowedKey(key)) { SetWindowTextW(hint_, L"这个键无法绑定。请选择键盘上的另一个单键。"); return; }
  const auto existing = characters::Conflict(keys_,static_cast<size_t>(capture_),key);
  if (existing >= 0) {
    SetWindowTextW(hint_, (std::wstring(L"同一界面按键已用于「") + labels[existing] + L"」。请改键或关闭冲突项。").c_str()); return;
  }
  keys_[static_cast<size_t>(capture_)] = key; capture_ = -1;
  SetWindowTextW(hint_, L"已录入。点击保存后生效；键位会保留到下次启动。\n总开关及每项开关会保留到下次启动。"); Refresh();
}
bool ShortcutDialog::Translate(MSG& message) {
  if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
  if (capture_ >= 0 && (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ||
      message.message == WM_KEYUP || message.message == WM_SYSKEYUP)) {
    const bool up = message.message == WM_KEYUP || message.message == WM_SYSKEYUP;
    const unsigned key = characters::NormalizeKey(static_cast<unsigned>(message.wParam));
    const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
    const unsigned picked = tracker_.Feed(key, up, modified);
    if (picked) Record(picked);
    else if (!up && !characters::Modifier(key) && modified) SetWindowTextW(hint_, L"暂不支持组合键。请松开所有键，再按一个单键。");
    return true;
  }
  return IsDialogMessageW(window_, &message) != FALSE;
}
LRESULT CALLBACK ShortcutDialog::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto self = reinterpret_cast<ShortcutDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<ShortcutDialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, wparam, lparam);
  try {
    if (message == WM_ERASEBKGND) { RECT r{}; GetClientRect(window, &r); self->theme_.Background(reinterpret_cast<HDC>(wparam), r); return 1; }
    if (message == WM_CTLCOLORSTATIC) { auto dc = reinterpret_cast<HDC>(wparam); SetTextColor(dc, self->theme_.colors().ink);
      SetBkColor(dc, self->theme_.colors().background); return reinterpret_cast<LRESULT>(self->theme_.Control(dc, false)); }
    if (message == WM_DRAWITEM) { self->theme_.Button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam), self->font_); return TRUE; }
    if (message == WM_CLOSE) { self->Close(); return 0; }
    if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) { self->capture_ = -1; self->Refresh(); return 0; }
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) { self->Action(LOWORD(wparam)); return 0; }
    if (message == WM_SIZE) { self->Layout(); return 0; }
    if (message == WM_DPICHANGED) { const auto r = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(window, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE); self->Layout(); return 0; }
  } catch (const std::exception& error) { MessageBoxW(window, Utf8ToWide(error.what()).c_str(), L"快捷键设置未保存", MB_OK | MB_ICONWARNING); }
  return DefWindowProcW(window, message, wparam, lparam);
}
}
