#pragma once
#include "backup_restore.h"
#include <string>

namespace gbrowser {
// FR05 read-only user entry: authenticate/parse before exposing metadata.
// No restore registry, profile store or plaintext payload survives this call.
// Declarations in the file do not prove actual credential projection or restore
// compatibility. The returned description is private UI data, never a log.
std::wstring InspectBackupFile(const std::filesystem::path& source, const SensitiveBytes& password);
// Format declarations only; formatting is neither authentication nor adapter
// approval. Never exposes payload bytes and must remain private UI data.
std::wstring DescribeBackupSummary(const BackupImportSummary& summary);
}  // namespace gbrowser
