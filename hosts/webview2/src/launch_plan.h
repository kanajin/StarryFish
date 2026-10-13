#pragma once
#include "core.h"
#include "application_layout.h"
#include <optional>
#include <string_view>

namespace gbrowser::webview {
// Product startup is separate from explicit diagnostics. Never reinterpret
// CEF's fixed-site mode or import its validation profile into this host.
struct LaunchPlan {
  RunMode network_mode;
  const wchar_t* data_leaf;
};
inline std::optional<LaunchPlan> NativeLaunchPlan(std::wstring_view command) {
  if (IsDefaultMainCommandLine(command)) return LaunchPlan{RunMode::GbfOpenNetwork, L"UserData"};
  const auto mode = MainRunMode(command);
  if (mode == RunMode::Offline) return LaunchPlan{*mode, L"Validation"};
  if (mode == RunMode::GbfOpenNetwork) return LaunchPlan{*mode, L"GbfValidation"};
  return std::nullopt;
}
// Baseline exercised by the extension-host spike, not a future-version
// compatibility promise. Required COM interfaces are checked at runtime too.
inline bool SupportedRuntimeVersion(std::wstring_view version) {
  unsigned major = 0;
  size_t cursor = 0;
  for (unsigned field = 0; field < 4; ++field) {
    const auto start = cursor;
    unsigned value = 0;
    while (cursor < version.size() && version[cursor] >= L'0' && version[cursor] <= L'9') {
      if (value > 214748364U) return false;
      value = value * 10 + static_cast<unsigned>(version[cursor++] - L'0');
      if (value > 2147483647U) return false;
    }
    if (cursor == start) return false;
    if (field == 0) major = value;
    if (field < 3 && (cursor == version.size() || version[cursor++] != L'.')) return false;
  }
  return cursor == version.size() && major >= 154;
}
}
