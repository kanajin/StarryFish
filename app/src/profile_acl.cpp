#include "core.h"
#include "browser_settings.h"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace gbrowser {
namespace fs = std::filesystem;
namespace {
void Fail(const char* message) { throw std::runtime_error(message); }
class DirectoryHandle {
 public:
  explicit DirectoryHandle(HANDLE handle) : handle_(handle) {}
  ~DirectoryHandle() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
  DirectoryHandle(const DirectoryHandle&) = delete;
  DirectoryHandle& operator=(const DirectoryHandle&) = delete;
  HANDLE get() const { return handle_; }
 private:
  HANDLE handle_;
};
}  // namespace

void RequireProfileAclPreparation(const fs::path& input) {
  RequireSafePath(input);
  const auto root = fs::absolute(input).lexically_normal();
  for (const auto& path : {root, root / L"Cache", root / L"Network"}) {
    RequireSafePath(path);
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = GetLastError();
      if (path != root && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) continue;
      Fail("目标资料目录不可检查，已停止启用该资料。请保留资料并检查路径与权限。");
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
      Fail("目标资料的根目录、Cache 或 Network 类型异常，已停止启用该资料。请保留原文件。");
    // Chromium may need to grant the network capability on Cache and Network.
    // Ask for broker ACL preparation rights but never modify the descriptor.
    DirectoryHandle directory(CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (directory.get() == INVALID_HANDLE_VALUE)
      Fail("目标资料无法通过沙箱权限准备预检查，已停止启用该资料。请保留资料并检查目录权限；不会自动修复或降级沙箱。");
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(directory.get()) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(directory.get(), &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
      Fail("目标资料目录句柄类型异常，已停止启用该资料。请保留原文件。");
  }
}

std::string PrepareChosenProfile(const ProfileStore& store, const std::string& id) {
  const auto saved = ReadSavedBrowserSettings(store, id);
  RequireProfileAclPreparation(store.CachePath(saved.profile.id));
  return saved.profile.id;
}
std::string PrepareInitialProfile(const fs::path& root) {
  ProfileStore store(root);
  // Validate a persisted selection before any default profile publication.
  auto id = store.Selected();
  const auto profiles = store.List();
  if (!id.empty()) {
    if (std::none_of(profiles.begin(), profiles.end(), [&](const Profile& profile) {
          return profile.id == id;
        }))
      Fail("启动资料不在资料列表中，已停止启动。请保留资料与选择文件，不会自动改选。");
  } else {
    id = profiles.empty() ? store.Create(L"验证资料 A").id : profiles.front().id;
  }
  return PrepareChosenProfile(store, id);
}
}  // namespace gbrowser
