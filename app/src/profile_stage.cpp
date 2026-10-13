#include "profile_stage.h"
#include "core.h"
#include <windows.h>
#include <algorithm>
#include <set>
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
std::string Relative(std::string path) {
  Require(!path.empty() && path.size() <= 200, "Invalid staged profile path length");
  for (auto& c : path) {
    Require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/',
        "Unsupported staged profile path character");
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  size_t begin = 0;
  unsigned depth = 0;
  do {
    const auto end = path.find('/', begin);
    const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
    Require(!part.empty() && part.size() <= 64 && part != "." && part != ".." &&
        part.back() != '.' && ++depth <= 8, "Invalid staged profile path component");
    const auto stem = part.substr(0, part.find('.'));
    const bool device = stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
         stem[3] >= '1' && stem[3] <= '9');
    Require(!device, "Staged profile device path rejected");
    if (end == std::string::npos) break;
    begin = end + 1;
  } while (true);
  Require(path != "name.txt" && path.rfind("name.txt/", 0) != 0,
          "Staged profile metadata is reserved");
  return path;
}
void Regular(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
      info.nNumberOfLinks == 1, "Staged profile file is not regular and single-link");
}
SensitiveBytes ReadFileBytes(HANDLE handle, size_t limit = kBackupPayloadLimit) {
  Regular(handle);
  LARGE_INTEGER size{};
  Require(GetFileSizeEx(handle, &size) && size.QuadPart >= 0 &&
      size.QuadPart <= static_cast<LONGLONG>(limit), "Staged file size is invalid");
  // SensitiveBytes exposes a const view. The input vector is transferred to
  // RAII ownership on success and wiped explicitly on a partial read failure.
  BackupBytes bytes(static_cast<size_t>(size.QuadPart));
  DWORD read = 0;
  if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) {
    SensitiveBytes wipe(std::move(bytes));
    throw std::runtime_error("Cannot read complete staged profile file");
  }
  return SensitiveBytes(std::move(bytes));
}
SensitiveBytes ReadFileBytes(const fs::path& path, size_t limit = kBackupPayloadLimit) {
  RequireSafePath(path);
  File file;
  file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Require(file.handle != INVALID_HANDLE_VALUE, "Cannot read staged profile file");
  return ReadFileBytes(file.handle, limit);
}
}  // namespace

void NewProfileStage::Write(const std::string& relative, const SensitiveBytes& data) {
  try {
    Require(!failed_ && !sealed_, "Staged profile is stopped or sealed");
    const auto name = Relative(relative);
    Require(entries_.size() < 512 && data.bytes().size() <= kBackupPayloadLimit - total_,
            "Staged profile exceeds file or byte limit");
    Require(std::none_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.path == name; }),
            "Staged profile file already written; overwrite rejected");
    Entry entry{name, SensitiveBytes(BackupBytes(data.bytes()))};
    // Reserve tracking before doing I/O; allocation failure must not leave an
    // untracked file that a caller could mistake for successful preparation.
    entries_.reserve(entries_.size() + 1);
    const auto path = root_ / Utf8ToWide(name);
    RequireSafePath(path);
    fs::create_directories(path.parent_path());
    RequireSafePath(path);
    File file;
    file.handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    Require(file.handle != INVALID_HANDLE_VALUE, "Cannot create staged profile file; evidence preserved");
    Regular(file.handle);
    DWORD written = 0;
    Require(WriteFile(file.handle, data.bytes().data(), static_cast<DWORD>(data.bytes().size()), &written, nullptr) &&
        written == data.bytes().size() && FlushFileBuffers(file.handle), "Cannot commit staged profile file");
    LARGE_INTEGER zero{};
    Require(SetFilePointerEx(file.handle, zero, nullptr, FILE_BEGIN), "Cannot rewind staged profile file");
    const auto actual = ReadFileBytes(file.handle);
    Require(actual.bytes() == entry.expected.bytes(), "Staged profile write readback mismatch");
    entries_.push_back(std::move(entry));
    total_ += data.bytes().size();
  } catch (...) { failed_ = true; throw; }
}
SensitiveBytes NewProfileStage::Read(const std::string& relative) const {
  try {
    Require(!failed_, "Staged profile is stopped");
    const auto name = Relative(relative);
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.path == name; });
    Require(found != entries_.end(), "Staged profile file was not prepared through this capability");
    auto actual = ReadFileBytes(root_ / Utf8ToWide(name));
    Require(actual.bytes() == found->expected.bytes(), "Staged profile file changed after writing");
    return actual;
  } catch (...) { failed_ = true; throw; }
}
void NewProfileStage::Seal() {
  Require(!failed_ && !sealed_, "Cannot seal failed staged profile");
  sealed_ = true;
  CheckIntegrity();
}
void NewProfileStage::CheckIntegrity() const {
  try {
    Require(!failed_ && sealed_, "Staged profile is not verified");
    RequireSafePath(root_);
    // Metadata is not a tracked payload, but must also be a standalone file.
    // Equal bytes on a hard link are not independent restored profile data.
    // This is a point-in-time check, not a lease against a same-user writer.
    (void)ReadFileBytes(root_ / L"name.txt", 256);
    std::set<std::string> expected{"name.txt"};
    std::set<std::string> directories;
    for (const auto& entry : entries_) {
      expected.insert(entry.path); (void)Read(entry.path);
      for (auto slash = entry.path.find('/'); slash != std::string::npos; slash = entry.path.find('/', slash + 1))
        directories.insert(entry.path.substr(0, slash));
    }
    for (const auto& item : fs::recursive_directory_iterator(root_)) {
      RequireSafePath(item.path());
      const auto name = item.path().lexically_relative(root_).generic_string();
      if (item.is_directory()) {
        Require(directories.erase(name) == 1, "Unexpected directory in staged profile; evidence preserved");
        continue;
      }
      Require(item.is_regular_file() && expected.erase(name) == 1,
              "Unexpected file in staged profile; evidence preserved");
    }
    Require(expected.empty() && directories.empty(), "Prepared profile file disappeared; evidence preserved");
  } catch (...) { failed_ = true; throw; }
}
}  // namespace gbrowser
