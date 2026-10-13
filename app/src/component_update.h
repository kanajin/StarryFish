#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace gbrowser {
enum class UpdateComponent { Tarou, ArcarumDungeon };
struct UpdateVersion {
  std::string candidate;  // 32 lowercase hex token, not a path or installation permit.
  std::string package_sha256;
  std::string declared_version;  // Exact bounded declaration; no semantic version ordering.
};
struct ComponentUpdateRequest {
  uint64_t operation = 0;  // Driver must allocate globally unique, non-reused operation IDs.
  UpdateComponent component = UpdateComponent::Tarou;
  uint64_t current_instance = 0;  // Driver-owned identity of the exact loaded old instance/context.
  UpdateVersion current, candidate;
};
enum class UpdateAction {
  VerifyCandidate, UnloadCurrent, LoadCandidate, VerifyLoadedCandidate, CommitSelection,
  CleanupCandidate, VerifyRollbackCurrent, LoadCurrent, VerifyLoadedCurrent
};
struct UpdateCommand {
  uint64_t operation;
  uint32_t step;
  UpdateAction action;
  UpdateComponent component;
  uint64_t current_instance;
  UpdateVersion target;
};
// Replies are only for a trusted runtime driver, never UI/manifest/receipt input.
// Confirmed means an actual completed, verified postcondition, not API acceptance.
// Rejected must be settled, with the action-specific invariants below proven.
// Unknown includes timeout, lost lease or any unprovable completion/postcondition.
enum class UpdateReply { Confirmed, Rejected, Unknown };
enum class UpdateOutcome { Pending, Unchanged, CanceledUnchanged, Updated, RolledBack, Quarantined };
struct UpdateFailure { UpdateAction action; UpdateReply reply; };

// Retained for FR10 manual Tarou/arcarum updates and tested rollback ordering;
// no automatic downloader/updater is intended by the requirement baseline.
// Pure serial protocol, with NO file/CEF operations, authorization or real driver.
// The driver must exclusively serialize this component and hold a trusted lease
// binding old runtime instance + old durable selection, candidate bytes/manifest,
// source/rights/signature/runtime gates, and storage ownership for the whole run.
// A point-in-time candidate recheck or these tokens do NOT satisfy that contract.
// No actual extension loader or approval provider is registered in the product.
class ComponentUpdate {
 public:
  explicit ComponentUpdate(const ComponentUpdateRequest& request);
  ComponentUpdate(const ComponentUpdate&) = delete;
  ComponentUpdate& operator=(const ComponentUpdate&) = delete;
  ComponentUpdate(ComponentUpdate&&) = delete;
  ComponentUpdate& operator=(ComponentUpdate&&) = delete;
  const ComponentUpdateRequest& request() const { return request_; }
  std::optional<UpdateCommand> command() const;
  UpdateOutcome outcome() const { return outcome_; }
  std::optional<UpdateFailure> failure() const { return failure_; }
  bool cancellation_requested() const { return cancel_; }
  // Stale/duplicate/mismatched/terminal replies have no effect and return false.
  // Matching valid replies require no allocation; command() still exports an
  // owned snapshot that can fail before the driver starts its external action.
  bool Reply(const UpdateCommand& command, UpdateReply reply);
  // Preflight cancels immediately; after unload starts, waits for the in-flight
  // reply. Commit/recovery/terminal cancellation is refused. Driver must still
  // drain outstanding read-only preflight work before releasing its resources.
  bool Cancel();
 private:
  void Next(UpdateAction action);
  void Finish(UpdateOutcome outcome);
  const ComponentUpdateRequest request_;
  std::optional<UpdateAction> action_ = UpdateAction::VerifyCandidate;
  uint32_t step_ = 1;
  UpdateOutcome outcome_ = UpdateOutcome::Pending;
  std::optional<UpdateFailure> failure_;
  bool cancel_ = false;
};

// Driver postconditions for each action:
// VerifyCandidate: strictly read-only; Confirmed requires all gates/lease/binding
// above, including compatible real load, unload, drain and persistence APIs.
// UnloadCurrent: Confirmed old instance actually absent; Rejected old instance
// still fully usable and old durable selection unchanged. Otherwise Unknown.
// LoadCandidate/VerifyLoadedCandidate: Confirmed exact candidate actually loaded
// and verified as applicable; Rejected settled failure, requiring cleanup.
// CommitSelection: Confirmed durable exact new selection and runtime match;
// Rejected proves durable old selection unchanged. Unknown is never rolled back.
// CleanupCandidate: Confirmed cancels/drains ALL outstanding/late candidate loads
// for this operation and proves candidate absent BEFORE any old load is issued.
// VerifyRollbackCurrent: recheck old bytes/identity/gates under the same lease.
// LoadCurrent/VerifyLoadedCurrent: Confirmed old instance restored/verified with
// old durable selection; any failure/uncertainty during recovery quarantines.
// Unknown after unload is issued ALWAYS quarantines: no retries or guessed undo.
// Terminal status alone does not prove driver facts or authorize lease release
// while callbacks remain outstanding. Quarantine requires host recovery policy.
}  // namespace gbrowser
