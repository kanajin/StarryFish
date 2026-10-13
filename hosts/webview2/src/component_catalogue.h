#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace gbrowser::webview {
struct AcceptedComponent {
  std::wstring name, entry;
  std::string tree;
  size_t files = 0, bytes = 0;
};
// Accepted byte identities from this current user's first installation. Source
// JSON receipts never select pages/roles or acquire permissions. DPAPI supplies
// local integrity, not publisher authentication or same-user attacker defense.
class ComponentCatalogue {
 public:
  explicit ComponentCatalogue(const std::filesystem::path& application);
  ~ComponentCatalogue();
  const std::array<AcceptedComponent, 3>& entries() const { return entries_; }
  bool installed() const { return handle_ != nullptr; }
  ComponentCatalogue(const ComponentCatalogue&) = delete;
  ComponentCatalogue& operator=(const ComponentCatalogue&) = delete;
 private:
  void* handle_ = nullptr;
  std::array<AcceptedComponent, 3> entries_;
};
// A per-profile receipt of a successful official helper/script Add callback. It never
// accepts source files or enables extensions; current source pins and the
// runtime's enabled/path ID checks remain mandatory before reuse.
class ComponentRegistrations {
 public:
  ComponentRegistrations(std::filesystem::path application, std::filesystem::path data);
  bool Matches(size_t role, const std::string& tree) const;
  void Record(size_t role, const std::string& tree) const;
 private:
  std::filesystem::path Path(size_t role) const;
  std::string entropy_;
  std::filesystem::path data_;
};
}
