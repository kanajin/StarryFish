#include "application_layout.h"
#include "core.h"
#include "file_digest.h"
#include "local_file.h"
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>

#define GBROWSER_STRINGIZE_INNER(value) #value
#define GBROWSER_STRINGIZE(value) GBROWSER_STRINGIZE_INNER(value)

namespace gbrowser {
std::string InstalledCandidateMarker() {
  return "GBROWSER-INSTALLED-CANDIDATE\nschema=1\nbuild="
      GBROWSER_STRINGIZE(GBROWSER_BUILD_INPUT_FINGERPRINT)
      "\ndata=LOCALAPPDATA/Gbrowser/UserData\n";
}
bool IsInstalledCandidate(const std::filesystem::path& executable_directory) {
  RequireSafePath(executable_directory);
  const auto marker = executable_directory / L"Gbrowser-layout.txt";
  RequireSafePath(marker);
  if (!std::filesystem::exists(std::filesystem::symlink_status(marker))) return false;
  const auto expected = InstalledCandidateMarker();
  FileDigest digest; digest.Add(expected.data(), expected.size());
  // Reject malformed, linked, stale or rewritten markers. Never fall back to
  // the development profiles after a present marker failed its identity check.
  return ReadFixedLocalFile(marker, expected.size(), digest.Finish()) == expected;
}
std::filesystem::path InstalledDataRoot(const std::filesystem::path& local_app_data, bool diagnostic) {
  if (!local_app_data.is_absolute() || local_app_data.root_name().native().size() != 2)
    throw std::runtime_error("Current-user data requires a standard absolute Windows path");
  for (const auto& part : local_app_data)
    if (part == L"." || part == L"..") throw std::runtime_error("Invalid current-user data path");
  RequireSafePath(local_app_data);
  const auto root = (local_app_data / L"Gbrowser" / (diagnostic ? L"Validation" : L"UserData")).make_preferred();
  RequireSafePath(root);
  return root;
}
std::filesystem::path CurrentUserDataRoot(bool diagnostic) {
  PWSTR folder = nullptr;
  const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &folder);
  struct FolderOwner { PWSTR value; ~FolderOwner() { CoTaskMemFree(value); } } owner{folder};
  if (FAILED(result) || !folder) throw std::runtime_error("Cannot locate current-user application data");
  return InstalledDataRoot(std::filesystem::path(folder), diagnostic);
}
void RequireSeparatedApplicationData(const std::filesystem::path& executable_directory,
                                     const std::filesystem::path& data_root) {
  const auto contains = [](std::filesystem::path path, const std::filesystem::path& target) {
    for (;;) {
      if (CompareStringOrdinal(path.c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
      const auto parent = path.parent_path();
      if (parent.empty() || parent == path) return false;
      path = parent;
    }
  };
  if (contains(executable_directory, data_root) || contains(data_root, executable_directory))
    throw std::runtime_error("Application files and user data directories must be separate");
}
}
