#pragma once
#include "gbf_policy.h"
#include <string_view>

namespace gbrowser::webview {
// Classification only: a page can spell this name too. The runtime gesture,
// original main frame, current GBF source and live proxy gate are required.
inline bool IsBookmarkWindowName(std::wstring_view name) {
  constexpr std::wstring_view prefix = L"gbrowser-bookmark-middle-";
  if (name.size() != prefix.size() + 36 || name.substr(0, prefix.size()) != prefix) return false;
  const auto id = name.substr(prefix.size());
  for (size_t i = 0; i < id.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != L'-') return false; }
    else if (!((id[i] >= L'0' && id[i] <= L'9') || (id[i] >= L'a' && id[i] <= L'f'))) return false;
  }
  return true;
}
inline bool IsBookmarkNavigation(RunMode mode, const std::string& source, const std::string& target,
    bool gesture, bool main_frame) {
  return mode == RunMode::GbfOpenNetwork && gesture && main_frame && source.size() <= 8192 &&
      target.size() <= 8192 && IsGbfPageUrl(source) && IsBrowserNetworkUrl(target) && !IsGbfLoginUrl(target);
}
}
