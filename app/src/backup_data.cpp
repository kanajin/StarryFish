#include "backup_data.h"
#include "core.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>

namespace gbrowser {
namespace {
constexpr std::array<uint8_t, 8> kMagic{'G', 'B', 'R', 'D', 'A', 'T', 'A', '1'};
void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
using Identity = std::pair<BackupSourceKind, std::string>;
Identity Id(const BackupSource& source) { return {source.kind, source.id}; }
void ValidateSource(const BackupSource& source) {
  const auto kind = static_cast<uint32_t>(source.kind);
  Require(kind >= 1 && kind <= 4 && source.schema != 0 && !source.id.empty() && source.id.size() <= 96 &&
      std::all_of(source.id.begin(), source.id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      }), "Invalid backup component identity or schema");
  Require(source.kind != BackupSourceKind::BrowserSettings || source.id == "gbrowser",
          "Invalid browser settings source");
  Require(source.kind != BackupSourceKind::GbfSession || source.id == "gbf", "Invalid GBF session source");
}
void ValidateSources(const std::vector<BackupSource>& sources, bool include_login) {
  Require(!sources.empty() && sources.size() <= 128, "Invalid backup record count");
  std::set<Identity> seen;
  bool settings = false, session = false;
  for (const auto& source : sources) {
    ValidateSource(source);
    Require(seen.insert(Id(source)).second, "Duplicate backup source");
    settings = settings || source.kind == BackupSourceKind::BrowserSettings;
    session = session || source.kind == BackupSourceKind::GbfSession;
  }
  Require(settings && session == include_login, "Backup session selection or required settings source is inconsistent");
}
std::vector<BackupSource> Sources(const BackupDocument& document) {
  Require(!document.records.empty() && document.records.size() <= 128, "Invalid backup record count");
  std::vector<BackupSource> sources;
  for (const auto& record : document.records) { ValidateSource(record.source); sources.push_back(record.source); }
  ValidateSources(sources, document.include_login);
  return sources;
}
struct Buffer {
  BackupBytes bytes;
  explicit Buffer(size_t size) : bytes(size) {}
  ~Buffer() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
};
void GrowSize(size_t& total, size_t count) {
  Require(count <= kBackupPayloadLimit - total, "Backup document exceeds 16 MiB"); total += count;
}
size_t FrameSize(const std::wstring& name, const std::vector<BackupSource>& sources) {
  ValidateProfileName(name);
  size_t total = 24; GrowSize(total, WideToUtf8(name).size());
  for (const auto& source : sources) { GrowSize(total, 16); GrowSize(total, source.id.size()); }
  return total;
}
void Put32(BackupBytes& bytes, size_t& position, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[position++] = static_cast<uint8_t>(value >> (8 * i));
}
struct Reader {
  const BackupBytes& bytes;
  size_t position = 0;
  size_t Take(size_t count) {
    Require(count <= bytes.size() - position, "Truncated backup document");
    const auto start = position; position += count; return start;
  }
  uint32_t Number() {
    const auto start = Take(4); uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i) result |= static_cast<uint32_t>(bytes[start + i]) << (8 * i);
    return result;
  }
  std::string Text(size_t count) {
    const auto start = Take(count);
    return std::string(reinterpret_cast<const char*>(bytes.data() + start), count);
  }
};
}  // namespace

