#include "profile_catalogue.h"
#include "core.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace gbrowser::webview {
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("资料索引异常，现有资料保留"); }
bool Id(const std::string& id) { return id == "default" || IsProfileId(id); }
std::wstring Leaf(std::wstring_view leaf) {
  Require(leaf == L"UserData" || leaf == L"Validation" || leaf == L"GbfValidation");
  return std::wstring(leaf);
}
std::string Hex(const std::string& bytes) {
  constexpr char hex[] = "0123456789abcdef"; std::string output;
  for (unsigned char byte : bytes) { output += hex[byte >> 4]; output += hex[byte & 15]; }
  return output;
}
std::string Unhex(const std::string& text) {
  Require(!text.empty() && text.size() <= 512 && text.size() % 2 == 0);
  std::string result;
  const auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < text.size(); i += 2) {
    const auto a = digit(text[i]), b = digit(text[i + 1]); Require(a >= 0 && b >= 0);
    result += static_cast<char>(a * 16 + b);
  }
  return result;
}
void Validate(const ProfileCatalogueState& state) {
  Require(!state.entries.empty() && state.entries.size() <= 64 && Id(state.selected));
  bool selected = false, legacy = false;
  for (size_t i = 0; i < state.entries.size(); ++i) {
    const auto& entry = state.entries[i]; Require(Id(entry.id)); ValidateProfileName(entry.name);
    for (size_t j = 0; j < i; ++j) Require(state.entries[j].id != entry.id && state.entries[j].name != entry.name);
    legacy |= entry.id == "default";
    selected |= entry.id == state.selected && !entry.retired;
  }
  Require(selected && legacy);
}
}
std::string EncodeProfiles(const ProfileCatalogueState& state) {
  Validate(state);
  std::string output = "GBWV-PROFILES-1\n" + state.selected + "\n";
  for (const auto& entry : state.entries)
    output += entry.id + " " + (entry.retired ? "1 " : "0 ") + Hex(WideToUtf8(entry.name)) + "\n";
  return output;
}
ProfileCatalogueState DecodeProfiles(const std::string& bytes) {
  std::istringstream input(bytes); std::string header, selected; Require(static_cast<bool>(std::getline(input, header)));
  Require(header == "GBWV-PROFILES-1" && static_cast<bool>(std::getline(input, selected)));
  ProfileCatalogueState result{selected, {}};
  for (std::string line; std::getline(input, line);) {
    Require(result.entries.size() < 64);
    std::istringstream row(line); std::string id, name; int retired = -1;
    Require(static_cast<bool>(row >> id >> retired >> name) && (retired == 0 || retired == 1));
    result.entries.push_back({id, Utf8ToWide(Unhex(name)), retired == 1});
  }
  Require(EncodeProfiles(result) == bytes); return result;
}
std::filesystem::path ProfileDataPath(const std::filesystem::path& installation,
    std::wstring_view mode_leaf, const std::string& id) {
  Require(Id(id)); const auto leaf = Leaf(mode_leaf);
  return id == "default" ? installation / leaf : installation / (L"Profiles-" + leaf) / Utf8ToWide(id);
}
ProfileCatalogue::ProfileCatalogue(const std::filesystem::path& installation, std::wstring_view leaf)
    : file_(installation / (L"profiles-" + Leaf(leaf) + L".state")) {
  if (file_.bytes()) state_ = DecodeProfiles(*file_.bytes());
}
const LocalProfile& ProfileCatalogue::Find(const std::string& id, bool allow_retired) const {
  const auto found = std::find_if(state_.entries.begin(), state_.entries.end(), [&](const auto& entry) { return entry.id == id; });
  Require(found != state_.entries.end() && (allow_retired || !found->retired)); return *found;
}
void ProfileCatalogue::Save(ProfileCatalogueState next) {
  const auto bytes = EncodeProfiles(next); // Ordinary invalid input is recoverable before I/O.
  Require(!failed_);
  try { file_.Save(bytes); state_ = std::move(next); }
  catch (...) { failed_ = true; throw; } // An unknown publication cannot be replayed by another UI click.
}
std::string ProfileCatalogue::Create(const std::wstring& name) {
  std::array<unsigned char, 16> bytes{};
  Require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
  const auto id = "p-" + Hex(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  auto next = state_; next.entries.push_back({id, name, false}); Save(std::move(next)); return id;
}
void ProfileCatalogue::Rename(const std::string& id, const std::wstring& name) {
  Find(id, true); auto next = state_;
  for (auto& entry : next.entries) if (entry.id == id) entry.name = name;
  Save(std::move(next));
}
void ProfileCatalogue::Retire(const std::string& id, const std::string& active) {
  Find(id); Find(active); Require(id != active); auto next = state_;
  for (auto& entry : next.entries) if (entry.id == id) entry.retired = true;
  if (next.selected == id) next.selected = active;
  // Logical recycling preserves the original UDF and its login/extension data.
  Save(std::move(next));
}
void ProfileCatalogue::Restore(const std::string& id) {
  Require(Find(id, true).retired); auto next = state_;
  for (auto& entry : next.entries) if (entry.id == id) entry.retired = false;
  Save(std::move(next));
}
void ProfileCatalogue::Activate(const std::string& id) { Find(id); auto next = state_; next.selected = id; Save(std::move(next)); }
}
