#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace gbrowser::webview {
// Persist intent before adding a genuinely missing fixed ZeroOmega. Adding an
// extension can outlive the host; an in-memory flag would lose first setup on
// restart. A started operation is never replayed, even without a success reply.
class ZeroInitialization {
 public:
  explicit ZeroInitialization(std::filesystem::path data) : data_(std::move(data)) {}
  bool Prepare(bool missing_extension) const {
    if (Exists(Started())) return false;
    if (Exists(Prepared())) return true;
    if (!missing_extension) return false; // Existing untracked configuration stays untouched.
    Create(Prepared());
    return true;
  }
  bool Begin() const {
    if (Exists(Started())) return false;
    if (!Exists(Prepared())) throw std::runtime_error("初始配置缺少待执行记录");
    Create(Started()); // Flush before any reset/ExecuteScript can be dispatched.
    return true;
  }
 private:
  struct File { HANDLE value; ~File() { CloseHandle(value); } };
  std::filesystem::path Prepared() const { return data_ / L"zeroomega-initialization.prepared"; }
  std::filesystem::path Started() const { return data_ / L"zeroomega-initialization.started"; }
  static bool Exists(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_NOT_FOUND) return false;
      throw std::runtime_error("无法核对初始配置记录");
    }
    File held{handle}; BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1 || !GetFileSizeEx(handle, &size) || size.QuadPart != 0)
      throw std::runtime_error("初始配置记录身份异常");
    return true;
  }
  static void Create(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("无法建立初始配置单次记录");
    const auto flushed = FlushFileBuffers(handle);
    const auto closed = CloseHandle(handle);
    if (!flushed || !closed || !Exists(path)) throw std::runtime_error("初始配置记录结果未知");
  }
  std::filesystem::path data_;
};
}
