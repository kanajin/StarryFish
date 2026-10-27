#pragma once
#include "include/cef_client.h"
#include "include/cef_browser.h"
#include <filesystem>
#include <string>
#include <vector>

namespace starryfish::linux_host {
class DesktopShell : public virtual CefBaseRefCounted {
 public:
  virtual void BrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
  virtual bool BrowserReadyToClose(CefRefPtr<CefBrowser> browser) = 0;
  virtual void BrowserClosed(CefRefPtr<CefBrowser> browser) = 0;
  virtual void TitleChanged(CefRefPtr<CefBrowser> browser, const CefString& title) = 0;
  virtual void AddressChanged(CefRefPtr<CefBrowser> browser, const CefString& url) = 0;
  virtual void NavigationStarted(CefRefPtr<CefBrowser> browser) = 0;
  virtual void FaviconChanged(CefRefPtr<CefBrowser> browser,
                              const std::vector<CefString>& urls) = 0;
  virtual bool AllowNavigation(CefRefPtr<CefBrowser> browser, const std::string& url) = 0;
  virtual void DialogClosed() = 0;
};
CefRefPtr<DesktopShell> CreateDesktopShell(CefRefPtr<CefClient> client,
                                         const std::string& initial_url,
                                         const std::filesystem::path& config);
}  // namespace starryfish::linux_host
