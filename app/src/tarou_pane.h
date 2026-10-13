#pragma once
#include <string>

namespace gbrowser {
// Native pane slots remain owned across cancellation/timeout until the actual
// CEF close arrives. A replacement must not overlap Tarou's one-document rule.
class TarouPaneBinding {
 public:
  bool MayCreate(int target) const { return target > 0 && !slot_ && failed_ != target; }
  void Begin(int slot, int target);
  void Retire(int slot);
  void Fail(int slot);
  void PageFailed(int slot);
  void Block(int target) { if (target > 0) failed_ = target; }
  void Retry() { failed_ = 0; }
  void CancelRetire(int slot) { if (slot > 0 && slot == slot_) retiring_ = false; }
  bool NeedsRetire(int target) const {
    return slot_ && !retiring_ && (target != target_ || (faulted_ && failed_ != target_));
  }
  void Closed(int slot, bool unexpected = false);
  bool Shows(int slot, int target) const {
    return slot > 0 && slot == slot_ && target > 0 && target == target_ && !retiring_ && !faulted_;
  }
  int slot() const { return slot_; }
  int target() const { return target_; }
 private:
  int slot_ = 0, target_ = 0, failed_ = 0;
  // A failed live document still owns its slot/source. Retry only permits a
  // normal close; neither visibility nor singleton ownership is restored.
  bool retiring_ = false, faulted_ = false;
};
// Target eligibility never grants navigation. HTTPS endpoints only; the outer
// browser's navigation policy must independently allow the game page.
bool IsTarouGamePage(const std::string& url);
}
