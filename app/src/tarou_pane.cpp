#include "tarou_pane.h"
#include <stdexcept>
#include <algorithm>

namespace gbrowser {
void TarouPaneBinding::Begin(int slot, int target) {
  if (slot <= 0 || !MayCreate(target)) throw std::runtime_error("Tarou pane slot is unavailable");
  slot_ = slot; target_ = target; retiring_ = false; faulted_ = false;
}
void TarouPaneBinding::Retire(int slot) { if (slot > 0 && slot == slot_) retiring_ = true; }
void TarouPaneBinding::Fail(int slot) {
  if (slot > 0 && slot == slot_) { failed_ = target_; retiring_ = true; }
}
void TarouPaneBinding::PageFailed(int slot) {
  // Renderer termination may be followed by a load-error for the same host.
  // After explicit Retry, that late error must not revoke requested retirement.
  if (slot > 0 && slot == slot_ && !faulted_) { faulted_ = true; failed_ = target_; }
}
void TarouPaneBinding::Closed(int slot, bool unexpected) {
  if (slot <= 0 || slot != slot_) return;
  if (unexpected) failed_ = target_;
  slot_ = 0; target_ = 0; retiring_ = false; faulted_ = false;
}
bool IsTarouGamePage(const std::string& url) {
  if (url.size() > 8192 || !std::all_of(url.begin(), url.end(), [](unsigned char c) {
      return c >= 0x21 && c <= 0x7e && c != '\\'; })) return false;
  for (const char* root : {"https://game.granbluefantasy.jp/", "https://gbf.game.mbga.jp/"})
    if (url.compare(0, std::char_traits<char>::length(root), root) == 0) return true;
  return false;
}
}
