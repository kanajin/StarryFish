#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace starryfish::linux_host {

struct Paths {
  std::filesystem::path config;
  std::filesystem::path data;
  std::filesystem::path cache;
  std::filesystem::path runtime;
};

// No filesystem access: also used by isolated environment fixtures.
Paths ResolvePaths(const std::function<std::string(const char*)>& environment);
// Creates only application-owned subdirectories; refuses links and unsafe owners.
void PreparePaths(const Paths& paths);
void AtomicWrite(const std::filesystem::path& directory,
                 const std::string& filename, const std::string& contents);

class ProfileLock final {
 public:
  explicit ProfileLock(const std::filesystem::path& data);
  ~ProfileLock();
  ProfileLock(const ProfileLock&) = delete;
  ProfileLock& operator=(const ProfileLock&) = delete;

 private:
  int descriptor_ = -1;
};

}  // namespace starryfish::linux_host
