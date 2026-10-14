#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace gbrowser::webview {
// Only a known game entry route can be refreshed after closing its login popup.
// Never retain or replay an OAuth callback, a battle URL or an arbitrary query.
inline bool LoginResumeEntry(std::wstring_view url) {
  for (const auto origin : {L"https://game.granbluefantasy.jp/", L"https://gbf.game.mbga.jp/"}) {
    const std::wstring_view prefix(origin);
    if (url.substr(0, prefix.size()) != prefix) continue;
    const auto route = url.substr(prefix.size());
    return route.empty() || route == L"#" || route == L"#mypage" ||
        route == L"#title" || route == L"#login" || route == L"#top";
  }
  return false;
}
struct LoginResume {
  uint64_t generation = 0, navigation = 0, attempt = 0;
  std::wstring source;
  bool Matches(uint64_t current_generation, uint64_t current_navigation,
      uint64_t current_attempt, std::wstring_view current_source) const {
    return attempt && generation == current_generation && navigation == current_navigation &&
        attempt == current_attempt && LoginResumeEntry(source) && source == current_source;
  }
};
// Return only a boolean about the fixed public channel-selection instruction.
// No account fields, storage, authorization messages or callback parameters are read.
inline constexpr wchar_t kLoginChannelVisible[] = LR"JS((() => {
  if (window !== top || !['https://game.granbluefantasy.jp', 'https://gbf.game.mbga.jp'].includes(location.origin)) return false;
  const nodes = document.querySelectorAll('div,p,span');
  const expected = 'ログインするアカウントを選択してください。';
  for (let i = 0; i < Math.min(nodes.length, 2048); ++i) {
    const node = nodes[i];
    if (node.children.length || node.textContent.trim() !== expected) continue;
    const rect = node.getBoundingClientRect(), style = getComputedStyle(node);
    if (typeof node.checkVisibility === 'function' && !node.checkVisibility({checkOpacity: true, checkVisibilityCSS: true})) continue;
    if (rect.width > 0 && rect.height > 0 && style.visibility !== 'hidden' && style.display !== 'none') return true;
  }
  return false;
})())JS";
}
