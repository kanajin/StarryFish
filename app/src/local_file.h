#pragma once
#include <filesystem>
#include <string_view>
#include <string>

namespace gbrowser {
// Public configuration export shares the backup writer's local-path checks,
// exclusive staging handle and no-replace publication. No profile enumeration,
// directory creation, overwrite or network access occurs. Failed staging stays
// available as evidence; callers must not retry automatically.
void SaveNewLocalFile(const std::filesystem::path& destination, std::string_view bytes);
// Capture at most 1MiB of public bytes from one read-only, single-link handle;
// require an independent exact length/digest. The returned value is owned, so
// later path replacement cannot affect export. No execution or live path lease.
std::string ReadFixedLocalFile(const std::filesystem::path& source,
                              size_t expected_bytes, const std::string& expected_sha256);
}
