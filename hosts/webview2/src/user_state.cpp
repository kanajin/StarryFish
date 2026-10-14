#include "user_state.h"
#include "core.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace gbrowser::webview {
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("Gbrowser设置文件异常，原文件及暂存证据保留"); }
struct File { HANDLE value; ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
void Regular(HANDLE file) {
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks == 1);
}
std::optional<std::string> Read(const std::filesystem::path& path) {
  RequireSafePath(path);
  File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
    throw std::runtime_error("无法读取Gbrowser设置，请保留文件并停止该操作");
  }
  Regular(file.value); LARGE_INTEGER size{};
  Require(GetFileSizeEx(file.value, &size) && size.QuadPart > 0 && size.QuadPart <= 65536);
  std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
  Require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
  return bytes;
}
void Validate(const WindowState& state) {
  Require(state.x >= -131072 && state.x <= 131072 && state.y >= -131072 && state.y <= 131072 &&
      state.width >= 400 && state.width <= 32768 && state.height >= 300 && state.height <= 32768 &&
      state.dpi >= 96 && state.dpi <= 768 && state.panel_percent > 0 && state.panel_percent < 100);
}
}
UserStateFile::UserStateFile(std::filesystem::path path) : path_(std::move(path)), bytes_(Read(path_)) {}
void UserStateFile::Save(const std::string& bytes) {
  Require(!bytes.empty() && bytes.size() <= 65536);
  Require(Read(path_) == bytes_);
  if (bytes_ && *bytes_ == bytes) return;
  std::array<unsigned char, 16> random{};
  Require(BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
  std::wstring name = L"gbrowser-state-"; constexpr wchar_t hex[] = L"0123456789abcdef";
  for (auto byte : random) { name += hex[byte >> 4]; name += hex[byte & 15]; }
  const auto pending = path_.parent_path() / (name + L".pending"); RequireSafePath(pending);
  File file{CreateFileW(pending.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
  Require(file.value != INVALID_HANDLE_VALUE); Regular(file.value); DWORD written = 0;
  Require(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size() && FlushFileBuffers(file.value));
  RequireSafePath(path_); Require(Read(path_) == bytes_);
  const auto destination = path_.native(); Require(destination.size() < 32767);
  const auto size = sizeof(FILE_RENAME_INFO) + (destination.size() + 1) * sizeof(wchar_t);
  auto memory = std::make_unique<unsigned char[]>(size);
  auto* rename = new (memory.get()) FILE_RENAME_INFO{};
  rename->ReplaceIfExists = TRUE;
  rename->FileNameLength = static_cast<DWORD>(destination.size() * sizeof(wchar_t));
  memcpy(rename->FileName, destination.c_str(), (destination.size() + 1) * sizeof(wchar_t));
  Require(SetFileInformationByHandle(file.value, FileRenameInfo, rename, static_cast<DWORD>(size)));
  // The publication handle still excludes other readers. Release it before
  // readback, which also distinguishes a completed save from an unknown result.
  Require(CloseHandle(file.value) != FALSE); file.value = INVALID_HANDLE_VALUE;
  Require(Read(path_) == std::optional<std::string>(bytes));
  bytes_ = bytes;
}
std::string EncodeWindowState(const WindowState& state) {
  Validate(state);
  return "GBWV-LAYOUT-1\n" + std::to_string(state.x) + " " + std::to_string(state.y) + " " +
      std::to_string(state.width) + " " + std::to_string(state.height) + " " + std::to_string(state.dpi) + " " +
      std::to_string(state.panel_percent) + " " + (state.maximized ? "1\n" : "0\n");
}
WindowState DecodeWindowState(const std::string& bytes) {
  Require(bytes.substr(0, 14) == "GBWV-LAYOUT-1\n");
  std::istringstream input(bytes.substr(14)); WindowState state; int maximized = -1;
  Require(static_cast<bool>(input >> state.x >> state.y >> state.width >> state.height >> state.dpi >> state.panel_percent >> maximized));
  Require(maximized == 0 || maximized == 1); state.maximized = maximized == 1;
  Require(EncodeWindowState(state) == bytes); return state;
}
std::string EncodePaneState(const PaneState& state) {
  Require(state.group_percent > 0 && state.group_percent < 100 && state.tarou_share > 0 && state.tarou_share < 10000);
  Require(state.game_width >= 0 && state.game_width <= 32768 && state.game_dpi >= 96 && state.game_dpi <= 768);
  Require(state.tarou_width >= 0 && state.tarou_width <= 32768 && state.tarou_dpi >= 96 && state.tarou_dpi <= 768);
  Require(state.secondary_menu_width >= 0 && state.secondary_menu_width <= 32768 &&
      state.secondary_menu_dpi >= 96 && state.secondary_menu_dpi <= 768);
  return "GBWV-PANES-5\n" + std::string(state.moba_visible ? "1 " : "0 ") + (state.dual ? "1 " : "0 ") +
      (state.tarou ? "1 " : "0 ") + (state.arcarum ? "1 " : "0 ") + (state.tarou_detached ? "1 " : "0 ") +
      std::to_string(state.group_percent) + " " + std::to_string(state.tarou_share) + " " +
      std::to_string(state.game_width) + " " + std::to_string(state.game_dpi) + " " +
      std::to_string(state.tarou_width) + " " + std::to_string(state.tarou_dpi) + " " +
      (state.secondary_menu_hidden ? "1 " : "0 ") + std::to_string(state.secondary_menu_width) + " " +
      std::to_string(state.secondary_menu_dpi) + "\n";
}
PaneState DecodePaneState(const std::string& bytes, unsigned legacy_percent) {
  const bool v1 = bytes.substr(0, 13) == "GBWV-PANES-1\n", v2 = bytes.substr(0, 13) == "GBWV-PANES-2\n";
  const bool v3 = bytes.substr(0, 13) == "GBWV-PANES-3\n";
  const bool v4 = bytes.substr(0, 13) == "GBWV-PANES-4\n";
  Require(v1 || v2 || v3 || v4 || bytes.substr(0, 13) == "GBWV-PANES-5\n");
  std::istringstream in(bytes.substr(13)); int m = -1, d = -1, l = 0, t = -1, a = -1, detached = 0;
  Require(static_cast<bool>(in >> m >> d));
  if (v1 || v2) Require(static_cast<bool>(in >> l)); // Validated then discarded: continuous linking was retired.
  Require(static_cast<bool>(in >> t >> a));
  for (const auto value : {m, d, l, t, a}) Require(value == 0 || value == 1);
  PaneState state{m != 0, d != 0, t != 0, a != 0};
  const auto old_flags = std::string(m ? "1 " : "0 ") + (d ? "1 " : "0 ") +
      (l ? "1 " : "0 ") + (t ? "1 " : "0 ") + (a ? "1" : "0");
  if (v1) {
    Require("GBWV-PANES-1\n" + old_flags + "\n" == bytes && legacy_percent > 0 && legacy_percent < 100);
    const unsigned helpers = (t ? legacy_percent : 0) + (a ? 100U : 0);
    const unsigned games = (100 - legacy_percent) * (d ? 2U : 1U);
    state.group_percent = helpers ? (std::clamp)(helpers * 100 / (games + helpers), 1U, 99U) : legacy_percent;
    state.tarou_share = (std::clamp)(legacy_percent * 100 / (legacy_percent + 100), 1U, 99U) * 100;
  } else {
    Require(static_cast<bool>(in >> detached >> state.group_percent >> state.tarou_share));
    Require(detached == 0 || detached == 1); state.tarou_detached = detached == 1;
    if (v2) {
      Require(state.group_percent > 0 && state.group_percent < 100 && state.tarou_share > 0 && state.tarou_share < 100);
      Require("GBWV-PANES-2\n" + old_flags + (detached ? " 1 " : " 0 ") + std::to_string(state.group_percent) +
          " " + std::to_string(state.tarou_share) + "\n" == bytes);
      state.tarou_share *= 100;
    } else {
      Require(static_cast<bool>(in >> state.game_width >> state.game_dpi));
      if (!v3) Require(static_cast<bool>(in >> state.tarou_width >> state.tarou_dpi));
      if (v3 || v4) {
        const auto canonical = EncodePaneState(state); // Validate old bounded fields too.
        const auto end = canonical.size() - std::string(" 1 0 96\n").size() -
            (v3 ? std::string(" 0 96").size() : 0);
        Require(std::string(v3 ? "GBWV-PANES-3\n" : "GBWV-PANES-4\n") +
            canonical.substr(13, end - 13) + "\n" == bytes);
      } else {
        int hidden = -1;
        Require(static_cast<bool>(in >> hidden >> state.secondary_menu_width >> state.secondary_menu_dpi));
        Require(hidden == 0 || hidden == 1); state.secondary_menu_hidden = hidden != 0;
        Require(EncodePaneState(state) == bytes);
      }
    }
  }
  return state;
}
std::string EncodeDarkAppearance(bool dark) { return dark ? "GBWV-APPEARANCE-1\ndark\n" : "GBWV-APPEARANCE-1\nlight\n"; }
std::string EncodeBrowserLayout(bool browser_layout) {
  return browser_layout ? "GBWV-LAYOUT-1\n2\n" : "GBWV-LAYOUT-1\n1\n";
}
bool DecodeBrowserLayout(const std::string& bytes) {
  Require(bytes == EncodeBrowserLayout(false) || bytes == EncodeBrowserLayout(true));
  return bytes == EncodeBrowserLayout(true);
}
bool DecodeDarkAppearance(const std::string& bytes) {
  Require(bytes == EncodeDarkAppearance(false) || bytes == EncodeDarkAppearance(true));
  return bytes == EncodeDarkAppearance(true);
}
std::string EncodeHiddenLoading(bool hidden) {
  return hidden ? "STARRYFISH-LOADING-1\nhidden\n" : "STARRYFISH-LOADING-1\nvisible\n";
}
bool DecodeHiddenLoading(const std::string& bytes) {
  Require(bytes == EncodeHiddenLoading(false) || bytes == EncodeHiddenLoading(true));
  return bytes == EncodeHiddenLoading(true);
}

}
