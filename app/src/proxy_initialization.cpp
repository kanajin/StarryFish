#include "proxy_initialization.h"
#include <charconv>
#include <stdexcept>
#include <utility>

namespace gbrowser {
std::string_view ProxyActionName(ProxyAction action) {
  switch (action) {
    case ProxyAction::Initialize: return "initialize";
    case ProxyAction::Direct: return "direct";
    case ProxyAction::Acgpower: return "acgpower";
    case ProxyAction::Island: return "岛风go";
  }
  throw std::runtime_error("Unknown proxy action");
}
ProxySelection::ProxySelection(ProxyAction action, int origin, int browser, std::string frame,
    std::string profile, uint64_t navigation, uint64_t now)
    : action_(action), origin_(origin), browser_(browser), frame_(std::move(frame)),
      profile_(std::move(profile)), navigation_(navigation), started_(now) {
  (void)ProxyActionName(action);
  if (action == ProxyAction::Initialize || origin <= 0 || browser <= 0 || frame_.empty() || profile_.empty())
    throw std::runtime_error("Invalid confirmed proxy selection");
}
void ProxySelection::BindController(int tab, bool initial_navigation) {
  if (tab <= 0 || controller_ || !waiting()) throw std::runtime_error("Proxy controller binding rejected");
  controller_ = tab; initial_navigation_ = initial_navigation;
}
bool ProxySelection::OriginMatches(int tab, int browser, std::string_view frame, std::string_view profile, uint64_t navigation) const noexcept {
  return !canceled_ && origin_ == tab && browser_ == browser && frame_ == frame && profile_ == profile && navigation_ == navigation;
}
bool ProxySelection::Cancel(int tab) noexcept {
  if (canceled_ || (tab != origin_ && tab != controller_)) return false;
  canceled_ = true; return true;
}
bool ProxySelection::Navigation(int tab) noexcept {
  if (tab == controller_ && waiting() && initial_navigation_) {
    initial_navigation_ = false; return false;
  }
  return Cancel(tab);
}
bool ProxySelection::Expire(uint64_t now) noexcept {
  if (!waiting() || (now >= started_ && now - started_ < 60000)) return false;
  canceled_ = true; return true;
}
bool ProxySelection::ConsumeReady(int controller, uint64_t now) noexcept {
  if (Expire(now) || !waiting() || !controller_ || controller != controller_) return false;
  waiting_ = false; return true;
}
ProxyInitialization::ProxyInitialization(uint64_t operation, int tab, int browser, std::string frame,
    std::string profile, uint64_t now, ProxyAction action)
    : operation_(operation), started_(now), tab_(tab), browser_(browser),
      frame_(std::move(frame)), profile_(std::move(profile)), action_(action) {
  (void)ProxyActionName(action);
  if (!operation || tab <= 0 || browser <= 0 || frame_.empty() || profile_.empty())
    throw std::runtime_error("Invalid proxy initialization binding");
}
bool ProxyInitialization::pending() const noexcept {
  return state_ == ProxyInitializationState::AwaitingAuthorization || state_ == ProxyInitializationState::CheckingOptions ||
      state_ == ProxyInitializationState::Running;
}
bool ProxyInitialization::observing() const noexcept {
  return action_ != ProxyAction::Initialize && state_ == ProxyInitializationState::ValuesMatched;
}
void ProxyInitialization::RevokeObservation() noexcept {
  if (observing()) state_ = ProxyInitializationState::Unconfirmed;
}
void ProxyInitialization::FailUnconfirmed() noexcept {
  // A selection has no write authority before the separate apply handshake.
  // Missing early runtime/worker replies can therefore release this intent;
  // after authorization an unknown result must stay blocked, never replayed.
  state_ = action_ != ProxyAction::Initialize &&
      (state_ == ProxyInitializationState::AwaitingAuthorization || state_ == ProxyInitializationState::CheckingOptions) ?
      ProxyInitializationState::PreflightUnavailable : ProxyInitializationState::Unconfirmed;
}
bool ProxyInitialization::Expire(uint64_t now) noexcept {
  if (!pending() && !observing()) return false;
  const auto reference = observing() ? last_observed_ : started_;
  const uint64_t budget = observing() ? 180000 :
      action_ != ProxyAction::Initialize && state_ == ProxyInitializationState::AwaitingAuthorization ? 5000 : 60000;
  if (now >= reference && now - reference < budget) return false;
  FailUnconfirmed(); return true;
}
bool ProxyInitialization::Invalidate(int tab) noexcept {
  if ((!pending() && !observing()) || tab != tab_) return false;
  FailUnconfirmed(); return true;
}
ProxyInitializationReply ProxyInitialization::Query(std::string_view request, int tab, int browser,
    std::string_view frame, std::string_view profile, uint64_t now) noexcept {
  if (Expire(now) || (!pending() && !observing()) || tab != tab_ || browser != browser_ || frame != frame_ || profile != profile_ ||
      request.size() > 40) return ProxyInitializationReply::Reject;
  const auto separator = request.find(':');
  if (separator == std::string_view::npos || separator == 0 || request.front() == '0')
    return ProxyInitializationReply::Reject;
  uint64_t operation = 0;
  const auto number = std::from_chars(request.data(), request.data() + separator, operation);
  if (number.ec != std::errc{} || number.ptr != request.data() + separator || operation != operation_)
    return ProxyInitializationReply::Reject;
  const auto verb = request.substr(separator + 1);
  if (observing()) {
    if (verb == "verified") {
      last_observed_ = now; return ProxyInitializationReply::Observed;
    }
    if (verb != "changed") return ProxyInitializationReply::Reject;
    state_ = ProxyInitializationState::Unconfirmed;
    return ProxyInitializationReply::Finished;
  }
  if (state_ == ProxyInitializationState::AwaitingAuthorization) {
    if (verb == "unconfirmed" && action_ != ProxyAction::Initialize) {
      FailUnconfirmed(); return ProxyInitializationReply::Finished;
    }
    if (verb != "begin") return ProxyInitializationReply::Reject;
    state_ = action_ == ProxyAction::Initialize ? ProxyInitializationState::Running : ProxyInitializationState::CheckingOptions;
    return ProxyInitializationReply::Authorize;
  }
  if (state_ == ProxyInitializationState::CheckingOptions) {
    if (verb == "apply") {
      state_ = ProxyInitializationState::Running; return ProxyInitializationReply::Authorize;
    }
    if (verb == "different") {
      state_ = ProxyInitializationState::PreflightDifferent; return ProxyInitializationReply::Finished;
    }
    if (verb != "unconfirmed") return ProxyInitializationReply::Reject;
  }
  if (verb == "matched") { state_ = ProxyInitializationState::ValuesMatched; last_observed_ = now; }
  else if (verb == "different") state_ = ProxyInitializationState::ValuesDifferent;
  else if (verb == "unconfirmed") FailUnconfirmed();
  else return ProxyInitializationReply::Reject;
  return ProxyInitializationReply::Finished;
}
}
