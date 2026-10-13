#include "backup_restore.h"
#include <algorithm>
#include <stdexcept>

namespace gbrowser {
namespace {
bool SameSource(const BackupSource& a, const BackupSource& b) {
  return a.kind == b.kind && a.id == b.id && a.schema == b.schema;
}
const BackupRestoreAdapter& Find(const std::vector<BackupRestoreAdapter>& adapters, const BackupSource& source) {
  const auto found = std::find_if(adapters.begin(), adapters.end(), [&](const BackupRestoreAdapter& adapter) {
    return SameSource(adapter.capability.source, source);
  });
  if (found == adapters.end() || !found->prepare || !found->verify)
    throw std::runtime_error("Backup restore implementation is missing or incompatible");
  return *found;
}
BackupImportSummary Summarize(const BackupDocument& document) {
  BackupImportSummary result{document.name, document.include_login, {}};
  result.records.reserve(document.records.size());
  for (const auto& record : document.records)
    result.records.push_back({record.source, record.data.bytes().size()});
  return result;
}
}  // namespace

BackupPlan BindBackupRestorePlan(const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& adapters) {
  std::vector<BackupAdapter> capabilities;
  for (const auto& adapter : adapters) capabilities.push_back(adapter.capability);
  // Rebind the plan to the capabilities of the actual callback table. A plan
  // created from a different set of capability declarations is insufficient.
  const auto execution_plan = RebindBackupPlan(trusted_plan, capabilities);
  for (const auto& component : execution_plan.sources()) (void)Find(adapters, component);
  return execution_plan;
}
PreparedBackupImport::PreparedBackupImport(BackupDocument document, BackupPlan plan)
    : document_(std::move(document)), plan_(std::move(plan)), summary_(Summarize(document_)) {}

PreparedBackupImport PrepareBackupImport(const std::filesystem::path& source,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& trusted_adapters) {
  const auto adapters = trusted_adapters;
  auto execution_plan = BindBackupRestorePlan(trusted_plan, adapters);
  const auto plaintext = LoadBackupFile(source, password);
  auto document = DecodeBackupDocument(plaintext);
  CheckBackupCoverage(document, execution_plan);
  for (const auto& record : document.records) {
    const auto& adapter = Find(adapters, record.source);
    if (adapter.validate) adapter.validate(record.data, execution_plan.include_login());
  }
  return PreparedBackupImport(std::move(document), std::move(execution_plan));
}

Profile PreparedBackupImport::Publish(ProfileStore& store,
    const std::vector<BackupRestoreAdapter>& trusted_adapters, const std::optional<std::wstring>& target_name) {
  if (consumed_) throw std::runtime_error("Backup import already consumed; a second publication is rejected");
  consumed_ = true;
  struct ErasePayload {
    BackupDocument& document;
    ~ErasePayload() { document.records.clear(); }
  } erase{document_};
  // Own a stable copy: a callback updating the caller's registry must not
  // invalidate the selected implementations in this in-flight operation.
  // No callback/closure dependency survived the user's confirmation wait.
  const auto adapters = trusted_adapters;
  const auto execution_plan = BindBackupRestorePlan(plan_, adapters);
  CheckBackupCoverage(document_, execution_plan);
  // Resolve every callback before filesystem preparation; all payloads stay
  // in SensitiveBytes and are erased on return or any exception.
  std::vector<const BackupRestoreAdapter*> selected;
  for (const auto& record : document_.records) {
    const auto& adapter = Find(adapters, record.source);
    if (adapter.validate) adapter.validate(record.data, execution_plan.include_login());
    selected.push_back(&adapter);
  }
  return store.CreatePrepared(target_name ? *target_name : document_.name, [&](NewProfileStage& stage) {
    for (size_t i = 0; i < document_.records.size(); ++i)
      selected[i]->prepare(document_.records[i].data, stage, execution_plan.include_login());
  }, [&](const NewProfileStage& stage) {
    for (size_t i = 0; i < document_.records.size(); ++i)
      if (!selected[i]->verify(document_.records[i].data, stage, execution_plan.include_login())) return false;
    return true;
  });
}

Profile RestoreBackupAsNewProfile(ProfileStore& store, const std::filesystem::path& source,
    const SensitiveBytes& password, const BackupPlan& trusted_plan,
    const std::vector<BackupRestoreAdapter>& trusted_adapters) {
  auto prepared = PrepareBackupImport(source, password, trusted_plan, trusted_adapters);
  return prepared.Publish(store, trusted_adapters);
}
}  // namespace gbrowser
