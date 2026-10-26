#include "diagnostics.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_string_visitor.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <iostream>

namespace starryfish::linux_host {
namespace {
bool Reportable(const std::string& url) {
  return url == "chrome://gpu/" || url == "chrome://sandbox/";
}

class DiagnosticText final : public CefStringVisitor {
 public:
  DiagnosticText(CefRefPtr<CefBrowser> browser, std::string url)
      : browser_(browser), url_(std::move(url)) {}
  void Visit(const CefString& text) override {
    CEF_REQUIRE_UI_THREAD();
    if (!browser_->IsValid() || !browser_->GetMainFrame() ||
        browser_->GetMainFrame()->GetURL().ToString() != url_) return;
    const auto value = text.ToString();
    if (value.size() > 128 * 1024) return;
    if (value.empty()) {
      std::cout << "LOCAL DIAGNOSTIC " << url_
                << ": text unavailable; inspect the visible diagnostic page\n" << std::flush;
      return;
    }
    std::cout << "LOCAL DIAGNOSTIC " << url_ << '\n' << value << '\n' << std::flush;
  }
 private:
  CefRefPtr<CefBrowser> browser_;
  std::string url_;
  IMPLEMENT_REFCOUNTING(DiagnosticText);
};
}  // namespace

std::string DiagnosticUrl(const std::string& page) {
  if (page == "gpu" || page == "sandbox") return "chrome://" + page + "/";
  if (page != "input") return {};
  // This document has no script, network, storage or privileged message bridge.
  constexpr char html[] = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'nonce-sf-input'; form-action 'none'; base-uri 'none'">
<title>StarryFish 输入与缩放检查</title>
<style nonce="sf-input">body{background:#e2eef6;color:#1e394f;font:18px sans-serif;margin:40px;max-width:800px}input,textarea{box-sizing:border-box;width:100%;font:inherit;padding:12px;margin:8px 0 24px;border:1px solid #6d91aa;background:#f2f7fb;color:#1e394f}.ruler{width:100px;height:100px;background:#c0dbee;border:1px solid #24638f;display:grid;place-items:center}</style>
<h1>StarryFish 输入与缩放检查</h1><p>本页仅使用合成输入。请不要输入账号、密码或其他私人资料。</p>
<label>中文输入法：输入“星鱼中文测试”，观察候选框位置、提交和删除。<input autofocus autocomplete="off" placeholder="星鱼中文测试"></label>
<label>多行文本：测试换行及中英文切换。<textarea rows="3" autocomplete="off"></textarea></label>
<p>Ctrl + 加号／减号调整页面缩放，Ctrl + 0 恢复 100%。窗口标题显示倍率。检查 100%、125%、150% 时文字、边框与输入候选框。</p><div class="ruler">100 CSS px</div>
<p>中文显示：星鱼浏览器　日本語　English　1234567890</p></html>)HTML";
  return "data:text/html;base64," + CefBase64Encode(html, sizeof(html) - 1).ToString();
}

void ReportDiagnosticPage(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser->IsValid() || !browser->GetMainFrame()) return;
  const auto url = browser->GetMainFrame()->GetURL().ToString();
  if (!Reportable(url)) return;
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> target, std::string expected) {
    if (!target->IsValid() || !target->GetMainFrame() ||
        target->GetMainFrame()->GetURL().ToString() != expected) return;
    target->GetMainFrame()->GetText(new DiagnosticText(target, expected));
  }, browser, url), 3000);
}
}  // namespace starryfish::linux_host
