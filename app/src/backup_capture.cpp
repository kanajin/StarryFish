#include "backup_capture.h"
#include <algorithm>
#include <stdexcept>

namespace gbrowser {
namespace {
void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
const BackupCaptureAdapter& Find(const std::vector<BackupCaptureAdapter>& adapters, const BackupSource& source) {
  const auto found = std::find_if(adapters.begin(), adapters.end(), [&](const BackupCaptureAdapter& adapter) {
    const auto& candidate = adapter.capability.source;
    return candidate.kind == source.kind && candidate.id == source.id && candidate.schema == source.schema;
  });
  Require(found != adapters.end() && found->capture && found->verify,
          "Backup capture implementation is missing or incompatible");
  return *found;
}
void CheckLease(const BackupCaptureLease& lease, const Profile& expected, const BackupPlan& plan) {
  lease.Check(plan);
  const auto actual = lease.profile();
  Require(actual.id == expected.id && actual.name == expected.name, "Backup snapshot profile changed");
}
}  // namespace

PreparedBackupExport::PreparedBackupExport(SensitiveBytes envelope, std::filesystem::path destination,
    BackupExportSummary summary)
    : write_(std::move(envelope), std::move(destination)), summary_(std::move(summary)) {}

PreparedBackupExport PrepareBackupExport(const Profile& expected_profile, const std::filesystem::path& destination,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupCaptureAdapter>& trusted_capture,
    const std::vector<BackupRestoreAdapter>& trusted_restore, const AcquireBackupCapture& acquire) {
  // Own request/registry state before running any callback. Callbacks must not
  // retain references or schedule work beyond this synchronous lease lifetime.
  const auto expected = expected_profile;
  Require(IsProfileId(expected.id), "Invalid backup profile identity");
  ValidateProfileName(expected.name);
  const auto capture = trusted_capture;
  const auto restore = trusted_restore;
  const auto acquire_snapshot = acquire;
  Require(static_cast<bool>(acquire_snapshot), "Backup snapshot provider is missing");
  const auto restore_plan = BindBackupRestorePlan(trusted_plan, restore);
  std::vector<BackupAdapter> capabilities;
  for (const auto& adapter : capture) capabilities.push_back(adapter.capability);
  const auto plan = RebindBackupPlan(restore_plan, capabilities);
  std::vector<const BackupCaptureAdapter*> selected;
  for (const auto& source : plan.sources()) selected.push_back(&Find(capture, source));
  ValidateBackupPassword(password);
  const auto target = ValidateBackupDestination(destination);
  const SensitiveBytes password_snapshot{BackupBytes(password.bytes())};
  auto remaining = BackupDataBudget(expected.name, plan);
  const auto lease = acquire_snapshot(expected, plan);
  Require(lease != nullptr, "Backup snapshot acquisition failed");
  CheckLease(*lease, expected, plan);
  BackupDocument document; document.name = expected.name; document.include_login = plan.include_login();
  document.records.reserve(selected.size());
  for (size_t i = 0; i < selected.size(); ++i) {
    CheckLease(*lease, expected, plan);
    auto data = selected[i]->capture(*lease, plan.include_login());
    Require(data.bytes().size() <= remaining, "Backup document exceeds 16 MiB");
    remaining -= data.bytes().size();
    CheckLease(*lease, expected, plan);
    Require(selected[i]->verify(data, *lease, plan.include_login()), "Backup captured data verification failed");
    CheckLease(*lease, expected, plan);
    document.records.push_back({plan.sources()[i], std::move(data)});
  }
  CheckBackupCoverage(document, plan);
  CheckLease(*lease, expected, plan);
  const auto encoded = EncodeBackupDocument(document);
  CheckLease(*lease, expected, plan);
  // Seal and recheck under the lease for both interactive and synchronous
  // exports. File publication later consumes only this owned sealed snapshot.
  SensitiveBytes envelope(SealBackup(encoded, password_snapshot));
  CheckLease(*lease, expected, plan);
  BackupExportSummary summary{expected, document.include_login, {}};
  summary.records.reserve(document.records.size());
  for (const auto& record : document.records)
    summary.records.push_back({record.source, record.data.bytes().size()});
  return PreparedBackupExport(std::move(envelope), target, std::move(summary));
}

void ExportBackupProfile(const Profile& expected_profile, const std::filesystem::path& destination,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupCaptureAdapter>& trusted_capture,
    const std::vector<BackupRestoreAdapter>& trusted_restore, const AcquireBackupCapture& acquire) {
  auto prepared = PrepareBackupExport(expected_profile, destination, password, trusted_plan,
                                     trusted_capture, trusted_restore, acquire);
  prepared.Save();
}
}  // namespace gbrowser
