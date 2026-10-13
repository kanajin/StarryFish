#pragma once
#include "backup_capture.h"

namespace gbrowser {
// Product-owned preferences only. Tarou visibility is deliberately session-only
// (FR08), and Chromium storage, cookies and extension payloads are excluded.
struct BrowserSettings {
  uint32_t window_width = 1240, window_height = 840, tarou_percent = 50;
};
bool operator==(const BrowserSettings& a, const BrowserSettings& b);
void ValidateBrowserSettings(const BrowserSettings& settings);
// Convert a verified normal outer-window size into editable 96-DPI values;
// this fills a proposal only and neither persists nor resizes any window.
BrowserSettings WithWindowSize(const BrowserSettings& current, int64_t pixel_width, int64_t pixel_height, unsigned dpi);
SensitiveBytes EncodeBrowserSettings(const BrowserSettings& settings);
BrowserSettings DecodeBrowserSettings(const SensitiveBytes& bytes);
BrowserSettings LoadBrowserSettings(const ProfileStore& store, const std::string& id);
struct SavedBrowserSettings { const Profile profile; const BrowserSettings settings; };
// Resolve current metadata and read only the selected product-owned settings
// file before any modal/worker. Own values; never switch or retain store handles.
SavedBrowserSettings ReadSavedBrowserSettings(const ProfileStore& store, const std::string& id);
// Explicit user save only; missing files use defaults, damaged files are never
// silently repaired. Atomic handle-based replacement preserves failure evidence.
void SaveBrowserSettings(const ProfileStore& store, const std::string& id, const BrowserSettings& settings);
PreparedBackupExport PrepareBrowserSettingsExport(const Profile& profile, const BrowserSettings& settings,
    const std::filesystem::path& destination, const SensitiveBytes& password);
PreparedBackupImport PrepareBrowserSettingsImport(const std::filesystem::path& source, const SensitiveBytes& password);
Profile PublishBrowserSettingsImport(PreparedBackupImport& prepared, ProfileStore& store,
                                    const std::optional<std::wstring>& target_name = std::nullopt);
}  // namespace gbrowser
