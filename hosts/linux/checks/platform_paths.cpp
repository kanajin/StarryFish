#include "platform_paths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void Check(bool ok) { if (!ok) throw std::runtime_error("Check failed"); }
template <typename F> void Reject(F function) {
  bool rejected = false;
  try { function(); } catch (const std::exception&) { rejected = true; }
  Check(rejected);
}
}

int main() {
  using namespace starryfish::linux_host;
  namespace fs = std::filesystem;
  char temporary[] = "/tmp/starryfish-paths-XXXXXX";
  const char* created = mkdtemp(temporary);
  if (!created) return 1;
  const fs::path root(created);
  try {
    fs::create_directory(root / "runtime");
    chmod((root / "runtime").c_str(), 0700);
    std::map<std::string, std::string> env{{"HOME", (root / "home").string()},
                                         {"XDG_RUNTIME_DIR", (root / "runtime").string()}};
    auto read = [&](const char* key) { return env[key]; };
    auto paths = ResolvePaths(read);
    Check(paths.data == root / "home/.local/share/StarryFish");
    env["XDG_DATA_HOME"] = "relative/ignored";
    Check(ResolvePaths(read).data == paths.data);
    env["XDG_DATA_HOME"] = (root / "data").string();
    paths = ResolvePaths(read);
    Check(paths.data == root / "data/StarryFish");
    PreparePaths(paths);
    {
      ProfileLock first(paths.data);
      Reject([&] { ProfileLock second(paths.data); });
      const pid_t child = fork();
      Check(child >= 0);
      if (child == 0) {
        try { ProfileLock competing(paths.data); }
        catch (const std::exception&) { _exit(0); }
        _exit(1);
      }
      int status = 0;
      Check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0);
    }
    { ProfileLock reopened(paths.data); }
    const auto lock_inode = fs::status(paths.data / "profile.lock");
    Check(fs::is_regular_file(lock_inode));
    fs::create_hard_link(paths.data / "profile.lock", paths.data / "lock-alias");
    Reject([&] { ProfileLock unsafe(paths.data); });
    fs::remove(paths.data / "lock-alias");
    chmod((paths.data / "profile.lock").c_str(), 0644);
    Reject([&] { ProfileLock unsafe(paths.data); });
    chmod((paths.data / "profile.lock").c_str(), 0600);
    { ProfileLock reopened(paths.data); }
    AtomicWrite(paths.config, "settings.json", "first");
    AtomicWrite(paths.config, "settings.json", "second");
    Check(ReadPrivateFile(paths.config, "settings.json", 6) == "second");
    Check(!ReadPrivateFile(paths.config, "missing.json", 100));
    Reject([&] { ReadPrivateFile(paths.config, "settings.json", 5); });
    Reject([&] { ReadPrivateFile(paths.config, "../outside", 100); });
    std::string content;
    std::ifstream(paths.config / "settings.json") >> content;
    Check(content == "second");
    Reject([&] { AtomicWrite(paths.config, "../escape", "invalid"); });
    fs::create_symlink(paths.config / "settings.json", paths.config / "linked");
    Reject([&] { AtomicWrite(paths.config, "linked", "invalid"); });
    Reject([&] { ReadPrivateFile(paths.config, "linked", 100); });
    fs::create_hard_link(paths.config / "settings.json", paths.config / "hardlink");
    Reject([&] { AtomicWrite(paths.config, "hardlink", "invalid"); });
    Reject([&] { ReadPrivateFile(paths.config, "hardlink", 100); });
    chmod(paths.cache.c_str(), 0755);
    Reject([&] { PreparePaths(paths); });
    chmod(paths.cache.c_str(), 0700);
    fs::create_directory_symlink(root / "data", root / "linked-parent");
    env["XDG_DATA_HOME"] = (root / "linked-parent").string();
    Reject([&] { PreparePaths(ResolvePaths(read)); });
    env["XDG_RUNTIME_DIR"] = "relative";
    Reject([&] { ResolvePaths(read); });
    env["XDG_RUNTIME_DIR"] = (root / "runtime").string();
    env["XDG_DATA_HOME"] = (root / "../escape").string();
    Reject([&] { ResolvePaths(read); });
    fs::remove_all(root);  // Only the mkdtemp fixture owned by this test.
    std::cout << "PASS isolated XDG paths, ownership, locks, atomic replacement and links\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "; fixture retained at " << root << '\n';
    return 1;
  }
}
