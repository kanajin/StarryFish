#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gbrowser::webview {
struct TargetReply { int id, reason, sample; };
// ExecuteScript returns a JSON string with four numeric fields. Parse the same
// bounded contract used by the extension bridge; never accept escaped payloads
// or narrow a valid signed 32-bit tab ID to a nine-digit application budget.
inline TargetReply ParseTargetReply(std::string_view encoded, uint64_t generation) {
  const auto prefix = '"' + std::to_string(generation) + ':';
  auto fail = [] { throw std::runtime_error("游戏目标回复异常"); };
  if (encoded.size() > 72 || encoded.size() <= prefix.size() ||
      encoded.compare(0, prefix.size(), prefix) != 0 || encoded.back() != '"') fail();
  auto fields = encoded.substr(prefix.size(), encoded.size() - prefix.size() - 1);
  std::array<int, 3> values{};
  for (size_t i = 0; i < values.size(); ++i) {
    const auto end = fields.find(':');
    if ((i + 1 < values.size()) != (end != std::string_view::npos)) fail();
    const auto number = fields.substr(0, end);
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), values[i]);
    if (number.empty() || parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) fail();
    if (end != std::string_view::npos) fields.remove_prefix(end + 1);
  }
  const TargetReply result{values[0], values[1], values[2]};
  if (result.reason < 0 || result.reason > 4 || result.id < 0 ||
      (result.reason == 0 ? result.id <= 0 || result.sample != result.id : result.id != 0) ||
      (result.reason != 0 && result.reason != 4 && result.sample != 0)) fail();
  return result;
}
}
