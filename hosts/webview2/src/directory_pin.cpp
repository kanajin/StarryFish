#include "directory_pin.h"
#include "core.h"
#include <array>
#include <stdexcept>

namespace gbrowser::webview {
DirectoryPins::~DirectoryPins() { for (auto handle : handles_) CloseHandle(handle); }
std::filesystem::path DirectoryPins::One(const std::filesystem::path& path) {
  RequireSafePath(path);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("无法固定独立候选目录");
  try {
    BY_HANDLE_FILE_INFORMATION info{};
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), 0);
    if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !count || count >= buffer.size())
      throw std::runtime_error("候选目录身份异常");
    std::wstring actual(buffer.data(), count);
    if (actual.compare(0, 4, L"\\\\?\\") == 0) actual.erase(0, 4);
    std::filesystem::path normalized(actual); // noexcept move after ownership transfers to handles_.
    RequireSafePath(normalized);
    auto expected = path.native();
    while (!actual.empty() && actual.back() == L'\\') actual.pop_back();
    while (!expected.empty() && expected.back() == L'\\') expected.pop_back();
    if (_wcsicmp(actual.c_str(), expected.c_str()) != 0) throw std::runtime_error("候选目录指向异常");
    handles_.push_back(handle);
    return normalized;
  } catch (...) { CloseHandle(handle); throw; }
}
std::filesystem::path DirectoryPins::Tree(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.root_name().native().size() != 2 || path.native().size() >= MAX_PATH)
    throw std::runtime_error("仅接受本机绝对候选路径");
  std::vector<std::filesystem::path> parents;
  for (auto item = path; !item.empty(); item = item.parent_path()) {
    if (parents.size() >= 32) throw std::runtime_error("候选目录层级超限");
    parents.push_back(item);
    if (item == item.parent_path()) break;
  }
  std::filesystem::path normalized;
  for (auto it = parents.rbegin(); it != parents.rend(); ++it) normalized = One(*it);
  return normalized;
}
void DirectoryPins::CreateTree(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.root_name().native().size() != 2 || path.native().size() >= MAX_PATH)
    throw std::runtime_error("仅接受本机绝对候选路径");
  RequireSafePath(path);
  std::vector<std::filesystem::path> missing;
  auto parent = path;
  while (!std::filesystem::exists(parent)) {
    if (missing.size() >= 32 || parent.empty() || parent == parent.parent_path())
      throw std::runtime_error("无法定位可核验的候选父目录");
    missing.push_back(parent); parent = parent.parent_path();
  }
  size_t depth = missing.size();
  for (auto item = parent; !item.empty(); item = item.parent_path()) {
    if (++depth > 32) throw std::runtime_error("候选目录层级超限");
    if (item == item.parent_path()) break;
  }
  Tree(parent);
  // Pin the existing ancestry first. Create and verify one leaf at a time;
  // a redirected leaf stops before creating deeper state, locks or logs.
  for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
    std::filesystem::create_directory(*it);
    One(*it);
  }
}
}
