#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <filesystem>

namespace gbrowser {
// Retained for FR05 password-protected migration. Tested container/file
// primitives are not yet a complete profile adapter or a browser UI feature.
using BackupBytes = std::vector<uint8_t>;
// Move-only ownership for passwords/plaintext. Input producers must also wipe
// their own copies. No promise about paging, dumps, or external input buffers.
class SensitiveBytes {
 public:
  explicit SensitiveBytes(BackupBytes&& bytes) noexcept;
  ~SensitiveBytes();
  SensitiveBytes(SensitiveBytes&& other) noexcept;
  SensitiveBytes& operator=(SensitiveBytes&& other) noexcept;
  SensitiveBytes(const SensitiveBytes&) = delete;
  SensitiveBytes& operator=(const SensitiveBytes&) = delete;
  const BackupBytes& bytes() const { return bytes_; }
 private:
  BackupBytes bytes_;
};
inline constexpr size_t kBackupPayloadLimit = 16 * 1024 * 1024;
// Shared read-only preflight; saving rechecks the destination before I/O.
void ValidateBackupPassword(const SensitiveBytes& password);
std::filesystem::path ValidateBackupDestination(const std::filesystem::path& destination);
// Independent memory envelope, not a profile serializer or file importer.
BackupBytes SealBackup(const SensitiveBytes& payload, const SensitiveBytes& password);
SensitiveBytes OpenBackup(const BackupBytes& envelope, const SensitiveBytes& password);
// Explicit local destination; no overwrite, directory creation or profile I/O.
void SaveBackupFile(const std::filesystem::path& destination,
                    const SensitiveBytes& payload, const SensitiveBytes& password);
SensitiveBytes LoadBackupFile(const std::filesystem::path& source, const SensitiveBytes& password);
// Shared single-attempt publication for already sealed export/rekey snapshots.
// No password/plaintext/source/adapter survives confirmation. Constructor is
// restricted to trusted preparing classes; discard performs no filesystem I/O.
class PreparedBackupWrite {
 public:
  PreparedBackupWrite(const PreparedBackupWrite&) = delete;
  PreparedBackupWrite& operator=(const PreparedBackupWrite&) = delete;
  PreparedBackupWrite(PreparedBackupWrite&&) = delete;
  PreparedBackupWrite& operator=(PreparedBackupWrite&&) = delete;
  const std::filesystem::path& destination() const { return destination_; }
  void Save();
 private:
  PreparedBackupWrite(SensitiveBytes envelope, std::filesystem::path destination);
  SensitiveBytes envelope_;
  const std::filesystem::path destination_;
  bool consumed_ = false;
  friend class PreparedBackupExport;
  friend class PreparedBackupRekey;
};
}  // namespace gbrowser
