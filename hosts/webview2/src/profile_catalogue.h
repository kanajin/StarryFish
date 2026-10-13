#pragma once
#include "user_state.h"
#include <vector>

namespace gbrowser::webview {
struct LocalProfile { std::string id; std::wstring name; bool retired = false; };
struct ProfileCatalogueState {
  std::string selected = "default";
  std::vector<LocalProfile> entries{{"default", L"默认资料", false}};
};
std::string EncodeProfiles(const ProfileCatalogueState& state);
ProfileCatalogueState DecodeProfiles(const std::string& bytes);
// Names are display metadata only. Paths derive solely from the validated ID
// and fixed launch mode; the legacy default directory is never moved/copied.
std::filesystem::path ProfileDataPath(const std::filesystem::path& installation,
    std::wstring_view mode_leaf, const std::string& id);
class ProfileCatalogue {
 public:
  ProfileCatalogue(const std::filesystem::path& installation, std::wstring_view mode_leaf);
  const ProfileCatalogueState& state() const { return state_; }
  const LocalProfile& Find(const std::string& id, bool allow_retired = false) const;
  std::string Create(const std::wstring& name);
  void Rename(const std::string& id, const std::wstring& name);
  void Retire(const std::string& id, const std::string& active);
  void Restore(const std::string& id);
  // Commit only after runtime UDF identity and all four components are ready.
  void Activate(const std::string& id);
 private:
  void Save(ProfileCatalogueState next);
  UserStateFile file_;
  ProfileCatalogueState state_;
  bool failed_ = false;
};
}
