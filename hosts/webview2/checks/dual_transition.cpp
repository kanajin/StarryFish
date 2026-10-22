#include "dual_transition.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace gbrowser;
using namespace gbrowser::webview;
void Require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
bool Same(RECT a, RECT b) { return EqualRect(&a, &b) != FALSE; }
void CheckCommit(const DualResizePlan& plan, bool anchored) {
  DualTransition transition;
  RECT outer = plan.from;
  int resize_calls = 0;
  bool secondary = !plan.opening;
  const auto apply = [&](const DualResizePlan& target) {
    Require(!secondary, "secondary was not unloaded before outer resize");
    Require(!transition.CanCreate(true), "secondary creation allowed during geometry commit");
    ++resize_calls; outer = target.to;
    Require(outer.top == plan.from.top && outer.bottom == plan.from.bottom, "dual commit changed vertical snap");
    const auto& geometry = target.after;
    Require(geometry.right.width == plan.before.right.width && geometry.right.height == plan.before.right.height &&
        geometry.tarou.width == plan.before.tarou.width && geometry.tarou.height == plan.before.tarou.height &&
        geometry.arcarum.width == plan.before.arcarum.width && geometry.arcarum.height == plan.before.arcarum.height,
        "dual commit resized a retained renderer");
    if (anchored) Require(outer.left + geometry.right.x == plan.from.left + plan.before.right.x,
        "anchored commit shifted primary screen position");
    Require(!transition.Commit(transition.generation(), [](const auto&) { return true; }), "reentrant commit was allowed");
    return true;
  };
  Require(transition.Begin(plan), "cannot begin dual transition");
  const auto token = transition.generation();
  Require(!transition.Begin(plan) && transition.generation() == token, "reentrant toggle replaced transition");
  Require(!transition.Commit(token + 1, apply) && resize_calls == 0, "wrong generation moved outer window");
  Require(!transition.CanCreate(true) && Same(outer, plan.from), "begin changed outer window or allowed secondary creation");
  secondary = false; // Host hides/closes the old view while outer geometry is unchanged.
  Require(Same(outer, plan.from), "outer changed before secondary unload");
  Require(transition.Commit(token, apply) && !transition.Commit(token, apply), "transition committed more than once");
  Require(resize_calls == 1 && Same(outer, plan.to), "commit did not use exactly one final rectangle");
  Require(transition.CanCreate(plan.opening) == plan.opening, "secondary creation gate disagrees with committed state");

  Require(transition.Begin(plan), "cannot restart completed transition");
  const auto stale = transition.generation(); transition.Cancel();
  Require(!transition.Commit(stale, apply) && !transition.active(), "cancel left a live commit");
  Require(transition.Begin(plan) && !transition.Commit(stale, apply), "old generation committed newer transition");
  Require(!transition.Commit(transition.generation(), [&](const auto&) {
    transition.Cancel();
    Require(!transition.CanCreate(true) && !transition.Begin(plan), "cancel during native call allowed reentry");
    return true;
  }) && !transition.active(), "cancelled native call reported successful commit");
  Require(resize_calls == 1, "stale completion retried outer resize");

  Require(transition.Begin(plan), "cannot begin failure fixture");
  Require(!transition.Commit(transition.generation(), [](const auto&) { return false; }) && !transition.active(),
      "native failure left a stuck transition or reported success");
  Require(transition.Begin(plan), "cannot begin exception fixture");
  bool threw = false;
  try {
    transition.Commit(transition.generation(), [&](const auto&) {
      if (Same(outer, plan.to)) throw std::runtime_error("fixture");
      return false;
    });
  } catch (const std::runtime_error&) { threw = true; }
  Require(threw && !transition.active() && transition.Begin(plan), "exception left a stuck creation gate");
  transition.Cancel();
}
}

void RunDualTransitionChecks() {
  using namespace gbrowser;
  using namespace gbrowser::webview;
  // Real Windows snap geometry: the visible right edge touches work.right,
  // while GetWindowRect includes a DPI-scaled invisible resize border.
  for (unsigned dpi : {96U, 144U, 192U}) for (int origin : {0, -3840}) for (int taskbar : {0, 48}) {
    const int border = UiPixels(8, dpi);
    const RECT work{origin + taskbar, 40, origin + 3840, 2120};
    const RECT outer{origin + 1800, work.top, work.right + border, work.bottom + border};
    const RECT visible{outer.left + border, outer.top, work.right, work.bottom};
    const auto limits = OuterResizeWorkArea(outer, visible, work);
    const auto opened = PrimaryAnchoredResize(outer, 600, 600, limits, UiPixels(400, dpi));
    Require(opened && opened->right == outer.right && opened->left == outer.left - 600 &&
        opened->top == outer.top && opened->bottom == outer.bottom, "right/vertical snap falsely rejected as left shortage");
    const RECT near_left{limits.left + 25, outer.top, limits.left + 925, outer.bottom};
    const auto shifted = PrimaryAnchoredResize(near_left, 600, 600, limits, UiPixels(400, dpi));
    Require(shifted && shifted->left + border == work.left && shifted->right - shifted->left == 1500 &&
        shifted->right > near_left.right, "left-shortage fallback lost width or did not touch visible left boundary");
    Require(!PrimaryAnchoredResize(near_left, 4000, 4000, limits, 400), "total work-area shortage silently scaled game");
  }
  for (unsigned dpi : {96U, 144U, 192U}) for (bool classic : {false, true})
      for (unsigned helpers = 0; helpers < 8; ++helpers) for (bool cropped : {false, true}) {
    PaneState single;
    single.game_width = 401; single.game_dpi = 96;
    single.tarou = (helpers & 1) != 0; single.tarou_detached = (helpers & 2) != 0;
    single.arcarum = (helpers & 4) != 0;
    single.secondary_menu_hidden = cropped; single.secondary_menu_width = 64;
    const int width = UiPixels(1600, dpi), height = UiPixels(900, dpi), border = UiPixels(8, dpi);
    const auto before = ComputePaneLayout(width, height, dpi, 50, single, false, classic);
    auto dual = single; dual.dual = true;
    const int change = before.right.width - SecondaryMenuCut(dual, dpi, before.right.width) + UiPixels(6, dpi);
    const auto after = ComputePaneLayout(width + change, height, dpi, 50, dual, false, classic);
    Require(before.right.width == after.right.width && before.tarou.width == after.tarou.width &&
        before.arcarum.width == after.arcarum.width, "dual target changed retained viewport dimensions");
    const RECT start{4000, 20, 4000 + width + 2 * border, 20 + height + border};
    const auto end = PrimaryAnchoredResize(start, change, after.right.x - before.right.x, {0, 0, 16000, 5000}, 400);
    Require(end.has_value(), "dual geometry fixture cannot fit");
    CheckCommit({start, *end, before, after, true}, true);
    CheckCommit({*end, start, after, before, false}, true);
    const RECT left{0, 20, width + 2 * border, 20 + height + border};
    const auto shifted = PrimaryAnchoredResize(left, change, after.right.x - before.right.x, {0, 0, 16000, 5000}, 400);
    Require(shifted && shifted->left == 0, "left-edge fixture did not remain anchored to work area");
    CheckCommit({left, *shifted, before, after, true}, false);
    // Closing after automatic relocation restores the exact pre-open rectangle.
    CheckCommit({*shifted, left, after, before, false}, false);
  }
  std::cout << "PASS dual single-commit/gates/reentry/cancellation/failure/stable viewports/restore; visible snap borders/rightward fallback/DPI; browser NOT_STARTED\n";
}
