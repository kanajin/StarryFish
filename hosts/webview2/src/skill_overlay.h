#pragma once
#include "skill_cooldown.h"
#include "ui_theme.h"
#include <windowsx.h>
#include <stdexcept>

namespace gbrowser::webview::cooldown {
constexpr int kStripWidth = 500, kStripHeight = 54;
inline int ResizePercent(int initial, SIZE size, POINT delta) {
  const double length = static_cast<double>(size.cx) * size.cx + static_cast<double>(size.cy) * size.cy;
  if (length <= 0) return (std::clamp)(initial, 50, 200);
  const double change = (static_cast<double>(delta.x) * size.cx + static_cast<double>(delta.y) * size.cy) / length;
  return static_cast<int>(std::lround((std::clamp)(initial * (1.0 + change), 50.0, 200.0)));
}
inline unsigned FittedDpi(unsigned owner_dpi, int percent, int width, int height) {
  const auto desired = MulDiv(static_cast<int>(owner_dpi), (std::clamp)(percent, 50, 200), 100);
  return static_cast<unsigned>((std::max)(1, (std::min)({desired, (std::max)(1, width) * 96 / kStripWidth,
      (std::max)(1, height) * 96 / kStripHeight})));
}
inline void PaintStrip(HDC dc, const UiTheme& theme, unsigned dpi, const Snapshot& snapshot, bool secondary, bool hover) {
  const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
  const auto rect = [&](int left, int top, int right, int bottom) { return RECT{scale(left), scale(top), scale(right), scale(bottom)}; };
  const auto& colors = theme.colors();
  theme.Fill(dc, rect(0, 0, kStripWidth, kStripHeight), colors.background);
  theme.Round(dc, rect(0, 0, kStripWidth, kStripHeight), scale(9), colors.background, colors.border);
  const auto font = CreateFontW(-scale(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  const auto small_font = CreateFontW(-scale(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  theme.Text(dc, font, secondary ? L"副窗" : L"主窗", rect(5, 8, 47, 26), colors.ink);
  theme.Text(dc, small_font, L"技能 CD", rect(4, 27, 48, 45), colors.muted);
  for (size_t position = 0; position < 4; ++position) {
    const int left = 54 + static_cast<int>(position) * 104;
    if (position) theme.Fill(dc, rect(left - 6, 8, left - 5, 46), colors.border);
    const auto label = std::to_wstring(position + 1) + L" 号位";
    theme.Text(dc, small_font, label.c_str(), rect(left, 4, left + 92, 20), colors.muted, DT_LEFT);
    for (size_t skill = 0; skill < 4; ++skill) {
      const int value = snapshot.cells[position][skill];
      const int cell = left + static_cast<int>(skill) * 24;
      const auto bounds = rect(cell, 24, cell + 22, 46);
      const auto fill = value == 0 ? (theme.dark() ? RGB(191, 233, 224) : RGB(255, 255, 255)) : colors.selected;
      theme.Round(dc, bounds, scale(3), fill, value == 0 ? colors.accent : colors.border);
      const auto text = value == kMissing ? L"×" : value == kUnknown ? L"?" : value == 0 ? L"" : std::to_wstring(value);
      HFONT compact = nullptr;
      if (text.size() > 3) compact = CreateFontW(-scale(text.size() == 4 ? 9 : text.size() == 5 ? 7 : 6), 0, 0, 0,
          FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      theme.Text(dc, compact ? compact : value > 99 ? small_font : font, text.c_str(), bounds, value < 0 ? colors.muted : colors.ink);
      if (compact) DeleteObject(compact);
      if (value == 0) theme.Fill(dc, rect(cell + 7, 40, cell + 15, 42), RGB(42, 134, 119));
    }
  }
  if (hover) {
    theme.Round(dc, rect(kStripWidth - 29, 5, kStripWidth - 5, 29), scale(5), colors.hover, colors.hover);
    theme.Icon(dc, UiIcon::Close, rect(kStripWidth - 25, 9, kStripWidth - 9, 25), colors.ink);
  }
  const auto pen = CreatePen(PS_SOLID, (std::max)(1, scale(1)), colors.muted);
  const auto old_pen = SelectObject(dc, pen);
  for (int inset : {6, 11}) {
    MoveToEx(dc, scale(kStripWidth - 6 - inset), scale(kStripHeight - 7), nullptr);
    LineTo(dc, scale(kStripWidth - 7), scale(kStripHeight - 6 - inset));
  }
  SelectObject(dc, old_pen); DeleteObject(pen);
  DeleteObject(small_font); DeleteObject(font);
}
class Overlay {
 public:
  Overlay(HWND owner, UiTheme& theme, int command) : owner_(owner), theme_(theme), command_(command) {
    WNDCLASSW type{}; type.lpfnWndProc = Proc; type.hInstance = GetModuleHandleW(nullptr);
    type.style = CS_DBLCLKS;
    type.lpszClassName = L"StarryFish.SkillCooldown"; type.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("cooldown window class unavailable");
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"技能 CD · 拖动移动，右上角关闭",
        WS_POPUP, 0, 0, kStripWidth, kStripHeight, owner_, nullptr, type.hInstance, this);
    if (!window_) throw std::runtime_error("cooldown overlay unavailable");
    theme_.Window(window_);
    tip_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, type.hInstance, nullptr);
    if (tip_) {
      TOOLINFOW tool{sizeof(tool)}; tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.hwnd = window_;
      tool.uId = reinterpret_cast<UINT_PTR>(window_);
      tool.lpszText = const_cast<wchar_t*>(L"按 1–4 技能排列 · 数字：剩余回合 · 亮格：CD 为 0（不代表一定可用）\n×：无此技能 · ?：等待可信战斗数据\n拖动移动；拖动右下角等比例缩放（50%–200%），双击右下角恢复默认\n靠近右上角可关闭；三点 → 功能可重新开启");
      SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
      SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, 420);
      SendMessageW(tip_, TTM_SETDELAYTIME, TTDT_INITIAL, 800);
    }
  }
  ~Overlay() { if (tip_) DestroyWindow(tip_); if (window_) DestroyWindow(window_); }
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;
  void Update(const Snapshot& snapshot, bool secondary) {
    if (snapshot.cells == snapshot_.cells && secondary == secondary_ && dark_ == theme_.dark()) return;
    snapshot_ = snapshot; secondary_ = secondary; dark_ = theme_.dark();
    theme_.Window(window_); InvalidateRect(window_, nullptr, FALSE);
  }
  void Align(bool visible) {
    if (!visible || IsIconic(owner_) || !IsWindowVisible(owner_) || !IsWindowEnabled(owner_)) {
      dragging_ = resizing_ = close_down_ = false;
      if (GetCapture() == window_) ReleaseCapture();
      ShowWindow(window_, SW_HIDE); return;
    }
    RECT client{}; GetClientRect(owner_, &client);
    owner_dpi_ = GetDpiForWindow(owner_);
    const unsigned dpi = FittedDpi(owner_dpi_, percent_, client.right, client.bottom);
    if (dpi_ != dpi) { dpi_ = dpi; InvalidateRect(window_, nullptr, FALSE); }
    const int width = Scale(kStripWidth), height = Scale(kStripHeight);
    offset_.x = (std::clamp)(offset_.x, 0L, (std::max)(0L, MulDiv(client.right - width, 96, static_cast<int>(owner_dpi_)) * 1L));
    offset_.y = (std::clamp)(offset_.y, 0L, (std::max)(0L, MulDiv(client.bottom - height, 96, static_cast<int>(owner_dpi_)) * 1L));
    POINT point{MulDiv(offset_.x, static_cast<int>(owner_dpi_), 96), MulDiv(offset_.y, static_cast<int>(owner_dpi_), 96)};
    ClientToScreen(owner_, &point);
    RECT current{}; GetWindowRect(window_, &current);
    if (current.left != point.x || current.top != point.y || current.right - current.left != width || current.bottom - current.top != height)
      SetWindowPos(window_, nullptr, point.x, point.y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!IsWindowVisible(window_)) ShowWindow(window_, SW_SHOWNOACTIVATE);
  }
 private:
  int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
  bool CloseHit(LPARAM point) const {
    return GET_X_LPARAM(point) >= Scale(kStripWidth - 29) && GET_X_LPARAM(point) < Scale(kStripWidth - 4) &&
        GET_Y_LPARAM(point) >= 0 && GET_Y_LPARAM(point) <= Scale(31);
  }
  bool ResizeHit(LPARAM point) const {
    return GET_X_LPARAM(point) >= Scale(kStripWidth - 24) && GET_X_LPARAM(point) < Scale(kStripWidth) &&
        GET_Y_LPARAM(point) >= Scale(kStripHeight - 22) && GET_Y_LPARAM(point) < Scale(kStripHeight);
  }
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_SETCURSOR) {
      POINT point{}; GetCursorPos(&point); ScreenToClient(window, &point);
      const auto location = MAKELPARAM(point.x, point.y);
      SetCursor(LoadCursorW(nullptr, self->ResizeHit(location) ? IDC_SIZENWSE : self->CloseHit(location) ? IDC_HAND : IDC_SIZEALL)); return TRUE;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
      RECT bounds{}; GetClientRect(window, &bounds);
      const auto buffer = CreateCompatibleDC(dc); const auto bitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom);
      if (buffer && bitmap) {
        const auto old = SelectObject(buffer, bitmap);
        PaintStrip(buffer, self->theme_, self->dpi_, self->snapshot_, self->secondary_, self->hover_);
        BitBlt(dc, 0, 0, bounds.right, bounds.bottom, buffer, 0, 0, SRCCOPY); SelectObject(buffer, old);
      } else PaintStrip(dc, self->theme_, self->dpi_, self->snapshot_, self->secondary_, self->hover_);
      if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer); EndPaint(window, &paint); return 0;
    }
    if (message == WM_MOUSEMOVE) {
      const bool hover = GET_X_LPARAM(lparam) >= self->Scale(kStripWidth - 36) && GET_Y_LPARAM(lparam) < self->Scale(32);
      if (hover != self->hover_) { self->hover_ = hover; InvalidateRect(window, nullptr, FALSE); }
      TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
      if (self->resizing_) {
        POINT point{}; GetCursorPos(&point);
        self->percent_ = ResizePercent(self->resize_percent_, self->resize_size_, {point.x - self->resize_start_.x, point.y - self->resize_start_.y});
        self->Align(true);
      } else if (self->dragging_) {
        POINT point{}; GetCursorPos(&point); ScreenToClient(self->owner_, &point);
        self->offset_ = {MulDiv(point.x - self->anchor_.x, 96, static_cast<int>(self->owner_dpi_)),
            MulDiv(point.y - self->anchor_.y, 96, static_cast<int>(self->owner_dpi_))}; self->Align(true);
      }
      return 0;
    }
    if (message == WM_MOUSELEAVE) { self->hover_ = false; InvalidateRect(window, nullptr, FALSE); return 0; }
    if (message == WM_LBUTTONDOWN) {
      if (self->CloseHit(lparam)) { self->close_down_ = true; SetCapture(window); }
      else if (self->ResizeHit(lparam)) {
        self->resizing_ = true; GetCursorPos(&self->resize_start_);
        RECT bounds{}; GetWindowRect(window, &bounds);
        self->resize_size_ = {bounds.right - bounds.left, bounds.bottom - bounds.top};
        self->resize_percent_ = MulDiv(static_cast<int>(self->dpi_), 100, static_cast<int>(self->owner_dpi_));
        SetCapture(window);
      }
      else { self->dragging_ = true; self->anchor_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; SetCapture(window); }
      return 0;
    }
    if (message == WM_LBUTTONDBLCLK && self->ResizeHit(lparam)) {
      self->resizing_ = false; self->percent_ = 100; ReleaseCapture(); self->Align(true); return 0;
    }
    if (message == WM_LBUTTONUP) {
      const bool close = self->close_down_ && self->CloseHit(lparam);
      if (self->resizing_) self->percent_ = (std::clamp)(MulDiv(static_cast<int>(self->dpi_), 100, static_cast<int>(self->owner_dpi_)), 50, 200);
      self->dragging_ = self->resizing_ = self->close_down_ = false; ReleaseCapture();
      if (close) PostMessageW(self->owner_, WM_COMMAND, static_cast<WPARAM>(self->command_), 0);
      return 0;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) self->dragging_ = self->resizing_ = self->close_down_ = false;
    if (message == WM_CLOSE) { PostMessageW(self->owner_, WM_COMMAND, static_cast<WPARAM>(self->command_), 0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  HWND owner_ = nullptr, window_ = nullptr, tip_ = nullptr;
  UiTheme& theme_;
  int command_;
  unsigned dpi_ = 96, owner_dpi_ = 96;
  int percent_ = 100, resize_percent_ = 100;
  SIZE resize_size_{};
  POINT resize_start_{};
  Snapshot snapshot_;
  bool secondary_ = false, hover_ = false, dragging_ = false, resizing_ = false, close_down_ = false, dark_ = false;
  POINT offset_{100, 80}, anchor_{};
};
}
