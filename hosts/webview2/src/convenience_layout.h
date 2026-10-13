#pragma once
#include <windows.h>
#include "ui_interaction.h"
#include "user_state.h"
#include "tab_slots.h"
#include "compact_chrome.h"
#include <array>
#include <string_view>

namespace gbrowser::webview {
// Owned pane teardown must not re-enter the host's main-window dispatcher.
// Call only after its controller is reparented or actually closed.
inline void DestroyPaneWindow(HWND window) noexcept {
  if (!window) return;
  SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  DestroyWindow(window);
}
struct PaneLayout {
  WindowLayout chrome;
  Rect left{}, right{}, tarou{}, arcarum{}, divider{}, helper_divider{}, link{};
};
// Ordinary right-hand tabs share display adapters, never assistant ownership.
inline bool GameSurface(size_t index) {
  return index == 0 || index == 4 || (index >= TabSlots::kFirst && index < TabSlots::kAuxFirst);
}
inline int TarouWidthChange(const PaneLayout& before, bool was_docked, bool docked,
    int remembered, unsigned share, unsigned dpi) {
  if (was_docked == docked) return 0;
  const int gap = UiPixels(6, dpi);
  if (was_docked) return -(before.tarou.width +
      (before.arcarum.width > 0 ? before.helper_divider.width : before.divider.width));
  // Keep an existing Arcarum pane at its current width and restore the chosen
  // internal ratio. Without it, restore Tarou's own last docked width.
  const int width = before.arcarum.width > 0 ? static_cast<int>(
      static_cast<int64_t>(before.arcarum.width) * share / (10000 - share)) : remembered;
  return (std::max)(UiPixels(120, dpi), width) + gap;
}
inline int SavedGameWidth(const PaneState& state, unsigned dpi) {
  return state.game_width ? MulDiv(state.game_width, static_cast<int>(dpi), static_cast<int>(state.game_dpi)) : 0;
}
inline int SecondaryMenuCut(const PaneState& state, unsigned dpi, int game) {
  if (!state.dual || !state.secondary_menu_hidden) return 0;
  return (std::clamp)(MulDiv(state.secondary_menu_width, static_cast<int>(dpi),
      static_cast<int>(state.secondary_menu_dpi)), 0, (std::max)(0, game - UiPixels(160, dpi)));
}
inline int RetainedGameInset(int inset, std::string_view source) {
  // Exactly the display adapter's origin; Mobage login is not a game viewport.
  constexpr std::string_view origin = "https://game.granbluefantasy.jp/";
  return source.substr(0, origin.size()) == origin ? inset : 0;
}
inline RECT GameViewBounds(const Rect& frame, int cropped) {
  // RECT's right is a coordinate: the child retains frame.width + cropped,
  // and its parent clips exactly the hidden Moba strip, not a second strip.
  return {-cropped, 0, frame.width, frame.height};
}
// The legacy percentage is consulted only before a saved width is established.
// Window resizing gives the remaining width to helpers, never scales games.
inline PaneLayout ComputePaneLayout(int width, int height, unsigned dpi, unsigned percent,
    const PaneState& state, bool detached, bool browser_layout = false) {
  PaneLayout out{};
  const bool tarou = state.tarou && !state.tarou_detached;
  const bool dock = state.arcarum && !detached;
  const bool helpers = tarou || dock;
  out.chrome = HostWindowLayout(width, height, helpers, dpi, percent);
  // A plain theme-colored trim joins the left edge along the entire bottom.
  const int top = UiPixels(browser_layout ? 86 : 44, dpi);
  const int rail = (std::min)(width, UiPixels(browser_layout ? 6 : 48, dpi));
  const int content_height = (std::max)(0, height - top - LeftChromeInset(dpi));
  out.chrome.content.rail = {width - rail, top, rail, content_height};
  out.chrome.content.browser.y = top;
  out.chrome.content.browser.height = content_height;
  const int inset = (std::min)(out.chrome.content.rail.x, LeftChromeInset(dpi));
  const auto body = out.chrome.content.rail.x;
  const int gap = state.dual ? (std::min)(UiPixels(6, dpi), body - inset) : 0;
  const int available = (std::max)(0, body - inset - gap);
  const int helper = helpers ? static_cast<int>(static_cast<int64_t>(available) * percent / 100) : 0;
  const int count = state.dual ? 2 : 1;
  const int wanted = state.game_width ? SavedGameWidth(state, dpi) : (available - helper) / count;
  // On an exceptionally small/new monitor, clamp rendering without destroying
  // the remembered width. Normal user resizing has a matching minimum size.
  const int budget = (std::max)(0, available - (helpers ? UiPixels(6, dpi) : 0));
  int low = 0, high = (std::min)(wanted, budget);
  // Crop is bounded by the remaining game width. Fit against that actual
  // width, not the saved width, including exceptionally small monitors.
  while (low < high) {
    const int mid = low + (high - low + 1) / 2;
    if (mid * count - SecondaryMenuCut(state, dpi, mid) <= budget) low = mid;
    else high = mid - 1;
  }
  const int game = low;
  const auto area = out.chrome.content.browser;
  int x = inset;
  if (state.dual) { out.left = {x, area.y, game - SecondaryMenuCut(state, dpi, game), area.height}; x += out.left.width;
    out.link = {(std::max)(0, x - UiPixels(44, dpi)), area.y, UiPixels(40, dpi), area.height}; x += gap; }
  out.right = {x, area.y, game, area.height}; x += game;
  if (helpers) {
    const int outer_gap = (std::min)(UiPixels(6, dpi), (std::max)(0, body - x));
    out.divider = {x, area.y, outer_gap, area.height}; x += outer_gap;
    const int room = (std::max)(0, body - x);
    const int inner_gap = tarou && dock ? (std::min)(UiPixels(6, dpi), room) : 0;
    const int panel = tarou ? (dock ? static_cast<int>(static_cast<int64_t>(room - inner_gap) * state.tarou_share / 10000) : room) : 0;
    if (tarou) { out.tarou = {x, area.y, panel, area.height}; x += panel; }
    if (tarou && dock) { out.helper_divider = {x, area.y, inner_gap, area.height}; x += inner_gap; }
    if (dock) out.arcarum = {x, area.y, (std::max)(0, body - x), area.height};
  }
  return out;
}
// Dual resize anchors the primary pane's screen coordinate, including integer
// split rounding. Reject insufficient work area instead of silently moving it.
inline std::optional<RECT> PrimaryAnchoredResize(RECT old, int delta, int primary_delta, RECT work, int minimum) {
  int width = (std::max)(minimum, static_cast<int>(old.right - old.left) + delta);
  int left = old.left - primary_delta;
  if (left < work.left || left + width > work.right) return {};
  return RECT{left, old.top, left + width, old.bottom};
}
// Navigation linking never includes ordinary tabs, assistants or login views.
inline std::array<size_t, 2> GameCommandTargets(size_t active, size_t pointer, bool dual) {
  constexpr size_t left = 4;
  if (dual && pointer == left && active != 0) return {left, SIZE_MAX};
  if (active != 0 || !dual) return {active, SIZE_MAX};
  return {pointer == left ? left : 0, SIZE_MAX};
}
}
