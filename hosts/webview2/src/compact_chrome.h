#pragma once
#include "ui_interaction.h"
#include <array>

namespace gbrowser::webview {
// Layout 1: a single title/toolbar row and a fixed right icon rail.
// Keep geometry shared with isolated native checks; no WebView is needed.
struct CompactToolbar {
  Rect address, copy, tabs, profile, minimize, maximize, close, add;
};
struct BrowserToolbar {
  Rect tabs, add, minimize, maximize, close, address, copy, reload;
  std::array<Rect, 7> functions; // Tarou, Arcarum, proxy, dual, management, profile, menu.
};
inline BrowserToolbar BrowserToolbarGeometry(int width, unsigned dpi, size_t tab_count) {
  const auto px = [dpi](int n) { return UiPixels(n, dpi); };
  BrowserToolbar out{};
  const int window_x = (std::max)(0, width - px(108));
  out.minimize = {window_x, 0, px(36), px(36)};
  out.maximize = {window_x + px(36), 0, px(36), px(36)};
  out.close = {window_x + px(72), 0, px(36), px(36)};
  const int tab_room = (std::max)(0, window_x - px(88));
  const int tab_width = (std::min)(tab_room, static_cast<int>((std::min)(tab_count, size_t{16})) * px(180) + px(4));
  out.tabs = {px(8), px(4), tab_width, px(34)};
  out.add = {px(12) + tab_width, px(4), px(30), px(30)};
  out.reload = {px(6), px(46), px(30), px(32)};
  const int function_x = width - px(244);
  for (size_t i = 0; i < out.functions.size(); ++i)
    out.functions[i] = {function_x + px(static_cast<int>(i) * 34), px(46), px(32), px(32)};
  out.address = {px(46), px(46), (std::max)(0, function_x - px(86)), px(32)};
  out.copy = {function_x - px(36), px(46), px(30), px(32)};
  return out;
}
inline int LeftChromeInset(unsigned dpi) {
  // Layout 1 outer trim: one third narrower than the original 9-DIP gutter.
  return UiPixels(6, dpi);
}
inline CompactToolbar ToolbarGeometry(int width, unsigned dpi, size_t tab_count = 1) {
  const auto px = [dpi](int n) { return UiPixels(n, dpi); };
  const int button = px(36), gap = px(6);
  const int profile = width >= px(800) ? px(190) : width >= px(600) ? px(130) : px(36);
  const int right = (std::max)(0, width - px(4) - 3 * button);
  const int profile_x = (std::max)(0, right - profile - gap);
  const int address = (std::clamp)(profile_x - px(142), px(100), px(236));
  const int tabs_x = px(8) + address + gap + button + gap;
  const int tabs_width = (std::min)((std::max)(0, profile_x - gap - tabs_x - px(36)),
      static_cast<int>((std::min)(tab_count, size_t{16})) * px(160) + px(4));
  return {{px(8), px(6), address, px(32)},
      {px(8) + address + gap, px(4), button, button},
      {tabs_x, px(6), tabs_width, px(34)},
      {profile_x, px(4), profile, button}, {right, px(4), button, button},
      {right + button, px(4), button, button}, {right + 2 * button, px(4), button, button},
      {tabs_x + tabs_width + px(4), px(6), px(30), px(30)}};
}
struct HelperHeader { Rect title, pin, close; };
inline HelperHeader HeaderGeometry(Rect panel, unsigned dpi) {
  const auto px = [dpi](int n) { return UiPixels(n, dpi); };
  const int button = (std::min)(px(32), (std::max)(0, panel.width - px(12)) / 2);
  const int close_x = panel.x + panel.width - px(4) - button;
  const int pin_x = close_x - px(4) - button;
  return {{panel.x, panel.y, (std::max)(0, pin_x - panel.x - px(4)), px(40)},
      {pin_x, panel.y + px(4), button, px(32)}, {close_x, panel.y + px(4), button, px(32)}};
}
inline void FrameClientRect(LPARAM parameter, bool full, bool maximized, RECT work, int border) {
  auto& rect = full ? reinterpret_cast<NCCALCSIZE_PARAMS*>(parameter)->rgrc[0] :
      *reinterpret_cast<RECT*>(parameter);
  if (maximized) { rect = work; return; }
  // Retain native side/bottom resize borders outside WebView child windows.
  // Only the caption is removed. Both NCCALCSIZE payload forms must agree.
  rect.left += border; rect.right -= border; rect.bottom -= border;
}
// Coordinates are client-local, including the negative edges of other monitors.
inline LRESULT FrameEdge(POINT point, RECT client, int edge, bool maximized) {
  if (maximized) return HTCLIENT;
  const bool left = point.x < edge, right = point.x >= client.right - edge;
  const bool top = point.y < edge, bottom = point.y >= client.bottom - edge;
  if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
  if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
  return left ? HTLEFT : right ? HTRIGHT : HTCLIENT;
}
}
