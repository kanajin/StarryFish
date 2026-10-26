#pragma once

#include "include/cef_browser.h"
#include <string>

namespace starryfish::linux_host {
// Fixed, local diagnostic documents only. No arbitrary URL or browser switches.
std::string DiagnosticUrl(const std::string& page);
void ReportDiagnosticPage(CefRefPtr<CefBrowser> browser);
}  // namespace starryfish::linux_host
