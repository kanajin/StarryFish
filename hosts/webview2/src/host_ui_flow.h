#pragma once
#include <windows.h>
#include "proxy_initialization.h"

namespace gbrowser::webview {
enum class ProxyUiResult { Waiting, Matched, RestoreSaved, Failed };
inline ProxyUiResult ProxyFeedback(const ProxyInitialization& operation, bool probe, bool saved) {
  if (operation.pending()) return ProxyUiResult::Waiting;
  if (operation.state() == ProxyInitializationState::ValuesMatched) return ProxyUiResult::Matched;
  // A read-only mismatch with one authorized saved choice is an intermediate
  // startup step, not an error. Unknown/write failures never get this route.
  if (probe && saved && operation.state() == ProxyInitializationState::PreflightDifferent)
    return ProxyUiResult::RestoreSaved;
  return ProxyUiResult::Failed;
}
template<typename Move>
HRESULT FocusEnabledHost(HWND window, bool closing, Move move) {
  // A modal dialog disables its owner while async WebView replies can still
  // arrive. Do not move focus into that owner or treat UI focus as proxy state.
  if (!window || closing || !IsWindowEnabled(window)) return S_FALSE;
  return move();
}
}
