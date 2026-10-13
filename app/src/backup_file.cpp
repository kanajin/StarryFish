#include "backup.h"
#include "core.h"
#include "local_file.h"
#include "file_digest.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace gbrowser {
namespace fs = std::filesystem;
namespace {
void Require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
struct File {
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
  File() = default;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
};
bool DeviceName(std::wstring name) {
  name = name.substr(0, name.find(L'.'));
  while (!name.empty() && name.back() == L' ') name.pop_back();
  for (auto& c : name) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
  if (name == L"CON" || name == L"PRN" || name == L"AUX" || name == L"NUL" ||
      name == L"CONIN$" || name == L"CONOUT$") return true;
  return name.size() == 4 && (name.substr(0, 3) == L"COM" || name.substr(0, 3) == L"LPT") &&
      ((name[3] >= L'1' && name[3] <= L'9') || name[3] == L'¹' || name[3] == L'²' || name[3] == L'³');
}
fs::path LocalPath(const fs::path& path) {
  const auto drive = path.root_name().native();
  Require(path.is_absolute() && drive.size() == 2 && drive[1] == L':' &&
      ((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')) &&
      !path.filename().empty(), "Backup requires a standard absolute local file path");
  for (const auto& part : path.relative_path()) {
    const auto name = part.native();
    Require(!name.empty() && name != L"." && name != L".." && name.back() != L'.' && name.back() != L' ' &&
        !DeviceName(name) && std::none_of(name.begin(), name.end(), [](wchar_t c) {
          return c < 32 || c == L':' || c == L'?' || c == L'*' || c == L'"' ||
                 c == L'<' || c == L'>' || c == L'|';
        }), "Unsupported backup path component");
  }
  const auto type = GetDriveTypeW(path.root_path().c_str());
  Require(type == DRIVE_FIXED || type == DRIVE_REMOVABLE, "Network and non-local backup drives are rejected");
  RequireSafePath(path);
  return path;
}
void Regular(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
      "Backup handle is not a regular local file");
}
std::wstring RandomSuffix() {
  std::array<uint8_t, 16> bytes{};
  Require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
      BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "Backup staging randomness failed");
  constexpr wchar_t hex[] = L"0123456789abcdef";
  std::wstring text;
  for (auto byte : bytes) { text += hex[byte >> 4]; text += hex[byte & 15]; }
  return text;
}
void Publish(HANDLE file, const fs::path& target) {
  const auto name = target.native();
  Require(name.size() <= 32767, "Backup destination path is too long");
  const size_t size = sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t);
  auto storage = std::make_unique<uint8_t[]>(size);
  auto* info = new (storage.get()) FILE_RENAME_INFO{};
  info->ReplaceIfExists = FALSE; info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
  memcpy(info->FileName, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
  // Keep the exclusive source handle open through publication. A pathname
  // replacement cannot substitute another staging file during a close gap.
  Require(SetFileInformationByHandle(file, FileRenameInfo, info, static_cast<DWORD>(size)),
      "Cannot publish encrypted backup; destination unchanged and staging evidence preserved");
}
void SavePacket(const fs::path& target, const BackupBytes& packet) {
  const auto staged = LocalPath(target.parent_path() /
      (L"gbrowser-file-" + RandomSuffix() + L".pending"));
  {
    File file;
    file.handle = CreateFileW(staged.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Require(file.handle != INVALID_HANDLE_VALUE, "Cannot create encrypted backup staging file");
    Regular(file.handle);
    DWORD written = 0;
    Require(WriteFile(file.handle, packet.data(), static_cast<DWORD>(packet.size()), &written, nullptr) &&
        written == packet.size() && FlushFileBuffers(file.handle),
        "Cannot finish encrypted backup; staging evidence preserved");
    RequireSafePath(staged); RequireSafePath(target);
    Publish(file.handle, target);
  }
}
}  // namespace

fs::path ValidateBackupDestination(const fs::path& destination) {
  const auto target = LocalPath(destination);
  Require(fs::is_directory(target.parent_path()), "Backup destination directory must already exist");
  Require(!fs::exists(target), "Backup destination already exists; choose a new filename");
  return target;
}
void SaveNewLocalFile(const fs::path& destination, std::string_view bytes) {
  // This entry point is for small public configuration files, not plaintext
  // browser state or backup payloads. Encrypted exports keep their own limit.
  Require(!bytes.empty() && bytes.size() <= 1024 * 1024, "Configuration export size is invalid");
  const auto target = ValidateBackupDestination(destination);
  SavePacket(target, BackupBytes(bytes.begin(), bytes.end()));
}
std::string ReadFixedLocalFile(const fs::path& source, size_t expected_bytes,
                              const std::string& expected_sha256) {
  Require(expected_bytes && expected_bytes <= 1024 * 1024 && expected_sha256.size() == 64 &&
      std::all_of(expected_sha256.begin(), expected_sha256.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }), "Fixed public file expectation is invalid");
  const auto path = LocalPath(source);
  File file;
  file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Require(file.handle != INVALID_HANDLE_VALUE, "Cannot open fixed public file; source unchanged");
  Regular(file.handle);
  BY_HANDLE_FILE_INFORMATION info{};
  LARGE_INTEGER size{};
  Require(GetFileInformationByHandle(file.handle, &info) && info.nNumberOfLinks == 1 &&
      GetFileSizeEx(file.handle, &size) && size.QuadPart == static_cast<LONGLONG>(expected_bytes),
      "Fixed public file length or link count differs");
  std::string bytes(expected_bytes, '\0');
  DWORD read = 0;
  Require(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
      read == bytes.size(), "Cannot capture complete fixed public file");
  FileDigest hash; hash.Add(bytes.data(), bytes.size());
  Require(hash.Finish() == expected_sha256, "Fixed public file digest differs; no export performed");
  RequireSafePath(path);
  return bytes;
}
void SaveBackupFile(const fs::path& destination, const SensitiveBytes& payload, const SensitiveBytes& password) {
  const auto target = ValidateBackupDestination(destination);
  const auto packet = SealBackup(payload, password);
  SavePacket(target, packet);
}
PreparedBackupWrite::PreparedBackupWrite(SensitiveBytes envelope, fs::path destination)
    : envelope_(std::move(envelope)), destination_(std::move(destination)) {}
void PreparedBackupWrite::Save() {
  if (consumed_) throw std::runtime_error("Backup save already consumed; a second save is rejected");
  consumed_ = true;
  struct EraseEnvelope {
    SensitiveBytes& envelope;
    ~EraseEnvelope() { envelope = SensitiveBytes(BackupBytes{}); }
  } erase{envelope_};
  const auto target = ValidateBackupDestination(destination_);
  SavePacket(target, envelope_.bytes());
}
SensitiveBytes LoadBackupFile(const fs::path& source, const SensitiveBytes& password) {
  const auto path = LocalPath(source);
  File file;
  file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Require(file.handle != INVALID_HANDLE_VALUE, "Cannot open backup; file may be missing or in use");
  Regular(file.handle);
  LARGE_INTEGER size{};
  Require(GetFileSizeEx(file.handle, &size) && size.QuadPart >= 69 &&
      size.QuadPart <= static_cast<LONGLONG>(kBackupPayloadLimit + 68), "Backup file length is invalid");
  BackupBytes packet(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  Require(ReadFile(file.handle, packet.data(), static_cast<DWORD>(packet.size()), &read, nullptr) &&
      read == packet.size(), "Cannot read complete backup");
  return OpenBackup(packet, password);
}
}  // namespace gbrowser