BackupPlan MakeBackupPlan(const std::vector<BackupSource>& components,
                          const std::vector<BackupAdapter>& adapters,
                          bool include_login, bool inventory_complete) {
  Require(inventory_complete && components.size() <= 126 && adapters.size() <= 128,
          "Backup requires a complete bounded component inventory");
  BackupPlan plan{include_login, {{BackupSourceKind::BrowserSettings, "gbrowser", 1}}};
  if (include_login) plan.sources_.push_back({BackupSourceKind::GbfSession, "gbf", 1});
  for (const auto& source : components) {
    Require(source.kind == BackupSourceKind::Extension || source.kind == BackupSourceKind::Script,
            "Component inventory can contain only extension or script sources");
    plan.sources_.push_back(source);
  }
  ValidateSources(plan.sources_, include_login);
  std::set<Identity> seen;
  for (const auto& adapter : adapters) {
    ValidateSource(adapter.source);
    Require(seen.insert(Id(adapter.source)).second, "Duplicate backup adapter capability");
  }
  for (const auto& source : plan.sources_) {
    const auto found = std::find_if(adapters.begin(), adapters.end(), [&](const BackupAdapter& adapter) {
      return Id(adapter.source) == Id(source) && adapter.source.schema == source.schema;
    });
    Require(found != adapters.end() && (include_login
        ? found->export_with_login && found->restore_with_login
        : found->export_without_login && found->restore_without_login),
        "Backup component lacks verified capture and restore support for the selected login mode");
  }
  return plan;
}
BackupPlan RebindBackupPlan(const BackupPlan& trusted_plan, const std::vector<BackupAdapter>& actual_adapters) {
  if (trusted_plan.settings_only()) return MakeSettingsBackupPlan(actual_adapters);
  std::vector<BackupSource> inventory;
  for (const auto& source : trusted_plan.sources())
    if (source.kind == BackupSourceKind::Extension || source.kind == BackupSourceKind::Script)
      inventory.push_back(source);
  return MakeBackupPlan(inventory, actual_adapters, trusted_plan.include_login(), true);
}
BackupPlan MakeSettingsBackupPlan(const std::vector<BackupAdapter>& actual_adapters) {
  // Existing capability checks apply to the one selected source. The explicit
  // scope survives rebinding; true here concerns this selected settings scope.
  auto plan = MakeBackupPlan({}, actual_adapters, false, true);
  plan.settings_only_ = true;
  return plan;
}
size_t BackupDataBudget(const std::wstring& name, const BackupPlan& trusted_plan) {
  ValidateSources(trusted_plan.sources(), trusted_plan.include_login());
  return kBackupPayloadLimit - FrameSize(name, trusted_plan.sources());
}
void CheckBackupCoverage(const BackupDocument& document, const BackupPlan& trusted_plan) {
  ValidateProfileName(document.name);
  const auto actual = Sources(document);
  ValidateSources(trusted_plan.sources(), trusted_plan.include_login());
  Require(document.include_login == trusted_plan.include_login() && actual.size() == trusted_plan.sources().size(),
          "Backup source coverage or login choice differs from the trusted plan");
  for (const auto& source : actual)
    Require(std::any_of(trusted_plan.sources().begin(), trusted_plan.sources().end(), [&](const BackupSource& expected) {
      return Id(expected) == Id(source) && expected.schema == source.schema;
    }), "Unknown or incompatible backup component schema");
}
SensitiveBytes EncodeBackupDocument(const BackupDocument& document) {
  const auto sources = Sources(document);
  const auto name = WideToUtf8(document.name);
  size_t total = FrameSize(document.name, sources);
  for (const auto& record : document.records) GrowSize(total, record.data.bytes().size());
  Buffer encoded(total); auto& bytes = encoded.bytes;
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin()); size_t position = 8;
  Put32(bytes, position, 1); Put32(bytes, position, document.include_login ? 1 : 0);
  Put32(bytes, position, static_cast<uint32_t>(name.size()));
  Put32(bytes, position, static_cast<uint32_t>(document.records.size()));
  memcpy(bytes.data() + position, name.data(), name.size()); position += name.size();
  for (const auto& record : document.records) {
    Put32(bytes, position, static_cast<uint32_t>(record.source.kind)); Put32(bytes, position, record.source.schema);
    Put32(bytes, position, static_cast<uint32_t>(record.source.id.size()));
    Put32(bytes, position, static_cast<uint32_t>(record.data.bytes().size()));
    memcpy(bytes.data() + position, record.source.id.data(), record.source.id.size()); position += record.source.id.size();
    if (!record.data.bytes().empty()) memcpy(bytes.data() + position, record.data.bytes().data(), record.data.bytes().size());
    position += record.data.bytes().size();
  }
  return SensitiveBytes(std::move(encoded.bytes));
}
BackupDocument DecodeBackupDocument(const SensitiveBytes& encoded) {
  const auto& bytes = encoded.bytes();
  Require(bytes.size() >= 24 && bytes.size() <= kBackupPayloadLimit &&
      std::equal(kMagic.begin(), kMagic.end(), bytes.begin()), "Invalid backup document");
  Reader reader{bytes, 8};
  Require(reader.Number() == 1, "Unsupported backup document version");
  const auto flags = reader.Number(), name_length = reader.Number(), count = reader.Number();
  Require(flags <= 1 && name_length >= 1 && name_length <= 256 && count >= 1 && count <= 128,
          "Invalid backup document metadata");
  BackupDocument document; document.include_login = flags != 0;
  document.name = Utf8ToWide(reader.Text(name_length)); ValidateProfileName(document.name);
  struct Pending { BackupSource source; size_t start, length; };
  std::vector<Pending> pending; std::vector<BackupSource> sources;
  for (uint32_t i = 0; i < count; ++i) {
    const auto kind = reader.Number(), schema = reader.Number();
    const auto id_length = reader.Number(), data_length = reader.Number();
    Require(id_length >= 1 && id_length <= 96, "Invalid backup component ID length");
    BackupSource source{static_cast<BackupSourceKind>(kind), reader.Text(id_length), schema};
    ValidateSource(source);
    const auto start = reader.Take(data_length);
    sources.push_back(source); pending.push_back({std::move(source), start, data_length});
  }
  Require(reader.position == bytes.size(), "Trailing backup document data");
  ValidateSources(sources, document.include_login);
  // Only after full structural validation do we copy any sensitive payload.
  document.records.reserve(pending.size());
  for (auto& item : pending) {
    Buffer data(item.length);
    if (item.length) memcpy(data.bytes.data(), bytes.data() + item.start, item.length);
    document.records.push_back({std::move(item.source), SensitiveBytes(std::move(data.bytes))});
  }
  return document;
}
}  // namespace gbrowser
