#pragma once
#include "core.h"
#include "tab_slots.h"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace gbrowser::webview {
// WINDOWPLACEMENT uses workspace coordinates. Translate the monitor's work
// rectangle into that same space before clamping, including negative monitors
// and taskbars on the top/left; mixing screen coordinates makes windows creep.
inline RECT VisiblePlacement(RECT normal, RECT monitor, RECT work, int width, int height) {
  const int offset_x = work.left - monitor.left, offset_y = work.top - monitor.top;
  work.left -= offset_x; work.right -= offset_x;
  work.top -= offset_y; work.bottom -= offset_y;
  width = (std::clamp)(width, 1, static_cast<int>(work.right - work.left));
  height = (std::clamp)(height, 1, static_cast<int>(work.bottom - work.top));
  normal.left = (std::clamp)(normal.left, work.left, work.right - width);
  normal.top = (std::clamp)(normal.top, work.top, work.bottom - height);
  normal.right = normal.left + width; normal.bottom = normal.top + height;
  return normal;
}
// The CEF shell keeps its original layout contract. This host has no tab
// action buttons, and constrains split widths in pixels rather than CEF's
// historical 25--60 percent panel range.
inline WindowLayout HostWindowLayout(int width, int height, bool visible, unsigned dpi, unsigned percent) {
  if (!percent || percent >= 100) throw std::runtime_error("Invalid split proportion");
  auto result = ComputeWindowLayout(width, height, visible, dpi);
  result.tabs.width = (std::max)(0, width - result.tabs.x - UiPixels(8, dpi));
  result.add = result.close = {};
  auto& browser = result.content.browser;
  auto& panel = result.content.tarou;
  const int body = result.content.rail.x;
  const int minimum = (std::min)(body / 2, UiPixels(260, dpi));
  const int pane = visible ? (std::clamp)(static_cast<int>(static_cast<int64_t>(body) * percent / 100),
      minimum, body - minimum) : 0;
  browser.width = body - pane;
  panel.x = body - pane; panel.width = pane;
  const int gutter = visible ? (std::min)(UiPixels(6, dpi), pane) : 0;
  result.tarou_divider = {panel.x, panel.y, gutter, panel.height};
  result.tarou_close = {(std::max)(0, body - UiPixels(70, dpi)), panel.y + UiPixels(8, dpi),
      (std::min)(UiPixels(60, dpi), body), (std::min)(UiPixels(26, dpi), panel.height)};
  return result;
}

// A native hit index can be reused after an asynchronous close. Retain the
// controller generation through down/up; modal/capture loss cancels it.
class TabPointerClose {
 public:
  void Down(std::optional<TabSlots::Ticket> hit) { pressed_ = hit; }
  std::optional<TabSlots::Ticket> Up(std::optional<TabSlots::Ticket> hit) {
    const auto pressed = pressed_; pressed_.reset();
    if (!pressed || !hit || pressed->index != hit->index || pressed->generation != hit->generation) return {};
    return pressed;
  }
  void Cancel() { pressed_.reset(); }
 private:
  std::optional<TabSlots::Ticket> pressed_;
};
class SplitPointerGesture {
 public:
  bool Begin(int x, int value, int minimum, int maximum) {
    if (dragging_ || minimum < 0 || maximum < minimum) return false;
    anchor_ = x; initial_ = value_ = value;
    minimum_ = minimum; maximum_ = maximum;
    dragging_ = true; return true;
  }
  int Move(int x) {
    if (dragging_) value_ = (std::clamp)(initial_ + x - anchor_, minimum_, maximum_);
    return value_;
  }
  int Cancel() { dragging_ = false; return value_ = initial_; }
  void Finish() { dragging_ = false; }
  bool dragging() const { return dragging_; }
 private:
  int anchor_ = 0, initial_ = 0, value_ = 0, minimum_ = 0, maximum_ = 0;
  bool dragging_ = false;
};
}
