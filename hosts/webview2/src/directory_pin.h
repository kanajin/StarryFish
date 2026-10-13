#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>

namespace gbrowser::webview {
// Directory read access participates in Windows sharing checks; attributes-only
// access does not. Share writes for Chromium, hold back delete/rename access.
class DirectoryPins {
 public:
  ~DirectoryPins();
  DirectoryPins() = default;
  DirectoryPins(const DirectoryPins&) = delete;
  DirectoryPins& operator=(const DirectoryPins&) = delete;
  // Returns the normalized on-disk spelling of the pinned leaf. Chromium's
  // unpacked IDs hash path case, although Windows opens paths case-insensitively.
  std::filesystem::path Tree(const std::filesystem::path& path);
  void CreateTree(const std::filesystem::path& path);
  std::filesystem::path One(const std::filesystem::path& path);
 private:
  std::vector<HANDLE> handles_;
};
}
