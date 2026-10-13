#pragma once
#include "backup.h"
#include <string>
#include <utility>

namespace gbrowser {
// Retained for FR05's settings/session/extension/script coverage; the user's
// confirmed data scope requires these records even before real adapters exist.
enum class BackupSourceKind : uint32_t { BrowserSettings = 1, GbfSession = 2, Extension = 3, Script = 4 };
struct BackupSource {
  BackupSourceKind kind;
  std::string id;
  uint32_t schema;
};
struct BackupRecordSummary { BackupSource source; size_t bytes; };
// Populate only from trusted code and verified, pinned component adapters.
// These capabilities are NEVER accepted from the backup document.
struct BackupAdapter {
  BackupSource source;
  bool export_without_login = false, restore_without_login = false;
  bool export_with_login = false, restore_with_login = false;
};
class BackupPlan {
 public:
  bool include_login() const { return include_login_; }
  const std::vector<BackupSource>& sources() const { return sources_; }
  bool settings_only() const { return settings_only_; }
 private:
  BackupPlan(bool login, std::vector<BackupSource> sources)
      : include_login_(login), sources_(std::move(sources)) {}
  bool include_login_;
  bool settings_only_ = false;
  std::vector<BackupSource> sources_;
  friend BackupPlan MakeBackupPlan(const std::vector<BackupSource>&,
                                   const std::vector<BackupAdapter>&, bool, bool);
  friend BackupPlan MakeSettingsBackupPlan(const std::vector<BackupAdapter>&);
};
struct BackupRecord { BackupSource source; SensitiveBytes data; };
struct BackupDocument {
  std::wstring name;
  bool include_login = false;
  std::vector<BackupRecord> records;
};
BackupPlan MakeBackupPlan(const std::vector<BackupSource>& components,
                          const std::vector<BackupAdapter>& adapters,
                          bool include_login, bool inventory_complete);
BackupPlan RebindBackupPlan(const BackupPlan& trusted_plan, const std::vector<BackupAdapter>& actual_adapters);
// Explicitly limited to product settings: does not assert that the installed
// extension/script inventory is empty or that full-profile migration works.
BackupPlan MakeSettingsBackupPlan(const std::vector<BackupAdapter>& actual_adapters);
// Available aggregate record bytes after the shared bounded frame overhead.
size_t BackupDataBudget(const std::wstring& name, const BackupPlan& trusted_plan);
void CheckBackupCoverage(const BackupDocument& document, const BackupPlan& trusted_plan);
// Structural encoding only. Parsing/authentication does not approve restore.
SensitiveBytes EncodeBackupDocument(const BackupDocument& document);
BackupDocument DecodeBackupDocument(const SensitiveBytes& encoded);
}  // namespace gbrowser
