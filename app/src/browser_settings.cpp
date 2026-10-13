#include "browser_settings.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace gbrowser {
namespace {
constexpr char kFile[] = "Gbrowser-settings.bin";
constexpr std::array<uint8_t, 8> kMagic{'G','B','R','S','E','T','0','1'};
constexpr size_t kSize = 20;
void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
struct File {
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
  File() = default;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
};
void Regular(HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info{};
  Require(GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks == 1,
      "设置文件类型异常，请保留文件并停止操作。");
}
BackupAdapter Capability() { return {{BackupSourceKind::BrowserSettings, "gbrowser", 1}, true, true, false, false}; }
void CheckPlan(const BackupPlan& plan) {
  const auto& sources = plan.sources();
  Require(plan.settings_only() && !plan.include_login() && sources.size() == 1 &&
      sources[0].kind == BackupSourceKind::BrowserSettings && sources[0].id == "gbrowser" && sources[0].schema == 1,
      "设置快照不能用于完整资料或登录态备份。");
}
class SettingsLease final : public BackupCaptureLease {
 public:
  SettingsLease(Profile profile, BrowserSettings settings) : profile_(std::move(profile)), settings_(settings) {
    ValidateBrowserSettings(settings_);
  }
  Profile profile() const override { return profile_; }
  void Check(const BackupPlan& plan) const override { CheckPlan(plan); }
  const BrowserSettings& settings() const { return settings_; }
 private:
  const Profile profile_;
  const BrowserSettings settings_;
};
const SettingsLease& Lease(const BackupCaptureLease& lease) {
  const auto* settings = dynamic_cast<const SettingsLease*>(&lease);
  Require(settings != nullptr, "Invalid settings snapshot provider");
  return *settings;
}
std::vector<BackupRestoreAdapter> RestoreAdapters() {
  return {{Capability(), [](const SensitiveBytes& bytes, NewProfileStage& stage, bool login) {
    Require(!login, "设置导入不包含登录态。");
    const auto settings = DecodeBrowserSettings(bytes);
    stage.Write(kFile, EncodeBrowserSettings(settings));
  }, [](const SensitiveBytes& bytes, const NewProfileStage& stage, bool login) {
    return !login && DecodeBrowserSettings(bytes) == DecodeBrowserSettings(stage.Read(kFile));
  }, [](const SensitiveBytes& bytes, bool login) {
    Require(!login, "设置导入不包含登录态。"); (void)DecodeBrowserSettings(bytes);
  }}};
}
}  // namespace
bool operator==(const BrowserSettings& a, const BrowserSettings& b) {
  return a.window_width == b.window_width && a.window_height == b.window_height && a.tarou_percent == b.tarou_percent;
}
void ValidateBrowserSettings(const BrowserSettings& settings) {
  Require(settings.window_width >= 760 && settings.window_width <= 3840 &&
      settings.window_height >= 420 && settings.window_height <= 2160 &&
      settings.tarou_percent >= 25 && settings.tarou_percent <= 60,
      "窗口宽度须为760–3840、高度420–2160，太郎面板比例25–60%。");
}
BrowserSettings WithWindowSize(const BrowserSettings& current, int64_t pixel_width, int64_t pixel_height, unsigned dpi) {
  ValidateBrowserSettings(current);
  Require(dpi > 0 && dpi <= 768 && pixel_width > 0 && pixel_width <= 30720 &&
      pixel_height > 0 && pixel_height <= 17280, "无法核对当前窗口尺寸或DPI，请手动输入有效数值。");
  auto proposal = current;
  proposal.window_width = static_cast<uint32_t>((pixel_width * 96 + dpi / 2) / dpi);
  proposal.window_height = static_cast<uint32_t>((pixel_height * 96 + dpi / 2) / dpi);
  ValidateBrowserSettings(proposal); return proposal;
}
SensitiveBytes EncodeBrowserSettings(const BrowserSettings& settings) {
  ValidateBrowserSettings(settings);
  BackupBytes bytes(kSize);
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
  const std::array<uint32_t, 3> values{settings.window_width, settings.window_height, settings.tarou_percent};
  for (size_t n = 0; n < values.size(); ++n) for (size_t i = 0; i < 4; ++i)
    bytes[8 + 4 * n + i] = static_cast<uint8_t>(values[n] >> (i * 8));
  return SensitiveBytes(std::move(bytes));
}
BrowserSettings DecodeBrowserSettings(const SensitiveBytes& encoded) {
  const auto& bytes = encoded.bytes();
  Require(bytes.size() == kSize && std::equal(kMagic.begin(), kMagic.end(), bytes.begin()),
      "设置格式或版本不支持，请保留原文件。");
  std::array<uint32_t, 3> values{};
  for (size_t n = 0; n < values.size(); ++n) for (size_t i = 0; i < 4; ++i)
    values[n] |= static_cast<uint32_t>(bytes[8 + 4 * n + i]) << (i * 8);
  BrowserSettings settings{values[0], values[1], values[2]}; ValidateBrowserSettings(settings); return settings;
}
BrowserSettings LoadBrowserSettings(const ProfileStore& store, const std::string& id) {
  const auto path = store.CachePath(id) / kFile; RequireSafePath(path);
  File file; file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file.handle == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
    throw std::runtime_error("无法读取Gbrowser设置，请保留文件并正常关闭后重试。");
  }
  Regular(file.handle);
  LARGE_INTEGER size{};
  Require(GetFileSizeEx(file.handle, &size) && size.QuadPart == static_cast<LONGLONG>(kSize), "设置文件长度异常，请保留原文件。");
  BackupBytes bytes(kSize); DWORD read = 0;
  Require(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size(),
      "无法完整读取设置文件。");
  return DecodeBrowserSettings(SensitiveBytes(std::move(bytes)));
}
SavedBrowserSettings ReadSavedBrowserSettings(const ProfileStore& store, const std::string& id) {
  const auto profiles = store.List();
  const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const Profile& profile) { return profile.id == id; });
  Require(found != profiles.end(), "所选资料不存在，设置导出已停止。请重新打开资料列表。");
  return {*found, LoadBrowserSettings(store, id)};
}
void SaveBrowserSettings(const ProfileStore& store, const std::string& id, const BrowserSettings& settings) {
  const auto encoded = EncodeBrowserSettings(settings);
  (void)LoadBrowserSettings(store, id);  // Never overwrite a damaged/linked existing file.
  const auto path = store.CachePath(id) / kFile;
  std::array<uint8_t, 16> random{};
  Require(BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
      "无法准备设置保存。");
  std::wstring name = L"gbrowser-settings-";
  constexpr wchar_t hex[] = L"0123456789abcdef";
  for (auto byte : random) { name += hex[byte >> 4]; name += hex[byte & 15]; }
  const auto pending = path.parent_path() / (name + L".pending"); RequireSafePath(pending);
  File file; file.handle = CreateFileW(pending.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Require(file.handle != INVALID_HANDLE_VALUE, "无法创建设置保存文件，现有设置保留。");
  Regular(file.handle); DWORD written = 0;
  Require(WriteFile(file.handle, encoded.bytes().data(), static_cast<DWORD>(encoded.bytes().size()), &written, nullptr) &&
      written == encoded.bytes().size() && FlushFileBuffers(file.handle), "设置写入失败，现有设置与暂存证据保留。");
  RequireSafePath(pending); RequireSafePath(path);
  const auto target = path.native();
  Require(target.size() <= 32767, "设置路径过长。");
  auto memory = std::make_unique<uint8_t[]>(sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t));
  auto* rename = new (memory.get()) FILE_RENAME_INFO{};
  rename->ReplaceIfExists = TRUE; rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
  memcpy(rename->FileName, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
  // The exclusive staging handle stays open through publication. Only this
  // product-owned file is replaced; Chromium Preferences/databases are untouched.
  Require(SetFileInformationByHandle(file.handle, FileRenameInfo, rename,
      static_cast<DWORD>(sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t))),
      "设置发布失败，现有设置与暂存证据保留。");
}
PreparedBackupExport PrepareBrowserSettingsExport(const Profile& profile, const BrowserSettings& settings,
    const std::filesystem::path& destination, const SensitiveBytes& password) {
  ValidateBrowserSettings(settings);
  const auto plan = MakeSettingsBackupPlan({Capability()});
  const std::vector<BackupCaptureAdapter> capture{{Capability(), [](const BackupCaptureLease& lease, bool login) {
    Require(!login, "设置备份不包含登录态。"); return EncodeBrowserSettings(Lease(lease).settings());
  }, [](const SensitiveBytes& bytes, const BackupCaptureLease& lease, bool login) {
    return !login && DecodeBrowserSettings(bytes) == Lease(lease).settings();
  }}};
  return PrepareBackupExport(profile, destination, password, plan, capture, RestoreAdapters(),
      [settings](const Profile& source, const BackupPlan& requested) {
        CheckPlan(requested); return std::make_unique<SettingsLease>(source, settings);
      });
}
PreparedBackupImport PrepareBrowserSettingsImport(const std::filesystem::path& source, const SensitiveBytes& password) {
  return PrepareBackupImport(source, password, MakeSettingsBackupPlan({Capability()}), RestoreAdapters());
}
Profile PublishBrowserSettingsImport(PreparedBackupImport& prepared, ProfileStore& store,
                                    const std::optional<std::wstring>& target_name) {
  return prepared.Publish(store, RestoreAdapters(), target_name);
}
}  // namespace gbrowser
