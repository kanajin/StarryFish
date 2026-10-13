#include "backup_rekey.h"

namespace gbrowser {
PreparedBackupRekey::PreparedBackupRekey(SensitiveBytes envelope, std::filesystem::path destination,
    BackupImportSummary summary)
    : write_(std::move(envelope), std::move(destination)), summary_(std::move(summary)) {}
PreparedBackupRekey PrepareBackupRekey(const std::filesystem::path& source, const std::filesystem::path& destination,
    const SensitiveBytes& old_password, const SensitiveBytes& new_password) {
  ValidateBackupPassword(old_password); ValidateBackupPassword(new_password);
  const auto target = ValidateBackupDestination(destination);
  const auto input = source;
  const auto plaintext = LoadBackupFile(input, old_password);
  const auto document = DecodeBackupDocument(plaintext);
  BackupImportSummary summary{document.name, document.include_login, {}};
  summary.records.reserve(document.records.size());
  for (const auto& record : document.records)
    summary.records.push_back({record.source, record.data.bytes().size()});
  // Do not serialize the parsed document again or alter login projection:
  // even opaque/unregistered component payloads retain their exact bytes.
  SensitiveBytes envelope(SealBackup(plaintext, new_password));
  return PreparedBackupRekey(std::move(envelope), target, std::move(summary));
}
}  // namespace gbrowser
