#pragma once
#include <windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "profile_catalogue.h"

namespace gbrowser::webview::transfer {
using winrt::Windows::Data::Json::JsonObject;
constexpr size_t kMaximumBytes = 64 * 1024 * 1024;
// Portable authenticated archive. No machine-bound DPAPI keys or Chromium files.
std::string Encrypt(const std::string& plain, const std::wstring& password);
std::string Decrypt(const std::string& archive, const std::wstring& password);
std::string ReadArchive(const std::filesystem::path& path);
void WriteArchive(const std::filesystem::path& path, const std::string& archive);
void Validate(const JsonObject& document);
JsonObject ConfigurationOnly(const JsonObject& document);
std::string PrepareImport(ProfileCatalogue& profiles, const std::filesystem::path& installation,
    std::wstring_view leaf, const std::string& previous, const JsonObject& document);
void RestoreSessionCookies(ICoreWebView2* core, const JsonObject& document);
struct Request {
  std::filesystem::path path; std::wstring password;
  Request() = default;
  Request(Request&&) noexcept = default;
  Request& operator=(Request&&) noexcept = default;
  Request(const Request&) = delete;
  Request& operator=(const Request&) = delete;
  ~Request() { if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t)); }
};
bool Choose(HWND owner, bool importing, Request& request);
// Each instance owns a hidden, same-profile controller. Cancel also invalidates
// late COM completions; no callback refers to the Host after cancellation.
class BrowserTransfer {
 public:
  using Done = std::function<void(bool, JsonObject)>;
  BrowserTransfer();
  ~BrowserTransfer();
  void Start(ICoreWebView2Environment* environment, HWND parent,
      const std::vector<std::string>& extension_pages, bool importing, JsonObject document, Done done);
  void Cancel();
  std::wstring Failure() const;
 private:
  struct State;
  std::shared_ptr<State> state_;
};
}
