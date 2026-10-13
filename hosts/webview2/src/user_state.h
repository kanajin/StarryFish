#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace gbrowser::webview {
// Only Gbrowser-owned bounded files under a pinned data parent. It never reads
// Chromium Preferences, cookies or databases. Compare the last loaded bytes
// before atomic replacement; unexpected changes and staging failures retain
// the original file and evidence, rather than silently repairing or retrying.
class UserStateFile {
 public:
  explicit UserStateFile(std::filesystem::path path);
  const std::optional<std::string>& bytes() const { return bytes_; }
  void Save(const std::string& bytes);
 private:
  std::filesystem::path path_;
  std::optional<std::string> bytes_;
};
struct WindowState {
  int x = 0, y = 0, width = 1200, height = 800;
  unsigned dpi = 96, panel_percent = 50;
  bool maximized = false;
};
// Global cosmetic preference; no profile or Chromium state is changed.
std::string EncodeDarkAppearance(bool dark);
bool DecodeDarkAppearance(const std::string& bytes);
std::string EncodeBrowserLayout(bool browser_layout);
bool DecodeBrowserLayout(const std::string& bytes);
std::string EncodeWindowState(const WindowState& state);
WindowState DecodeWindowState(const std::string& bytes);
struct PaneState {
  bool moba_visible = true, dual = false, tarou = true, arcarum = false;
  bool tarou_detached = false;
  unsigned group_percent = 50, tarou_share = 3300;
  // Saved physical pixels plus reference DPI avoid rounding on every resize.
  // Zero is only the initial/legacy state, resolved once after window restore.
  int game_width = 0;
  unsigned game_dpi = 96;
  int tarou_width = 0;
  unsigned tarou_dpi = 96;
  bool secondary_menu_hidden = true;
  int secondary_menu_width = 0;
  unsigned secondary_menu_dpi = 96;
};
std::string EncodePaneState(const PaneState& state);
PaneState DecodePaneState(const std::string& bytes, unsigned legacy_percent = 50);
}
