#pragma once
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gbrowser::webview::capture {
using winrt::Windows::Data::Json::IJsonValue;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;
using winrt::Windows::Data::Json::JsonValueType;
inline std::string Encode(const IJsonValue& value) { return winrt::to_string(value.Stringify()); }
inline JsonObject Object(const std::string& value) { return JsonObject::Parse(winrt::to_hstring(value)); }
inline std::string Text(const JsonObject& value, const wchar_t* name, const std::string& fallback = {}) {
  return winrt::to_string(value.GetNamedString(name, winrt::to_hstring(fallback)));
}
inline void Put(JsonObject& value, const wchar_t* name, const std::string& text) {
  value.SetNamedValue(name, JsonValue::CreateStringValue(winrt::to_hstring(text)));
}
inline void Number(JsonObject& value, const wchar_t* name, double number) {
  value.SetNamedValue(name, JsonValue::CreateNumberValue(number));
}
inline void Flag(JsonObject& value, const wchar_t* name, bool flag = true) {
  value.SetNamedValue(name, JsonValue::CreateBooleanValue(flag));
}
inline std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}
inline bool Sensitive(const std::string& name) {
  auto key = Lower(name);
  key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) { return !std::isalnum(ch); }), key.end());
  for (const auto* part : {"token", "session", "password", "passwd", "secret", "credential", "signature", "cookie", "authorization", "apikey", "csrf", "xsrf"})
    if (key.find(part) != std::string::npos) return true;
  return key == "auth" || key == "sid" || key == "sig";
}
inline std::string DecodeName(const std::string& value) {
  std::string decoded;
  auto hex = [](char ch) -> int { if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10; if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10; return -1; };
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size() && hex(value[index + 1]) >= 0 && hex(value[index + 2]) >= 0) {
      decoded += static_cast<char>(hex(value[index + 1]) * 16 + hex(value[index + 2])); index += 2;
    } else decoded += value[index] == '+' ? ' ' : value[index];
  }
  return decoded;
}
inline std::string RedactPairs(const std::string& input, bool& redacted) {
  std::string output;
  size_t begin = 0;
  while (begin <= input.size()) {
    const auto end = input.find_first_of("&;", begin);
    const auto pair = input.substr(begin, end == std::string::npos ? end : end - begin);
    const auto equals = pair.find('=');
    const auto key = pair.substr(0, equals);
    if (Sensitive(DecodeName(DecodeName(key)))) { output += key + "=[REDACTED]"; redacted = true; }
    else output += pair;
    if (end == std::string::npos) break;
    output += input[end]; begin = end + 1;
  }
  return output;
}
inline std::string RedactUrl(std::string url, bool& redacted) {
  if (Lower(url.substr(0, 5)) == "data:") { redacted = true; return "data:[REDACTED]"; }
  const auto authority = url.find("://");
  if (authority != std::string::npos) {
    const auto end = url.find_first_of("/?#", authority + 3);
    const auto at = url.find('@', authority + 3);
    if (at != std::string::npos && (end == std::string::npos || at < end)) {
      url.replace(authority + 3, at - authority - 3, "[REDACTED]"); redacted = true;
    }
  }
  const auto fragment = url.find('#');
  if (fragment != std::string::npos) {
    const auto suffix = RedactPairs(url.substr(fragment + 1), redacted);
    url = url.substr(0, fragment + 1) + suffix;
  }
  const auto query = url.find('?');
  if (query != std::string::npos) {
    const auto end = url.find('#', query);
    url = url.substr(0, query + 1) + RedactPairs(url.substr(query + 1, end == std::string::npos ? end : end - query - 1), redacted) +
        (end == std::string::npos ? "" : url.substr(end));
  }
  return url;
}
inline IJsonValue RedactValue(const IJsonValue& value, bool& redacted, unsigned depth = 0) {
  if (depth > 64) { redacted = true; return JsonValue::CreateStringValue(L"[REDACTED: depth limit]"); }
  if (value.ValueType() == JsonValueType::Object) {
    JsonObject output;
    for (const auto& pair : value.GetObject()) {
      const auto key = winrt::to_string(pair.Key());
      if (Sensitive(key)) { output.SetNamedValue(pair.Key(), JsonValue::CreateStringValue(L"[REDACTED]")); redacted = true; }
      else output.SetNamedValue(pair.Key(), RedactValue(pair.Value(), redacted, depth + 1));
    }
    return output;
  }
  if (value.ValueType() == JsonValueType::Array) {
    JsonArray output;
    for (const auto& item : value.GetArray()) output.Append(RedactValue(item, redacted, depth + 1));
    return output;
  }
  if (value.ValueType() == JsonValueType::String) {
    const auto original = winrt::to_string(value.GetString());
    if (original.find("://") != std::string::npos || Lower(original.substr(0, 5)) == "data:")
      return JsonValue::CreateStringValue(winrt::to_hstring(RedactUrl(original, redacted)));
  }
  return value;
}
inline JsonObject Body(const std::string& bytes, const std::string& mime, bool base64 = false) {
  JsonObject result;
  Number(result, L"original_bytes", static_cast<double>(bytes.size()));
  Put(result, L"content_type", mime);
  Put(result, L"encoding", "utf-8");
  bool redacted = false;
  try {
    if (base64) throw std::runtime_error("opaque");
    if (bytes.empty()) { Put(result, L"text", ""); return result; }
    if (bytes.size() > 1024 * 1024) { Flag(result, L"truncated"); throw std::runtime_error("oversize"); }
    const auto type = Lower(mime);
    if (type.find("x-www-form-urlencoded") != std::string::npos) Put(result, L"text", RedactPairs(bytes, redacted));
    else {
      const auto parsed = JsonValue::Parse(winrt::to_hstring(bytes));
      if (parsed.ValueType() != JsonValueType::Object && parsed.ValueType() != JsonValueType::Array) throw std::runtime_error("opaque");
      result.SetNamedValue(L"json", RedactValue(parsed, redacted));
    }
    Flag(result, L"redacted", redacted);
  } catch (...) {
    Flag(result, L"body_unavailable"); Flag(result, L"redacted");
    Put(result, L"reason", base64 ? "opaque_binary" : bytes.size() > 1024 * 1024 ? "body_limit" : "unstructured_body");
    if (base64) {
      const size_t padding = !bytes.empty() && bytes.back() == '=' ? (bytes.size() > 1 && bytes[bytes.size()-2] == '=' ? 2 : 1) : 0;
      const auto decoded_size = bytes.size() / 4 * 3;
      Number(result, L"original_bytes", static_cast<double>(decoded_size >= padding ? decoded_size - padding : 0));
      Put(result, L"encoding", "base64_withheld");
    }
  }
  return result;
}
inline std::string NormalizeCooldownKey(std::string key) {
  key = Lower(std::move(key));
  key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) { return !std::isalnum(ch); }), key.end());
  return key;
}
inline bool CooldownContextKey(const std::string& key) {
  return key.find("skill") != std::string::npos || key.find("ability") != std::string::npos || key == "abilities";
}
inline std::string CooldownField(const std::string& normalized_key, bool skill_context) {
  if (normalized_key == "recast" || normalized_key == "skillrecast" || normalized_key == "abilityrecast" ||
      normalized_key == "recasttime" || normalized_key == "recastturn" || normalized_key == "recastturns" ||
      normalized_key == "skillrecasttime" || normalized_key == "abilityrecasttime") return "recast";
  if (normalized_key == "cooldown" || normalized_key == "skillcooldown" || normalized_key == "abilitycooldown" ||
      normalized_key == "cooldownturn" || normalized_key == "cooldownturns" || normalized_key == "skillcooldownturns" ||
      normalized_key == "abilitycooldownturns") return "cooldown";
  if (normalized_key == "skillcd") return "skill_cd";
  if (normalized_key == "abilitycd") return "ability_cd";
  if (skill_context && normalized_key == "cd") return "cd";
  if (skill_context && (normalized_key == "remaincd" || normalized_key == "remainingcd" ||
      normalized_key == "remainingcooldown" || normalized_key == "remaincooldown" ||
      normalized_key == "remainingturns" || normalized_key == "turnsremaining")) return "remaining_cd";
  return {};
}
inline bool CooldownInteger(const IJsonValue& value, int& result) {
  int parsed = 0;
  if (value.ValueType() == JsonValueType::Number) {
    const auto number = value.GetNumber();
    if (!std::isfinite(number) || std::floor(number) != number || number < 0 || number > 99) return false;
    parsed = static_cast<int>(number);
  } else if (value.ValueType() == JsonValueType::String) {
    const auto text = winrt::to_string(value.GetString());
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (text.empty() || conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() || parsed < 0 || parsed > 99) return false;
  } else return false;
  result = parsed;
  return true;
}
inline bool ParseCooldownInteger(const std::string& text, int& result) {
  if (text.empty()) return false;
  int parsed = 0;
  const auto conversion = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() || parsed < 0 || parsed > 99) return false;
  result = parsed;
  return true;
}
inline bool ParseGbfFrameJson(const std::string& bytes, IJsonValue& parsed, std::string& format) {
  auto parse = [&](const std::string& candidate) {
    try {
      auto value = JsonValue::Parse(winrt::to_hstring(candidate));
      if (value.ValueType() != JsonValueType::Object && value.ValueType() != JsonValueType::Array) return false;
      parsed = value;
      return true;
    } catch (...) { return false; }
  };
  if (parse(bytes)) { format = "json"; return true; }
  size_t start = 0;
  while (start < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[start]))) ++start;
  const auto json_start = bytes.find_first_of("{[", start);
  if (json_start == std::string::npos || json_start == start || json_start - start > 3 || bytes[json_start] != '[') return false;
  for (size_t index = start; index < json_start; ++index) {
    const auto ch = static_cast<unsigned char>(bytes[index]);
    if (!std::isdigit(ch)) return false;
  }
  if (!parse(bytes.substr(json_start))) return false;
  format = "prefixed_json";
  return true;
}
inline void AddCooldownField(JsonArray& fields, const std::string& field,
    const std::vector<unsigned>& indices, const std::vector<unsigned>& location, int value, bool& limited) {
  if (fields.Size() >= 64) { limited = true; return; }
  JsonObject entry; Put(entry, L"field", field);
  JsonArray positions; for (const auto index : indices) positions.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
  JsonArray steps; for (const auto index : location) steps.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
  entry.SetNamedValue(L"location", steps);
  entry.SetNamedValue(L"indices", positions); Number(entry, L"value", static_cast<double>(value)); fields.Append(entry);
}
inline void WalkCooldownFields(const IJsonValue& value, const std::string& key, bool skill_context,
    const std::string& inherited_field, std::vector<unsigned>& indices, std::vector<unsigned>& location,
    JsonArray& fields, unsigned depth, size_t& visited, bool& limited) {
  if (depth > 32 || visited >= 4096 || fields.Size() >= 64) { limited = true; return; }
  ++visited;
  if (value.ValueType() == JsonValueType::Object) {
    unsigned ordinal = 0;
    for (const auto& property : value.GetObject()) {
      if (visited >= 4096 || fields.Size() >= 64) { limited = true; return; }
      const auto position = ordinal++;
      const auto decoded_name = DecodeName(DecodeName(winrt::to_string(property.Key())));
      if (Sensitive(decoded_name)) { ++visited; continue; }
      const auto name = NormalizeCooldownKey(decoded_name);
      const bool context = skill_context || CooldownContextKey(name);
      auto field = CooldownField(name, context);
      if (field.empty()) field = inherited_field;
      bool add_index = false;
      unsigned object_index = 0;
      if (!inherited_field.empty()) {
        const auto conversion = std::from_chars(name.data(), name.data() + name.size(), object_index);
        add_index = !name.empty() && conversion.ec == std::errc{} && conversion.ptr == name.data() + name.size() && object_index < 1000;
        if (!add_index && name != "value" && name != "turn" && name != "cd" && name != "recast" && name != "cooldown" && name != "remaining")
          field.clear();
      }
      const bool pushed_index = add_index && indices.size() < 32;
      if (pushed_index) indices.push_back(object_index);
      location.push_back(position);
      WalkCooldownFields(property.Value(), name, context, field, indices, location, fields, depth + 1, visited, limited);
      location.pop_back();
      if (pushed_index) indices.pop_back();
    }
    return;
  }
  if (value.ValueType() == JsonValueType::Array) {
    const auto array = value.GetArray();
    for (uint32_t index = 0; index < array.Size(); ++index) {
      if (visited >= 4096 || fields.Size() >= 64) { limited = true; return; }
      const bool pushed_index = indices.size() < 32;
      if (pushed_index) indices.push_back(index);
      location.push_back(index);
      WalkCooldownFields(array.GetAt(index), key, skill_context, inherited_field, indices, location, fields, depth + 1, visited, limited);
      location.pop_back();
      if (pushed_index) indices.pop_back();
    }
    return;
  }
  const auto field = inherited_field.empty() ? CooldownField(NormalizeCooldownKey(key), skill_context) : inherited_field;
  int number = 0;
  if (!field.empty() && CooldownInteger(value, number)) AddCooldownField(fields, field, indices, location, number, limited);
}
inline JsonObject GbfCooldownExtraction(const std::string& bytes, bool text_frame = true) {
  JsonObject result; Put(result, L"parser", "gbf_skill_cd_v1"); Put(result, L"scope", "cd_fields_only");
  Put(result, L"semantics", "unverified_candidates");
  JsonArray fields; bool limited = false; std::string format = "unknown";
  if (!text_frame) Put(result, L"state", "binary_skipped");
  else if (bytes.size() > 1024 * 1024) Put(result, L"state", "body_limit");
  else {
    IJsonValue parsed{nullptr};
    if (ParseGbfFrameJson(bytes, parsed, format)) {
      std::vector<unsigned> indices, location; size_t visited = 0;
      WalkCooldownFields(parsed, "", false, "", indices, location, fields, 0, visited, limited);
    } else if (bytes.find('=') != std::string::npos) {
      format = "form";
      size_t begin = 0; unsigned ordinal = 0;
      while (begin <= bytes.size()) {
        if (ordinal >= 4096 || fields.Size() >= 64) { limited = true; break; }
        const auto end = bytes.find_first_of("&;", begin);
        const auto pair = bytes.substr(begin, end == std::string::npos ? end : end - begin);
        const auto equals = pair.find('=');
        if (equals != std::string::npos) {
          const auto decoded_key = DecodeName(DecodeName(pair.substr(0, equals)));
          const auto key = NormalizeCooldownKey(decoded_key);
          const auto field = Sensitive(decoded_key) ? "" : CooldownField(key, CooldownContextKey(key));
          int number = 0;
          if (!field.empty() && ParseCooldownInteger(DecodeName(pair.substr(equals + 1)), number))
            AddCooldownField(fields, field, {}, {ordinal}, number, limited);
        }
        ++ordinal;
        if (end == std::string::npos) break;
        begin = end + 1;
      }
    }
    if (fields.Size()) Put(result, L"state", "matched");
    else if (format == "json" || format == "prefixed_json" || format == "form") Put(result, L"state", "no_cd_fields");
    else Put(result, L"state", "unrecognized_frame");
  }
  Put(result, L"format", format); Number(result, L"field_count", fields.Size());
  if (limited) Flag(result, L"truncated");
  result.SetNamedValue(L"fields", fields);
  return result;
}
inline JsonObject Headers(const JsonObject& headers, bool& redacted) {
  JsonObject output;
  for (const auto& item : headers) {
    const auto key = winrt::to_string(item.Key());
    if (Sensitive(key)) { output.SetNamedValue(item.Key(), JsonValue::CreateStringValue(L"[REDACTED]")); redacted = true; }
    else if (item.Value().ValueType() == JsonValueType::String) {
      auto text = winrt::to_string(item.Value().GetString());
      if (Lower(key) == "location" || Lower(key) == "referer" || Lower(key) == "origin") text = RedactUrl(text, redacted);
      output.SetNamedValue(item.Key(), JsonValue::CreateStringValue(winrt::to_hstring(text)));
    }
  }
  return output;
}
}
