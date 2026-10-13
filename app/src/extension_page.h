#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gbrowser {
enum class DevelopmentExtensionState { Missing, Disabled, Enabled, Unavailable };
// Ordered fixed sources: preserve an existing registration, prefer the unique
// enabled one, and refuse ambiguity/unknown state. This never changes prefs.
size_t SelectComponentSource(const std::vector<DevelopmentExtensionState>& states);
// Unpacked development IDs are path-derived, not signatures or publisher IDs.
std::string UnpackedExtensionId(const std::filesystem::path& source);
class ExtensionPage {
 public:
  const std::string& url() const { return url_; }
  int target() const { return target_; }
  bool AllowsPage(const std::string& url) const;
  bool AllowsAsset(const std::string& url) const;
  bool AllowsRequest(const std::string& url, const std::string& method,
                     const std::string& initiator, bool navigation, bool download) const;
  bool MatchesProfile(const std::string& profile, const std::filesystem::path& cache) const;
  // Native windows use only the original Tarou helper or arcarum panel entry
  // from this held source. No proxy page, extra query, other extension or source
  // acquires a native-window capability; original Chrome permissions still apply.
  std::shared_ptr<const ExtensionPage> Auxiliary(const std::string& url) const;
 private:
  struct Source;
  ExtensionPage(std::string origin, std::string options, std::vector<std::string> assets,
                       std::string profile, std::filesystem::path cache, int target,
                       std::shared_ptr<Source> held_source);
  const std::string origin_, url_, profile_;
  const std::filesystem::path cache_;
  const int target_;
  const std::vector<std::string> assets_;
  // Read handles prohibit modifying/removing checked files. New files are not
  // in assets_; ancestor/directory handles prohibit replacing checked paths.
  const std::shared_ptr<Source> held_source_;
  friend std::shared_ptr<const ExtensionPage> PrepareExtensionPage(
      const std::filesystem::path&, const std::string&, size_t, size_t,
      const std::string&, const std::string&, const std::filesystem::path&, int);
};
// Expectations come from trusted product code, NEVER disk receipts or prefs.
// No installation, registration, permission grant or network request occurs.
std::shared_ptr<const ExtensionPage> PrepareExtensionPage(
    const std::filesystem::path& source, const std::string& tree_sha256,
    size_t file_count, size_t content_bytes, const std::string& options_file,
    const std::string& profile, const std::filesystem::path& cache, int target = 0);
// Reuse only a live immutable capability; never cache registration or failures.
// Weak ownership lets all Windows file locks drain with the actual owners.
class ExtensionPageCache {
 public:
  std::shared_ptr<const ExtensionPage> Prepare(
      const std::filesystem::path& source, const std::string& tree_sha256,
      size_t file_count, size_t content_bytes, const std::string& options_file,
      const std::string& profile, const std::filesystem::path& cache, int target = 0);
 private:
  struct Entry {
    std::filesystem::path source, cache;
    std::string tree, options, profile;
    size_t files, bytes;
    int target;
    std::weak_ptr<const ExtensionPage> page;
  };
  std::vector<Entry> entries_;  // UI-thread only, at most 32 live keys.
};
bool IsPageUrlAllowed(const std::string& url,
                      const std::shared_ptr<const ExtensionPage>& extension = nullptr);
}
