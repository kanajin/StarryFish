#include "extension_page.h"
#include "core.h"
#include <windows.h>
#include "file_digest.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace gbrowser {
namespace {
void Require(bool ok) {
  if (!ok) throw std::runtime_error("扩展来源或页面范围核对失败，未打开组件页面。请保留原文件，不要修改权限或签名。");
}
class File {
 public:
  explicit File(HANDLE value) : value_(value) {}
  ~File() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  File(File&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  HANDLE get() const { return value_; }
 private:
  HANDLE value_;
};
std::filesystem::path LocalSource(const std::filesystem::path& source) {
  auto normalized = source.lexically_normal(); normalized.make_preferred();
  Require(source.is_absolute() && normalized == source && source.native().size() < MAX_PATH &&
          source.root_name().native().size() == 2 && source.root_name().native()[1] == L':');
  RequireSafePath(source);
  const auto drive = GetDriveTypeW(source.root_path().c_str());
  Require(drive == DRIVE_FIXED || drive == DRIVE_REMOVABLE);
  return normalized;
}
File Open(const std::filesystem::path& path, bool directory) {
  RequireSafePath(path);
  File file(CreateFileW(path.c_str(), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
      directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  Require(file.get() != INVALID_HANDLE_VALUE && GetFileType(file.get()) == FILE_TYPE_DISK &&
      GetFileInformationByHandle(file.get(), &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
      (directory || info.nNumberOfLinks == 1));
  return file;
}
bool AssetName(const std::string& name) {
  return !name.empty() && name.size() < MAX_PATH && name.front() != '/' &&
      name.find("..") == std::string::npos && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '/' || c == '_' || c == '-' || c == '.' || c == '@' || c == '+';
      });
}
bool Suffix(const std::string& text) {
  return text.size() <= 4096 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
    return c >= 0x21 && c <= 0x7e && c != '\\';
  });
}
}
size_t SelectComponentSource(const std::vector<DevelopmentExtensionState>& states) {
  if (states.empty() || states.size() > 8) throw std::runtime_error("Invalid component source candidates");
  std::optional<size_t> registered, enabled;
  for (size_t index = 0; index < states.size(); ++index) {
    if (states[index] == DevelopmentExtensionState::Unavailable)
      throw std::runtime_error("无法可靠核对组件登记；请在原生管理页检查，不会选择替代目录。");
    if (states[index] == DevelopmentExtensionState::Missing) continue;
    if (!registered) registered = index;
    if (states[index] == DevelopmentExtensionState::Enabled) {
      if (enabled) throw std::runtime_error("同一组件有多个固定版本启用。请在原生管理页明确保留一个启用版本；不会自动停用或迁移数据。");
      enabled = index;
    } else if (states[index] != DevelopmentExtensionState::Disabled)
      throw std::runtime_error("Unknown component registration state");
  }
  return enabled.value_or(registered.value_or(0));
}
struct ExtensionPage::Source { std::vector<File> files; };
std::string UnpackedExtensionId(const std::filesystem::path& source) {
  // Chromium crx_file::id_util::GenerateIdForPath: native Windows UTF-16,
  // upper-case drive letter, SHA256 first 16 bytes mapped to a-p.
  auto value = source.native();
  Require(source.is_absolute() && value.size() >= 3 && value[1] == L':');
  if (value[0] >= L'a' && value[0] <= L'z') value[0] -= L'a' - L'A';
  static_assert(sizeof(wchar_t) == 2);
  FileDigest hash; hash.Add(value.data(), value.size() * sizeof(wchar_t));
  auto id = hash.Finish().substr(0, 32);
  for (auto& c : id) c = static_cast<char>('a' + (c <= '9' ? c - '0' : c - 'a' + 10));
  return id;
}
ExtensionPage::ExtensionPage(std::string origin, std::string options,
    std::vector<std::string> assets, std::string profile, std::filesystem::path cache, int target,
    std::shared_ptr<Source> held_source)
    : origin_(std::move(origin)), url_(origin_ + '/' + options + (target ? "?" + std::to_string(target) : "")),
      profile_(std::move(profile)), cache_(std::move(cache)), target_(target),
      assets_(std::move(assets)), held_source_(std::move(held_source)) {}
