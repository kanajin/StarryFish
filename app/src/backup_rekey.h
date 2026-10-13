#pragma once
#include "backup_restore.h"

namespace gbrowser {
// FR05 password maintenance, not import approval. Re-encrypt the authenticated
// frame unchanged, including opaque records and declared login mode. No live
// profile, source file, plaintext or password is retained after preparation.
class PreparedBackupRekey {
 public:
  PreparedBackupRekey(const PreparedBackupRekey&) = delete;
  PreparedBackupRekey& operator=(const PreparedBackupRekey&) = delete;
  PreparedBackupRekey(PreparedBackupRekey&&) = delete;
  PreparedBackupRekey& operator=(PreparedBackupRekey&&) = delete;
  const BackupImportSummary& summary() const { return summary_; }
  const std::filesystem::path& destination() const { return write_.destination(); }
  void Save() { write_.Save(); }
 private:
  PreparedBackupRekey(SensitiveBytes envelope, std::filesystem::path destination, BackupImportSummary summary);
  PreparedBackupWrite write_;
  const BackupImportSummary summary_;
  friend PreparedBackupRekey PrepareBackupRekey(const std::filesystem::path&, const std::filesystem::path&,
      const SensitiveBytes&, const SensitiveBytes&);
};
// Both passwords and destination are checked before reading the source. Only
// supported structural frames are accepted; no component adapters are run.
// Confirmation later saves one new file, never overwrites or rereads the source.
PreparedBackupRekey PrepareBackupRekey(const std::filesystem::path& source, const std::filesystem::path& destination,
    const SensitiveBytes& old_password, const SensitiveBytes& new_password);
}  // namespace gbrowser
