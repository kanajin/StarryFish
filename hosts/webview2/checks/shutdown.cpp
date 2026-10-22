#include "callback_gate.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct HostFixture {
  gbrowser::webview::CallbackGate callbacks;
  int& entered;
  bool closed = false;
  std::function<void()> Request() {
    const auto ticket = callbacks.Track();
    return [this, ticket] {
      // Same entry guard as SDK completion handlers. Deliberately capture the
      // fixture to exercise a late callback after its owner has been destroyed.
      if (!ticket.Complete()) return;
      Require(!closed, "callback entered a closing host"); ++entered;
    };
  }
  void Close() { callbacks.Cancel(); closed = true; }
};
}
void RunShutdownChecks() {
  int entered = 0;
  {
    gbrowser::webview::CallbackGate lifecycle;
    const auto event = lifecycle.Track();
    Require(event.Active() && event.Active(), "lifecycle event was prematurely consumed");
    lifecycle.Cancel();
    Require(!event.Active() && !event.Complete(), "late lifecycle event remained live");
  }
  std::vector<std::function<void()>> late;
  {
    HostFixture host{{}, entered};
    auto live = host.Request(); auto duplicate = live;
    live(); duplicate();
    Require(entered == 1, "completion ran more than once");
    for (int i = 0; i < 32; ++i) late.push_back(host.Request());
    host.Close(); host.Close();
    // Some SDK callbacks may never arrive after Controller::Close. Dropping
    // them must not be a prerequisite to closing the remaining views.
    late.erase(late.begin(), late.begin() + 8);
    late.front()();
    Require(host.closed && entered == 1, "optional requests blocked shutdown");
  }
  for (auto& callback : late) callback();
  Require(entered == 1, "late completion used a destroyed host");
  {
    HostFixture next{{}, entered};
    next.Request()();
    for (auto& callback : late) callback();
    Require(entered == 2, "old callbacks entered the next profile host");
    late.push_back(next.Request()); // Destructor cancellation, startup failure.
  }
  late.back()();
  Require(entered == 2, "destructor did not invalidate a callback");
  std::cout << "PASS optional completion cancellation/duplicates/late delivery/destruction/profile reuse; browser NOT_STARTED\n";
}
