#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace gbrowser {
enum class ProxyInitializationState {
  AwaitingAuthorization, CheckingOptions, Running, ValuesMatched, PreflightDifferent, ValuesDifferent, Unconfirmed,
  PreflightUnavailable
};
enum class ProxyInitializationReply { Reject, Authorize, Finished, Observed };
enum class ProxyAction { Initialize, Direct, Acgpower, Island, Custom };
std::string_view ProxyActionName(ProxyAction action);
// One confirmed selection waiting for its exact controller. Kept in memory
// only: readiness consumes dispatch once; neither callbacks nor timers retry.
class ProxySelection {
 public:
  ProxySelection(ProxyAction action, int origin, int browser, std::string frame,
                 std::string profile, uint64_t navigation, uint64_t now);
  void BindController(int tab, bool initial_navigation);
  bool OriginMatches(int tab, int browser, std::string_view frame, std::string_view profile, uint64_t navigation) const noexcept;
  bool Navigation(int tab) noexcept;
  bool ConsumeReady(int controller, uint64_t now) noexcept;
  bool Expire(uint64_t now) noexcept;
  bool Cancel(int tab) noexcept;
  bool waiting() const noexcept { return waiting_ && !canceled_; }
  int origin() const noexcept { return origin_; }
  int controller() const noexcept { return controller_; }
  ProxyAction action() const noexcept { return action_; }
 private:
  const ProxyAction action_;
  const int origin_, browser_;
  const std::string frame_, profile_;
  const uint64_t navigation_, started_;
  int controller_ = 0;
  bool initial_navigation_ = false, waiting_ = true, canceled_ = false;
};
// One explicitly confirmed operation. The host additionally validates the live
// context, registration and pinned original options document before each query.
// A reply acknowledges worker options/state only, never successful routing.
class ProxyInitialization {
 public:
  ProxyInitialization(uint64_t operation, int tab, int browser, std::string frame,
                      std::string profile, uint64_t now, ProxyAction action);
  ProxyInitializationReply Query(std::string_view request, int tab, int browser,
                                 std::string_view frame, std::string_view profile, uint64_t now) noexcept;
  bool Expire(uint64_t now) noexcept;
  bool Invalidate(int tab) noexcept;
  bool pending() const noexcept;
  // After acgpower matches, only bound read-only lease renewal/revocation
  // remains admissible; neither can authorize another write/configuration.
  bool observing() const noexcept;
  void RevokeObservation() noexcept;
  ProxyInitializationState state() const noexcept { return state_; }
  ProxyAction action() const noexcept { return action_; }
  const std::string& profile() const noexcept { return profile_; }
  uint64_t elapsed(uint64_t now) const noexcept { return now >= started_ ? now - started_ : 0; }
 private:
  void FailUnconfirmed() noexcept;
  const uint64_t operation_, started_;
  uint64_t last_observed_ = 0;
  const int tab_, browser_;
  const std::string frame_, profile_;
  const ProxyAction action_;
  ProxyInitializationState state_ = ProxyInitializationState::AwaitingAuthorization;
};
}
