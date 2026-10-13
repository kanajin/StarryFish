#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace gbrowser {
// Candidate packaging has a build-bound marker, not a release/sandbox approval.
// It selects only this fixed layout; no mutable file may supply a data path.
std::string InstalledCandidateMarker();
bool IsInstalledCandidate(const std::filesystem::path& executable_directory);
std::filesystem::path InstalledDataRoot(const std::filesystem::path& local_app_data, bool diagnostic);
std::filesystem::path CurrentUserDataRoot(bool diagnostic);
void RequireSeparatedApplicationData(const std::filesystem::path& executable_directory,
                                     const std::filesystem::path& data_root);
// No-argument startup is reserved for a checked installed candidate. Existing
// diagnostic argv parsing and the development A/B/C location stay unchanged.
bool IsDefaultMainCommandLine(std::wstring_view command_line);
}
