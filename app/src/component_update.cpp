#include "component_update.h"
#include <algorithm>
#include <stdexcept>

namespace gbrowser {
namespace {
bool Hex(const std::string& value, size_t length) {
  return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool Version(const UpdateVersion& version) {
  return Hex(version.candidate, 32) && Hex(version.package_sha256, 64) &&
      !version.declared_version.empty() && version.declared_version.size() <= 64 &&
      std::all_of(version.declared_version.begin(), version.declared_version.end(), [](char c) {
        return c >= '!' && c <= '~';
      });
}
ComponentUpdateRequest OwnRequest(const ComponentUpdateRequest& request) {
  if (!request.operation || !request.current_instance ||
      (request.component != UpdateComponent::Tarou && request.component != UpdateComponent::ArcarumDungeon) ||
      !Version(request.current) || !Version(request.candidate) ||
      request.current.candidate == request.candidate.candidate ||
      request.current.package_sha256 == request.candidate.package_sha256)
    throw std::invalid_argument("Invalid or redundant component update request");
  return request;
}
bool SameVersion(const UpdateVersion& a, const UpdateVersion& b) {
  return a.candidate == b.candidate && a.package_sha256 == b.package_sha256 &&
      a.declared_version == b.declared_version;
}
const UpdateVersion& TargetVersion(const ComponentUpdateRequest& request, UpdateAction action) {
  const bool old = action == UpdateAction::UnloadCurrent || action == UpdateAction::VerifyRollbackCurrent ||
      action == UpdateAction::LoadCurrent || action == UpdateAction::VerifyLoadedCurrent;
  return old ? request.current : request.candidate;
}
}  // namespace

ComponentUpdate::ComponentUpdate(const ComponentUpdateRequest& request) : request_(OwnRequest(request)) {}

std::optional<UpdateCommand> ComponentUpdate::command() const {
  if (!action_) return std::nullopt;
  return UpdateCommand{request_.operation, step_, *action_, request_.component, request_.current_instance,
                       TargetVersion(request_, *action_)};
}
void ComponentUpdate::Next(UpdateAction action) { ++step_; action_ = action; }
void ComponentUpdate::Finish(UpdateOutcome outcome) { outcome_ = outcome; action_.reset(); }

bool ComponentUpdate::Reply(const UpdateCommand& supplied, UpdateReply reply) {
  // Consume a completed driver's reply without copying an allocating command
  // snapshot. An export failure must never strand an already completed action.
  if (!action_ || supplied.operation != request_.operation || supplied.step != step_ ||
      supplied.action != *action_ || supplied.component != request_.component ||
      supplied.current_instance != request_.current_instance ||
      !SameVersion(supplied.target, TargetVersion(request_, *action_))) return false;
  if (reply != UpdateReply::Confirmed && reply != UpdateReply::Rejected && reply != UpdateReply::Unknown)
    throw std::invalid_argument("Invalid component update reply");
  const auto action = *action_;
  if (reply != UpdateReply::Confirmed) failure_ = UpdateFailure{action, reply};
  // All post-preflight uncertainty is terminal, including timeouts or lease loss.
  if (reply == UpdateReply::Unknown) {
    Finish(action == UpdateAction::VerifyCandidate ? UpdateOutcome::Unchanged : UpdateOutcome::Quarantined);
    return true;
  }
  const bool confirmed = reply == UpdateReply::Confirmed;
  switch (action) {
    case UpdateAction::VerifyCandidate:
      if (confirmed) Next(UpdateAction::UnloadCurrent);
      else Finish(UpdateOutcome::Unchanged);
      break;
    case UpdateAction::UnloadCurrent:
      if (confirmed) Next(cancel_ ? UpdateAction::VerifyRollbackCurrent : UpdateAction::LoadCandidate);
      else Finish(cancel_ ? UpdateOutcome::CanceledUnchanged : UpdateOutcome::Unchanged);
      break;
    case UpdateAction::LoadCandidate:
      Next(confirmed && !cancel_ ? UpdateAction::VerifyLoadedCandidate : UpdateAction::CleanupCandidate);
      break;
    case UpdateAction::VerifyLoadedCandidate:
      Next(confirmed && !cancel_ ? UpdateAction::CommitSelection : UpdateAction::CleanupCandidate);
      break;
    case UpdateAction::CommitSelection:
      if (confirmed) Finish(UpdateOutcome::Updated);
      else Next(UpdateAction::CleanupCandidate);
      break;
    case UpdateAction::CleanupCandidate:
      if (confirmed) Next(UpdateAction::VerifyRollbackCurrent);
      else Finish(UpdateOutcome::Quarantined);
      break;
    case UpdateAction::VerifyRollbackCurrent:
      if (confirmed) Next(UpdateAction::LoadCurrent);
      else Finish(UpdateOutcome::Quarantined);
      break;
    case UpdateAction::LoadCurrent:
      if (confirmed) Next(UpdateAction::VerifyLoadedCurrent);
      else Finish(UpdateOutcome::Quarantined);
      break;
    case UpdateAction::VerifyLoadedCurrent:
      Finish(confirmed ? UpdateOutcome::RolledBack : UpdateOutcome::Quarantined);
      break;
  }
  return true;
}

bool ComponentUpdate::Cancel() {
  if (!action_ || cancel_) return false;
  switch (*action_) {
    case UpdateAction::VerifyCandidate:
      cancel_ = true; Finish(UpdateOutcome::CanceledUnchanged); return true;
    case UpdateAction::UnloadCurrent:
    case UpdateAction::LoadCandidate:
    case UpdateAction::VerifyLoadedCandidate:
      cancel_ = true; return true;
    default:
      return false;
  }
}
}  // namespace gbrowser
