#pragma once
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include "gbf_policy.h"

namespace gbrowser::webview::cooldown {
constexpr int kUnknown = -2, kMissing = -1;
struct Snapshot {
  std::array<std::array<int, 4>, 4> cells{};
  Snapshot() { for (auto& row : cells) row.fill(kUnknown); }
};
inline std::optional<int> Integer(const winrt::Windows::Data::Json::IJsonValue& value) {
  using namespace winrt::Windows::Data::Json;
  if (value.ValueType() == JsonValueType::Number) {
    const double number = value.GetNumber();
    if (std::isfinite(number) && number >= 0 && number <= 999999 && std::floor(number) == number)
      return static_cast<int>(number);
  } else if (value.ValueType() == JsonValueType::String) {
    const auto text = winrt::to_string(value.GetString());
    if (!text.empty() && text.size() <= 6 && text.find_first_not_of("0123456789") == std::string::npos)
      return std::stoi(text);
  }
  return std::nullopt;
}
inline bool BattleResponse(const std::string& url) {
  if (!IsGbfPageUrl(url) || IsGbfLoginUrl(url)) return false;
  const auto origin = url.find('/', url.find("://") + 3);
  if (origin == std::string::npos) return false;
  const auto path = url.substr(origin, url.find_first_of("?#", origin) - origin);
  return (path.rfind("/rest/raid/", 0) == 0 || path.rfind("/rest/multiraid/", 0) == 0) &&
      path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0;
}
inline std::string BattleRoute(const std::string& url) {
  if (!IsGbfPageUrl(url) || IsGbfLoginUrl(url)) return {};
  const auto hash = url.find('#');
  if (hash == std::string::npos) return {};
  const auto route = url.substr(hash + 1, 256);
  return route.rfind("raid/", 0) == 0 || route.rfind("raid_multi/", 0) == 0 ? route : std::string{};
}
inline std::optional<Snapshot> Parse(const std::string& body) {
  using namespace winrt::Windows::Data::Json;
  if (body.size() > 1024 * 1024) return std::nullopt;
  JsonObject root;
  if (!JsonObject::TryParse(winrt::to_hstring(body), root)) return std::nullopt;
  auto container = root;
  if (root.HasKey(L"status") && root.GetNamedValue(L"status").ValueType() == JsonValueType::Object) {
    const auto status = root.GetNamedObject(L"status");
    if (status.HasKey(L"ability")) container = status;
  }
  if (!container.HasKey(L"ability")) return std::nullopt;
  Snapshot result;
  if (container.GetNamedValue(L"ability").ValueType() != JsonValueType::Object) return result;
  std::array<bool, 4> occupied{};
  for (const auto& entry : container.GetNamedObject(L"ability")) {
    if (entry.Value().ValueType() != JsonValueType::Object) continue;
    const auto character = entry.Value().GetObject();
    if (!character.HasKey(L"pos")) continue;
    const auto position = Integer(character.GetNamedValue(L"pos"));
    if (!position || *position >= 4) continue;
    auto& row = result.cells[static_cast<size_t>(*position)];
    if (occupied[static_cast<size_t>(*position)]) { row.fill(kUnknown); continue; }
    occupied[static_cast<size_t>(*position)] = true;
    if (!character.HasKey(L"list") || character.GetNamedValue(L"list").ValueType() != JsonValueType::Object) continue;
    const auto list = character.GetNamedObject(L"list");
    if (!list.Size()) continue;
    for (size_t skill = 0; skill < row.size(); ++skill) {
      const auto key = winrt::to_hstring(std::to_string(skill + 1));
      if (!list.HasKey(key)) { row[skill] = kMissing; continue; }
      const auto value = list.GetNamedValue(key);
      if (value.ValueType() != JsonValueType::Array || !value.GetArray().Size() ||
          value.GetArray().GetAt(0).ValueType() != JsonValueType::Object) continue;
      const auto metadata = value.GetArray().GetObjectAt(0);
      if (metadata.HasKey(L"ability-character-num")) {
        const auto owner = Integer(metadata.GetNamedValue(L"ability-character-num"));
        if (!owner || *owner != *position) continue;
      }
      if (metadata.HasKey(L"ability-recast")) {
        const auto remaining = Integer(metadata.GetNamedValue(L"ability-recast"));
        if (remaining) row[skill] = *remaining;
      }
    }
  }
  return result;
}
class State {
 public:
  void Reset() { ++epoch_; sequence_ = 0; snapshot_ = Snapshot{}; }
  uint64_t epoch() const { return epoch_; }
  const Snapshot& snapshot() const { return snapshot_; }
  bool Apply(uint64_t epoch, uint64_t sequence, const Snapshot& snapshot) {
    if (epoch != epoch_ || sequence < sequence_) return false;
    sequence_ = sequence; snapshot_ = snapshot; return true;
  }
 private:
  uint64_t epoch_ = 0, sequence_ = 0;
  Snapshot snapshot_;
};
}
