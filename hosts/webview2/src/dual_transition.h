#pragma once
#include "convenience_layout.h"
#include <cstdint>

namespace gbrowser::webview {
struct DualResizePlan {
  RECT from{}, to{};
  PaneLayout before{}, after{};
  bool opening = false;
};

// One synchronous geometry commit, with creation blocked until layout completes.
// No renderer, profile, timer or native window is owned by this state machine.
class DualTransition {
 public:
  bool Begin(const DualResizePlan& plan) {
    if (active_ || committing_) return false;
    plan_ = plan; active_ = true; ++generation_; return true;
  }
  bool active() const { return active_; }
  uint64_t generation() const { return generation_; }
  const DualResizePlan& plan() const { return plan_; }
  bool CanCreate(bool dual) const { return dual && !active_ && !committing_; }
  template <class Apply> bool Commit(uint64_t generation, Apply apply) {
    if (!active_ || committing_ || generation != generation_) return false;
    committing_ = true;
    struct Reset {
      DualTransition& state;
      ~Reset() { state.active_ = false; state.committing_ = false; }
    } reset{*this};
    const bool applied = apply(plan_);
    return applied && active_ && generation == generation_;
  }
  void Cancel() { active_ = false; ++generation_; }
 private:
  DualResizePlan plan_{};
  uint64_t generation_ = 0;
  bool active_ = false, committing_ = false;
};
}