bool ExtensionPage::AllowsPage(const std::string& url) const {
  return url == url_ || (url.compare(0, url_.size() + 1, url_ + '#') == 0 && Suffix(url));
}
bool ExtensionPage::AllowsAsset(const std::string& url) const {
  if (url.size() > 4096 || url.compare(0, origin_.size() + 1, origin_ + '/') != 0 || !Suffix(url)) return false;
  const auto path = url.substr(origin_.size() + 1, url.find_first_of("?#", origin_.size() + 1) - origin_.size() - 1);
  return std::binary_search(assets_.begin(), assets_.end(), path);
}
bool ExtensionPage::AllowsRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download) const {
  return !download && method == "GET" &&
      (navigation ? AllowsPage(url) : (initiator == origin_ && AllowsAsset(url)));
}
bool ExtensionPage::MatchesProfile(const std::string& profile, const std::filesystem::path& cache) const {
  return profile == profile_ && cache == cache_;
}
std::shared_ptr<const ExtensionPage> ExtensionPage::Auxiliary(const std::string& url) const {
  // The user-owned original and embedded arcarum workers both retain the
  // explicit action/OPEN_PANEL path. Reuse their checked panel and source locks,
  // without admitting preview queries or deriving a game-target capability.
  if (url_ == origin_ + "/src/panel/index.html" && url == url_)
    return std::shared_ptr<const ExtensionPage>(new ExtensionPage(origin_, "src/panel/index.html",
        assets_, profile_, cache_, 0, held_source_));
  if (url_ != origin_ + "/src/views/sidePanel/main.html") return nullptr;
  // Exact component keys in the locked 3.7.1 popup.js registry. Debug and
  // WindowPanel are deliberately absent: neither is an auxiliary game tool.
  static constexpr std::array<const char*, 12> pages{
      "ArtifactBox", "ArtifactRule", "BuildCompare", "DungeonMap", "ExportRecord",
      "GachaRecord", "NpcDetail", "RecoveryItem", "RewardList", "SampoSetup",
      "SupportSummon", "TeamraidRecord"};
  const std::string entry = "src/views/popup/main.html";
  if (!std::binary_search(assets_.begin(), assets_.end(), entry)) return nullptr;
  for (const auto* name : pages) if (url == origin_ + '/' + entry + '?' + name)
    return std::shared_ptr<const ExtensionPage>(new ExtensionPage(origin_, entry + '?' + name,
        assets_, profile_, cache_, 0, held_source_));
  return nullptr;
}
std::shared_ptr<const ExtensionPage> PrepareExtensionPage(
    const std::filesystem::path& source, const std::string& tree_sha256,
    size_t file_count, size_t content_bytes, const std::string& options_file,
    const std::string& profile, const std::filesystem::path& cache, int target) {
  // Tarou's original WindowPanel document accepts exactly one numeric tabId.
  // This creates an embedded document, not a Chrome SIDE_PANEL context.
  Require(tree_sha256.size() == 64 && file_count && file_count <= 1024 && content_bytes <= 32 * 1024 * 1024 &&
          AssetName(options_file) && !profile.empty() && cache.is_absolute() && target >= 0 &&
          (!target || options_file == "src/views/sidePanel/main.html"));
  const auto root = LocalSource(source);
  auto held = std::make_shared<ExtensionPage::Source>();
  // Hold each ancestor before walking children. No checked file/ancestor can
  // then be renamed by a cooperative updater until all owning tabs drain.
  std::vector<std::filesystem::path> ancestors{root};
  for (auto path = root.parent_path(); !path.empty(); path = path.parent_path()) {
    ancestors.push_back(path); if (path == path.parent_path()) break;
  }
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) held->files.push_back(Open(*it, true));
  std::vector<std::pair<std::string, std::string>> entries;
  size_t bytes = 0, visited = 0;
  for (auto it = std::filesystem::recursive_directory_iterator(root);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    Require(++visited <= 2048 && it.depth() <= 16);
    const auto name = it->path().lexically_relative(root).generic_u8string();
    Require(AssetName(name));
    const bool directory = it->is_directory();
    auto file = Open(it->path(), directory);
    if (!directory) {
      Require(entries.size() < file_count);
      LARGE_INTEGER size{};
      Require(GetFileSizeEx(file.get(), &size) && size.QuadPart >= 0 &&
          static_cast<unsigned long long>(size.QuadPart) <= content_bytes - bytes);
      FileDigest hash; std::array<unsigned char, 65536> buffer{}; size_t read_bytes = 0;
      for (;;) {
        DWORD read = 0;
        Require(ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr));
        if (!read) break;
        Require(read <= content_bytes - bytes - read_bytes);
        hash.Add(buffer.data(), read); read_bytes += read;
      }
      Require(read_bytes == static_cast<size_t>(size.QuadPart)); bytes += read_bytes;
      entries.emplace_back(name, hash.Finish());
    }
    held->files.push_back(std::move(file));
  }
  Require(entries.size() == file_count && bytes == content_bytes);
  std::sort(entries.begin(), entries.end());
  std::string tree = "["; std::vector<std::string> assets;
  for (const auto& entry : entries) {
    if (!assets.empty()) tree += ',';
    tree += "[\"" + entry.first + "\",\"" + entry.second + "\"]";
    assets.push_back(entry.first);
  }
  tree += ']'; FileDigest hash; hash.Add(tree.data(), tree.size());
  Require(hash.Finish() == tree_sha256 && std::binary_search(assets.begin(), assets.end(), options_file));
  return std::shared_ptr<const ExtensionPage>(new ExtensionPage(
      "chrome-extension://" + UnpackedExtensionId(root), options_file, std::move(assets), profile, cache, target, held));
}
std::shared_ptr<const ExtensionPage> ExtensionPageCache::Prepare(
    const std::filesystem::path& source, const std::string& tree_sha256,
    size_t file_count, size_t content_bytes, const std::string& options_file,
    const std::string& profile, const std::filesystem::path& cache, int target) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
      [](const Entry& entry) { return entry.page.expired(); }), entries_.end());
  for (const auto& entry : entries_) {
    if (entry.source == source && entry.cache == cache && entry.tree == tree_sha256 &&
        entry.files == file_count && entry.bytes == content_bytes && entry.options == options_file &&
        entry.profile == profile && entry.target == target)
      if (auto page = entry.page.lock()) return page;
  }
  // Exact keys originate in trusted product code. Prepare validates canonical
  // paths and keeps every checked file/ancestor locked; unknown added files
  // never enter its asset list. An expired key therefore requires a fresh hash.
  auto page = PrepareExtensionPage(source, tree_sha256, file_count, content_bytes,
      options_file, profile, cache, target);
  if (entries_.size() < 32)
    entries_.push_back({source, cache, tree_sha256, options_file, profile, file_count, content_bytes, target, page});
  return page;  // A full cache affects performance only, never admission.
}
bool IsPageUrlAllowed(const std::string& url, const std::shared_ptr<const ExtensionPage>& extension) {
  return IsOfflineUrl(url) || (extension && extension->AllowsPage(url));
}
}
