#pragma once
#include "backup_data.h"
#include "profile_stage.h"
#include "core.h"
#include <functional>
#include <optional>

namespace gbrowser {
// Retained FR05 import orchestration and single-use confirmation ownership.
// Synthetic restoration tests do not prove real Chromium/extension migration.
// Populate from trusted, verified code, never from a backup document. These
// synchronous callbacks are only for adapters that can prepare and verify
// their logical data through this staging capability. Product-owned settings
// are registered; other real adapters are absent. Raw Chromium database copies
// are not adapters.
struct BackupRestoreAdapter {
  BackupAdapter capability;
  std::function<void(const SensitiveBytes&, NewProfileStage&, bool)> prepare;
  std::function<bool(const SensitiveBytes&, const NewProfileStage&, bool)> verify;
  // Optional pure, synchronous logical-schema precheck. Registered product
  // adapters use it before confirmation/staging and again with the CURRENT
  // table before publication. It must not retain payloads or perform I/O.
  std::function<void(const SensitiveBytes&, bool)> validate;
};
// Rebind declarations to the actual trusted implementation table; require all
// needed synchronous callbacks before any source or profile I/O.
BackupPlan BindBackupRestorePlan(const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& adapters);
struct BackupImportSummary {
  std::wstring name;
  bool include_login;
  std::vector<BackupRecordSummary> records;
};
// Own authenticated input across confirmation without retaining callbacks or
// a password. Summary is metadata only, never component/restore approval.
class PreparedBackupImport {
 public:
  PreparedBackupImport(const PreparedBackupImport&) = delete;
  PreparedBackupImport& operator=(const PreparedBackupImport&) = delete;
  PreparedBackupImport(PreparedBackupImport&&) = delete;
  PreparedBackupImport& operator=(PreparedBackupImport&&) = delete;
  const BackupImportSummary& summary() const { return summary_; }
  // Exactly one attempt, including failure/reentrancy. Rebind CURRENT trusted
  // callbacks before staging; their dependencies must live through this
  // synchronous call. Never reread the source, reuse old callbacks, or select.
  // Payloads are wiped when this attempt finishes, or on discard/destruction.
  // An explicit target name changes only the new profile's display metadata;
  // authenticated source declarations and payloads remain unchanged.
  Profile Publish(ProfileStore& store, const std::vector<BackupRestoreAdapter>& trusted_adapters,
                  const std::optional<std::wstring>& target_name = std::nullopt);
 private:
  PreparedBackupImport(BackupDocument document, BackupPlan plan);
  BackupDocument document_;
  const BackupPlan plan_;
  const BackupImportSummary summary_;
  bool consumed_ = false;
  friend PreparedBackupImport PrepareBackupImport(const std::filesystem::path&,
      const SensitiveBytes&, const BackupPlan&, const std::vector<BackupRestoreAdapter>&);
};
// Preflight actual callback capabilities, authenticate/parse/check coverage,
// then own the decoded snapshot. Does not create directories or run adapters.
// Discarding the result cancels without profile/selection I/O. A later Publish
// still requires current callbacks, verified data and stage integrity checks.
PreparedBackupImport PrepareBackupImport(const std::filesystem::path& source,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& trusted_adapters);
// Validate actual callback capabilities before reading the encrypted file;
// authenticate/parse/check complete coverage before creating any staging.
// Always publishes a fresh ID without selecting it. Does not create CEF
// contexts, infer installed components, project credentials, or offer a UI.
// Synchronous convenience: prepare and immediately publish using the supplied
// registry. Interactive callers must hold the prepared object until confirmed.
Profile RestoreBackupAsNewProfile(ProfileStore& store, const std::filesystem::path& source,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& trusted_adapters);
}  // namespace gbrowser
