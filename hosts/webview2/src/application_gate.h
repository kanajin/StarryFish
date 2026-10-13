#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace gbrowser::webview {
// Called under the installation lock and directory pins, before loading any
// component or creating a WebView environment. A dangling link is a gate too.
inline void RequireCompleteApplicationUpdate(const std::filesystem::path& root) {
  const auto marker = root / L"application-update.pending";
  const auto handle = CreateFileW(marker.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
    throw std::runtime_error("程序更新未确认完成；保留现有资料和更新证据，请检查安装记录");
  }
  if (GetLastError() != ERROR_FILE_NOT_FOUND)
    throw std::runtime_error("无法确认程序更新已完成；保留更新证据，不启动运行环境");
}
}
