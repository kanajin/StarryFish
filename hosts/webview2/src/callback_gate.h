#pragma once
#include <memory>

namespace gbrowser::webview {
// UI-apartment callbacks that produce optional page data must not keep Close
// waiting for a renderer. A ticket can outlive the host, but cannot enter it
// after cancellation (or run twice). Controller creation and disk jobs retain
// their separate, mandatory lifetime accounting.
class CallbackGate {
  struct Epoch { bool active = true; };
 public:
  class Ticket {
    struct State { std::shared_ptr<Epoch> epoch; bool completed = false; };
    std::shared_ptr<State> state_;
    friend class CallbackGate;
    explicit Ticket(std::shared_ptr<Epoch> epoch) : state_(std::make_shared<State>(State{std::move(epoch)})) {}
   public:
    bool Active() const { return !state_->completed && state_->epoch->active; }
    bool Complete() const {
      if (state_->completed) return false;
      state_->completed = true;
      return state_->epoch->active;
    }
  };
  CallbackGate() = default;
  CallbackGate(const CallbackGate&) = delete;
  CallbackGate& operator=(const CallbackGate&) = delete;
  ~CallbackGate() { Cancel(); }
  Ticket Track() const { return Ticket(epoch_); }
  void Cancel() { epoch_->active = false; }
 private:
  std::shared_ptr<Epoch> epoch_ = std::make_shared<Epoch>();
};
}
