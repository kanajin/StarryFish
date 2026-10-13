#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gbrowser::webview {
// Optional, bounded public phase timings. No page URLs, request bodies,
// extension storage or credentials; failure cannot change game/permission flow.
class TimingLog {
 public:
  TimingLog() = default;
  void Open(const std::filesystem::path& data, ULONGLONG start) noexcept {
    if (file_ != INVALID_HANDLE_VALUE) return;
    try {
      // Keep old evidence. At the lifetime budget, stop creating logs rather
      // than deleting files or growing storage on every application restart.
      if (!HasBudget(data)) return;
      start_ = start;
      const auto name = L"native-timing-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(start) + L".log";
      file_ = CreateFileW((data / name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    } catch (...) { /* Optional diagnostics must not prevent startup. */ }
  }
  ~TimingLog() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
  TimingLog(const TimingLog&) = delete;
  TimingLog& operator=(const TimingLog&) = delete;
  void Mark(const char* phase, ULONGLONG reference = 0) noexcept {
    if (file_ == INVALID_HANDLE_VALUE || events_ >= 128) return;
    try {
      const auto now = GetTickCount64();
      const auto since = reference ? reference : start_;
      const std::string line = std::string(phase) + " elapsed_ms=" + std::to_string(now >= since ? now - since : 0) + "\n";
      DWORD written = 0;
      if (line.size() > 256 || !WriteFile(file_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size()) {
        CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; return;
      }
      ++events_;
    } catch (...) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
  }
  void Value(const char* phase, int value) noexcept {
    // Numeric runtime error/status codes only. Never accepts webpage values,
    // URLs, response bodies, headers or credentials.
    try { Mark((std::string(phase) + " value=" + std::to_string(value)).c_str()); }
    catch (...) { /* Optional public diagnostics do not change navigation. */ }
  }
 private:
  static bool HasBudget(const std::filesystem::path& data) {
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW((data / L"native-timing-*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    struct Search { HANDLE handle; ~Search() { FindClose(handle); } } held{search};
    size_t count = 0;
    uint64_t bytes = 0;
    do {
      if (++count >= 32 || (entry.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
      const auto size = (static_cast<uint64_t>(entry.nFileSizeHigh) << 32) | entry.nFileSizeLow;
      if (size > 1024 * 1024) return false;
      bytes += size;
      if (bytes > 1024 * 1024 - 128 * 256) return false;
    } while (FindNextFileW(search, &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
  }
  HANDLE file_ = INVALID_HANDLE_VALUE;
  ULONGLONG start_ = 0;
  size_t events_ = 0;
};
}
