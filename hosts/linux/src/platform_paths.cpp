#include "platform_paths.h"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace starryfish::linux_host {
namespace {

class Descriptor {
 public:
  explicit Descriptor(int value) : value_(value) {
    if (value_ < 0) throw std::runtime_error("Cannot open private filesystem entry");
  }
  ~Descriptor() { if (value_ >= 0) close(value_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const { return value_; }
  int release() { const int value = value_; value_ = -1; return value; }
  void replace(int value) {
    if (value < 0) throw std::runtime_error("Cannot traverse private directory");
    close(value_);
    value_ = value;
  }

 private:
  int value_;
};

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool AbsoluteClean(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.native().find('\0') != std::string::npos) return false;
  for (const auto& part : path) {
    if (part == ".." || part == ".") return false;
  }
  return true;
}

int OpenDirectory(const std::filesystem::path& path, bool create) {
  Require(AbsoluteClean(path), "Private directory must be absolute without dot segments");
  Descriptor current(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  for (const auto& part : path.relative_path()) {
    if (part.empty()) continue;
    if (create && mkdirat(current.get(), part.c_str(), 0700) < 0 && errno != EEXIST) {
      throw std::runtime_error("Cannot create private directory");
    }
    current.replace(openat(current.get(), part.c_str(),
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  }
  return current.release();
}

void CheckPrivateDirectory(int descriptor) {
  struct stat info {};
  Require(fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
              info.st_uid == geteuid() && (info.st_mode & 077) == 0,
          "Private directory ownership or permissions are unsafe");
}

void PrivateDirectory(const std::filesystem::path& path) {
  Descriptor descriptor(OpenDirectory(path, true));
  CheckPrivateDirectory(descriptor.get());
}

void CheckRegularFile(int descriptor) {
  struct stat info {};
  Require(fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == geteuid() && info.st_nlink == 1 &&
              (info.st_mode & 077) == 0,
          "Private file ownership, links or permissions are unsafe");
}

}  // namespace

Paths ResolvePaths(const std::function<std::string(const char*)>& environment) {
  const std::filesystem::path home(environment("HOME"));
  auto resolve = [&](const char* variable, const char* fallback) {
    const std::filesystem::path configured(environment(variable));
    // XDG specifies that relative values are ignored.
    if (configured.is_absolute()) {
      Require(AbsoluteClean(configured), "XDG path contains dot segments");
      return configured;
    }
    Require(AbsoluteClean(home), "An absolute HOME is required for XDG defaults");
    return home / fallback;
  };
  const std::filesystem::path runtime(environment("XDG_RUNTIME_DIR"));
  Require(AbsoluteClean(runtime), "XDG_RUNTIME_DIR must be set by the desktop session");
  return {resolve("XDG_CONFIG_HOME", ".config") / "StarryFish",
          resolve("XDG_DATA_HOME", ".local/share") / "StarryFish",
          resolve("XDG_CACHE_HOME", ".cache") / "StarryFish",
          runtime / "StarryFish"};
}

void PreparePaths(const Paths& paths) {
  Require(geteuid() != 0, "StarryFish must not run as root");
  // The session runtime directory must already exist. Never invent a /tmp fallback.
  Descriptor runtime(OpenDirectory(paths.runtime.parent_path(), false));
  CheckPrivateDirectory(runtime.get());
  for (const auto& path : {paths.config, paths.data, paths.cache, paths.runtime}) {
    PrivateDirectory(path);
  }
  PrivateDirectory(paths.data / "UserData");
}

ProfileLock::ProfileLock(const std::filesystem::path& data) {
  Descriptor directory(OpenDirectory(data, false));
  CheckPrivateDirectory(directory.get());
  Descriptor lock(openat(directory.get(), "profile.lock",
                         O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
  CheckRegularFile(lock.get());
  Require(flock(lock.get(), LOCK_EX | LOCK_NB) == 0,
          "This Linux profile is already in use");
  descriptor_ = lock.release();
}

ProfileLock::~ProfileLock() {
  // Do not unlink: all processes must contend for the same inode.
  if (descriptor_ >= 0) close(descriptor_);
}

void AtomicWrite(const std::filesystem::path& directory,
                 const std::string& filename, const std::string& contents) {
  Require(!filename.empty() && filename != "." && filename != ".." &&
              filename.find('/') == std::string::npos &&
              filename.find('\\') == std::string::npos &&
              filename.find('\0') == std::string::npos,
          "Invalid private filename");
  Descriptor parent(OpenDirectory(directory, false));
  CheckPrivateDirectory(parent.get());
  struct stat existing {};
  if (fstatat(parent.get(), filename.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
    Require(S_ISREG(existing.st_mode) && existing.st_uid == geteuid() &&
                existing.st_nlink == 1 && (existing.st_mode & 077) == 0,
            "Refusing to replace an unsafe private file");
  } else {
    Require(errno == ENOENT, "Cannot inspect private file");
  }
  // O_EXCL prevents following or replacing any pre-existing temporary entry.
  std::string temporary;
  int fd = -1;
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    temporary = ".write-" + std::to_string(getpid()) + "-" + std::to_string(attempt);
    fd = openat(parent.get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0 || errno != EEXIST) break;
  }
  Descriptor output(fd);
  try {
    size_t offset = 0;
    while (offset < contents.size()) {
      const auto count = write(output.get(), contents.data() + offset, contents.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      Require(count > 0, "Cannot write private file");
      offset += static_cast<size_t>(count);
    }
    Require(fsync(output.get()) == 0, "Cannot flush private file");
    Require(renameat(parent.get(), temporary.c_str(), parent.get(), filename.c_str()) == 0,
            "Cannot commit private file");
    Require(fsync(parent.get()) == 0, "Cannot flush private directory");
  } catch (...) {
    unlinkat(parent.get(), temporary.c_str(), 0);
    throw;
  }
}

}  // namespace starryfish::linux_host
