#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gbrowser::webview {
// Seven optional tabs and four fixed-helper windows use separate capacity. A cancelled
// creation owns its slot until the actual completion is consumed: a late
// controller can never be published into a newer tab at the same index.
class TabSlots {
 public:
  // 4/5 belong to the optional left game and independent Arcarum pane. They
  // never consume an ordinary tab or helper-popup slot.
  static constexpr size_t kFirst = 6, kCount = 7, kAuxFirst = kFirst + kCount, kAuxCount = 4;
  static constexpr size_t kViewCount = kAuxFirst + kAuxCount;
  struct Ticket { size_t index; uint64_t generation; };
  std::optional<Ticket> Reserve() {
    return ReserveRange(0, kCount);
  }
  std::optional<Ticket> ReserveAuxiliary() {
    return ReserveRange(kCount, slots_.size());
  }
 private:
  std::optional<Ticket> ReserveRange(size_t first, size_t last) {
    for (size_t i = first; i < last; ++i) if (!slots_[i].used) {
      auto& slot = slots_[i];
      // The upper 56 bits also identify deferred native shortcut messages.
      if (slot.generation == (UINT64_MAX >> 8)) continue;
      ++slot.generation; slot.used = slot.pending = slot.visible = true;
      return Ticket{i + kFirst, slot.generation};
    }
    return std::nullopt;
  }
 public:
  bool Current(Ticket ticket) const {
    if (ticket.index < kFirst || ticket.index >= kViewCount) return false;
    const auto& slot = slots_[ticket.index - kFirst];
    return slot.used && slot.generation == ticket.generation;
  }
  bool Complete(Ticket ticket) {
    if (!Current(ticket)) return false;
    auto& slot = slots_[ticket.index - kFirst];
    slot.pending = false;
    return slot.visible;
  }
  bool Cancel(Ticket ticket) {
    if (!Current(ticket)) return false;
    auto& slot = slots_[ticket.index - kFirst];
    slot.visible = false;
    return !slot.pending;
  }
  void Release(Ticket ticket) {
    if (!Current(ticket)) return;
    auto& slot = slots_[ticket.index - kFirst];
    if (!slot.pending) slot.used = slot.visible = false;
  }
 private:
  struct Slot { uint64_t generation = 0; bool used = false, pending = false, visible = false; };
  std::array<Slot, kCount + kAuxCount> slots_{};
};
}
