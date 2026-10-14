#pragma once
#include "game_copy.h"

namespace gbrowser::webview {
// Chromium's browser UI may skip SPA entries after a script redirects during
// back/forward. The History API traverses the actual session entries instead.
// Use it only for explicit navigation in the trusted GBF route document.
inline const wchar_t* GameHistoryScript(const std::wstring& source, bool forward) {
  if (!CopyableGameUrl(source)) return nullptr;
  // ExecuteScript is asynchronous: recheck the document when the script runs.
  // No route is remembered/recreated, so GBF still owns its redirects/history.
  return forward ?
      LR"((()=>{if(window===window.top&&location.pathname==='/'&&location.hash&&['https://game.granbluefantasy.jp','https://gbf.game.mbga.jp'].includes(location.origin))history.forward();})();)" :
      LR"((()=>{if(window===window.top&&location.pathname==='/'&&location.hash&&['https://game.granbluefantasy.jp','https://gbf.game.mbga.jp'].includes(location.origin))history.back();})();)";
}
}
