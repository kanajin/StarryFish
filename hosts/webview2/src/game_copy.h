#pragma once
#include <string>

namespace gbrowser::webview {
// Copy only an actual game route; never copy login callbacks or an address draft.
inline bool CopyableGameUrl(const std::wstring& url) {
  return url.size() <= 8192 && url.find_first_of(L"\r\n\t") == std::wstring::npos &&
      (url.rfind(L"https://game.granbluefantasy.jp/#", 0) == 0 ||
       url.rfind(L"https://gbf.game.mbga.jp/#", 0) == 0);
}
enum class GameCopy { None, Navigate, Reload };
inline GameCopy PlanGameCopy(const std::wstring& primary, const std::wstring& secondary) {
  if (!CopyableGameUrl(primary)) return GameCopy::None;
  return primary == secondary ? GameCopy::Reload : GameCopy::Navigate;
}
}
