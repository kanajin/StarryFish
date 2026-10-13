#pragma once
#include "backup_restore.h"
#include <memory>

namespace gbrowser {
// Retained FR05 export orchestration, exercised by synthetic capture tests.
// Product-owned settings snapshots and host confirmation are connected. Real
// Chromium/session/extension/script cross-source providers remain unconnected.
// Trusted synchronous provider only. It must hold cross-source quiescence or
// an immutable logical image through capture/encoding/sealing/final check, with complete inventory
// and selected-mode projection. Check() throws if this contract is lost or
// the plan/inventory differs. Version comparisons alone are NOT a snapshot.
// Destruction releases the provider's resources. The settings-only provider
// owns immutable logical values; it is not a live Chromium filesystem snapshot.
class BackupCaptureLease {
 public:
  virtual ~BackupCaptureLease() = default;
  BackupCaptureLease(const BackupCaptureLease&) = delete;
  BackupCaptureLease& operator=(const BackupCaptureLease&) = delete;
  virtual Profile profile() const = 0;
  virtual void Check(const BackupPlan& trusted_plan) const = 0;
 protected:
  BackupCaptureLease() = default;
};
struct BackupCaptureAdapter {
  BackupAdapter capability;
  // Read only the lease's logical image, not live paths. Producers must bound
  // allocations and wipe their own plaintext copies. Verify must enforce the
  // schema and login projection, not merely compare the bytes to themselves.
  std::function<SensitiveBytes(const BackupCaptureLease&, bool)> capture;
  std::function<bool(const SensitiveBytes&, const BackupCaptureLease&, bool)> verify;
};
using AcquireBackupCapture = std::function<std::unique_ptr<BackupCaptureLease>(const Profile&, const BackupPlan&)>;
struct BackupExportSummary {
  Profile profile;
  bool include_login;
  std::vector<BackupRecordSummary> records;
};
// A verified, already sealed snapshot, not a live capture/restore capability.
// No plaintext/password/callback/lease is retained across confirmation.
class PreparedBackupExport {
 public:
  PreparedBackupExport(const PreparedBackupExport&) = delete;
  PreparedBackupExport& operator=(const PreparedBackupExport&) = delete;
  PreparedBackupExport(PreparedBackupExport&&) = delete;
  PreparedBackupExport& operator=(PreparedBackupExport&&) = delete;
  const BackupExportSummary& summary() const { return summary_; }
  const std::filesystem::path& destination() const { return write_.destination(); }
  // Exactly one attempt, even on failure. Recheck the fixed local destination;
  // never overwrite or recapture. Discarding the object cancels without I/O.
  // The sealed buffer is erased on return/failure/destruction, not a guarantee
  // about external copies, paging or dumps. Failed disk evidence is retained.
  void Save() { write_.Save(); }
 private:
  PreparedBackupExport(SensitiveBytes envelope, std::filesystem::path destination, BackupExportSummary summary);
  PreparedBackupWrite write_;
  const BackupExportSummary summary_;
  friend PreparedBackupExport PrepareBackupExport(const Profile&, const std::filesystem::path&,
      const SensitiveBytes&, const BackupPlan&, const std::vector<BackupCaptureAdapter>&,
      const std::vector<BackupRestoreAdapter>&, const AcquireBackupCapture&);
};
// Same implementation/password/destination preflight as synchronous export.
// Acquire, capture, verify, encode and seal under the lease, then release it
// before returning the prepared object. No file is created until Save().
// Host confirmation and real adapter/gate approvals are still required.
PreparedBackupExport PrepareBackupExport(const Profile& expected_profile, const std::filesystem::path& destination,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupCaptureAdapter>& trusted_capture,
    const std::vector<BackupRestoreAdapter>& trusted_restore, const AcquireBackupCapture& acquire);
// Synchronous convenience: prepare/check/seal, release the capture lease, then
// immediately perform the same single Save(). No source I/O before complete
// implementation/password/destination preflight. Saving uses only owned sealed
// bytes, without recapture. Interactive callers must explicitly confirm Save().
// This provides orchestration, not a live CEF snapshot or credential classifier.
// Native settings-only UI/adapters exist; full-profile adapters remain absent.
void ExportBackupProfile(const Profile& expected_profile, const std::filesystem::path& destination,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupCaptureAdapter>& trusted_capture,
    const std::vector<BackupRestoreAdapter>& trusted_restore, const AcquireBackupCapture& acquire);
}  // namespace gbrowser
