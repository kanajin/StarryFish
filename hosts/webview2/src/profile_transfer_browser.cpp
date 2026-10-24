#include "profile_transfer.h"
#include "core.h"
#include <wrl.h>
#include <shlwapi.h>
#include <array>
#include <chrono>
#include <stdexcept>
#ifdef STARRYFISH_TRANSFER_TEST_TRACE
#include <iostream>
#define TRANSFER_TRACE(x) (std::cout << x << "\n" << std::flush)
#else
#define TRANSFER_TRACE(x) ((void)0)
#endif

namespace gbrowser::webview::transfer {
using namespace winrt::Windows::Data::Json;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("迁移接口失败"); }
std::wstring Text(std::function<HRESULT(LPWSTR*)> getter) {
  LPWSTR value = nullptr; Check(getter(&value)); std::wstring result(value ? value : L""); CoTaskMemFree(value); return result;
}
std::wstring Quote(const std::wstring& value) { return std::wstring(JsonValue::CreateStringValue(value).Stringify()); }
constexpr const wchar_t* kOrigins[]{L"https://game.granbluefantasy.jp", L"http://game.granbluefantasy.jp",
    L"https://gbf.game.mbga.jp", L"http://gbf.game.mbga.jp", L"https://connect.mobage.jp", L"https://ssl.sp.mbga.jp"};
}
struct BrowserTransfer::State : std::enable_shared_from_this<BrowserTransfer::State> {
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> core;
  ComPtr<ICoreWebView2CookieManager> cookies;
  EventRegistrationToken navigation{}, resource{}, starting{};
  std::vector<std::string> pages;
  JsonObject document;
  Done done;
  size_t step = 0;
  std::wstring phase = L"创建恢复视图", failure;
  bool importing = false, canceled = false, creating = true, finished = false, finishing = false, finish_success = false;
  HWND dispatch = nullptr;
  std::shared_ptr<State> completion_hold;
  static LRESULT CALLBACK Dispatch(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) { state = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
    if (state && (message == WM_APP || message == WM_TIMER)) { state->Finalize(); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
  }
  ~State() { if (dispatch) DestroyWindow(dispatch); }
  void MakeDispatcher() {
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = Dispatch; cls.lpszClassName = L"StarryFishProfileTransferDispatch";
    RegisterClassW(&cls); dispatch = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
    if (!dispatch) throw std::runtime_error("迁移调度窗口失败");
  }
  std::wstring destination;
  void Finish(bool success) {
    if (finished || creating || finishing) return;
    finishing = true; finish_success = success; completion_hold = shared_from_this();
    // Closing the last hidden controller inside Runtime.evaluate's completion
    // crashes WebView2 155. Unwind the COM callback before teardown and Host callbacks.
    if (!PostMessageW(dispatch, WM_APP, 0, 0)) SetTimer(dispatch, 1, 1, nullptr);
  }
  void Finalize() {
    if (finished || !finishing) return;
    auto keep_alive = std::move(completion_hold);
    const bool success = finish_success;
    TRANSFER_TRACE("TRANSFER finish " << success);
    finished = true;
    if (core) { core->remove_NavigationCompleted(navigation); core->remove_WebResourceRequested(resource); core->remove_NavigationStarting(starting); }
    if (controller) controller->Close();
    cookies.Reset(); core.Reset(); controller.Reset(); environment.Reset();
    if (dispatch) { DestroyWindow(dispatch); dispatch = nullptr; }
    auto callback = std::move(done); if (callback) callback(success && !canceled, document);
    document = JsonObject{};
  }
  template<class F> void Guard(F f) { if (finished || finishing) return; if (canceled) { Finish(false); return; } try { f(); } catch (const std::exception& error) { (void)error; TRANSFER_TRACE("TRANSFER error " << error.what()); failure = phase + L"：" + Utf8ToWide(error.what()); Finish(false); } catch (const winrt::hresult_error& error) { failure = phase + L"：接口错误 " + std::to_wstring(static_cast<long>(error.code())); TRANSFER_TRACE("TRANSFER COM error " << error.code()); Finish(false); } catch (...) { failure = phase + L"：未知接口错误"; TRANSFER_TRACE("TRANSFER COM error"); Finish(false); } }
  void Created(HRESULT error, ICoreWebView2Controller* value) {
    TRANSFER_TRACE("TRANSFER controller callback " << error);
    creating = false; controller = value;
    Guard([&] {
      Check(error); if (!controller) throw std::runtime_error("迁移视图缺失");
      Check(controller->put_IsVisible(FALSE)); Check(controller->get_CoreWebView2(&core));
      ComPtr<ICoreWebView2Settings> settings; Check(core->get_Settings(&settings));
      // Never run an options page's startup/reset logic merely to read storage.
      // ExecuteScript/DevTools are host operations, independent of page scripting.
      Check(settings->put_IsScriptEnabled(FALSE)); Check(settings->put_AreDefaultScriptDialogsEnabled(FALSE));
      Check(settings->put_IsWebMessageEnabled(FALSE));
      ComPtr<ICoreWebView2_2> v2; Check(core.As(&v2)); Check(v2->get_CookieManager(&cookies));
      auto self = shared_from_this();
      Check(core->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL));
      Check(core->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
          [self](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            self->Guard([&] {
              ComPtr<ICoreWebView2WebResourceRequest> request; Check(args->get_Request(&request));
              const auto uri = Text([&](LPWSTR* p) { return request->get_Uri(p); });
              if (uri.rfind(L"chrome-extension://", 0) == 0) return;
              // Local synthetic document with the real origin; no HTTP request,
              // login flow, game script or remote resource is sent by this view.
              const char html[] = "<!doctype html><meta charset=utf-8><title>StarryFish transfer</title>";
              ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(html), sizeof(html) - 1));
              ComPtr<ICoreWebView2WebResourceResponse> response;
              Check(self->environment->CreateWebResourceResponse(stream.Get(), 200, L"OK", L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'none'", &response));
              Check(args->put_Response(response.Get()));
            }); return S_OK;
          }).Get(), &resource));
      Check(core->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
          [self](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            self->Guard([&] { const auto uri = Text([&](LPWSTR* p) { return args->get_Uri(p); });
              if (uri != self->destination) { args->put_Cancel(TRUE); throw std::runtime_error("迁移导航被重定向"); }
            }); return S_OK;
          }).Get(), &starting));
      Check(core->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
          [self](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            self->Guard([&] { BOOL success = FALSE; Check(args->get_IsSuccess(&success));
              if (!success) throw std::runtime_error("迁移页面失败"); self->Storage(); }); return S_OK;
          }).Get(), &navigation));
      Check(core->CallDevToolsProtocolMethod(L"Emulation.setScriptExecutionDisabled", L"{\"value\":true}",
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([self](HRESULT hr, LPCWSTR) -> HRESULT {
          self->Guard([&] {
            TRANSFER_TRACE("TRANSFER disable scripts result " << hr); Check(hr);
            // Existing site service workers must not intercept the synthetic
            // storage documents or fetch a real game/login response.
            Check(self->core->CallDevToolsProtocolMethod(L"Network.setBypassServiceWorker", L"{\"bypass\":true}",
              Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([self](HRESULT result, LPCWSTR) -> HRESULT {
                self->Guard([&] { Check(result); self->BeginCookies(); }); return S_OK;
              }).Get()));
          }); return S_OK;
        }).Get()));
    });
  }
  void BeginCookies() {
    TRANSFER_TRACE("TRANSFER cookies begin");
    auto self = shared_from_this();
      if (importing) RestoreCookies();
      else { document.Insert(L"extensions", JsonArray{}); document.Insert(L"sites", JsonObject{}); }
      phase = L"Cookie 回读校验";
        Check(cookies->GetCookies(L"", Callback<ICoreWebView2GetCookiesCompletedHandler>(
          [self](HRESULT hr, ICoreWebView2CookieList* list) -> HRESULT {
            self->Guard([&] { Check(hr); if (!list) throw std::runtime_error("Cookie 列表缺失");
              UINT count = 0; Check(list->get_Count(&count)); if (count > 10000) throw std::runtime_error("Cookie 过多");
              JsonArray output;
              for (UINT i = 0; i < count; ++i) {
                ComPtr<ICoreWebView2Cookie> cookie; Check(list->GetValueAtIndex(i, &cookie)); JsonObject item;
                item.Insert(L"name", JsonValue::CreateStringValue(Text([&](LPWSTR* p) { return cookie->get_Name(p); })));
                item.Insert(L"value", JsonValue::CreateStringValue(Text([&](LPWSTR* p) { return cookie->get_Value(p); })));
                item.Insert(L"domain", JsonValue::CreateStringValue(Text([&](LPWSTR* p) { return cookie->get_Domain(p); })));
                item.Insert(L"path", JsonValue::CreateStringValue(Text([&](LPWSTR* p) { return cookie->get_Path(p); })));
                double expires = 0; BOOL secure = FALSE, http = FALSE, session = FALSE; COREWEBVIEW2_COOKIE_SAME_SITE_KIND same{};
                Check(cookie->get_Expires(&expires)); Check(cookie->get_IsSecure(&secure)); Check(cookie->get_IsHttpOnly(&http));
                Check(cookie->get_IsSession(&session)); Check(cookie->get_SameSite(&same));
                item.Insert(L"expires", JsonValue::CreateNumberValue(expires)); item.Insert(L"sameSite", JsonValue::CreateNumberValue(same));
                item.Insert(L"secure", JsonValue::CreateBooleanValue(secure != FALSE)); item.Insert(L"httpOnly", JsonValue::CreateBooleanValue(http != FALSE));
                item.Insert(L"session", JsonValue::CreateBooleanValue(session != FALSE)); output.Append(item);
              }
              if (self->importing) {
                for (const auto& expected : self->document.GetNamedArray(L"cookies")) {
                  const auto wanted = expected.GetObject(); bool found = false;
                  for (const auto& actual : output) {
                    const auto got = actual.GetObject();
                    if (got.GetNamedString(L"name") != wanted.GetNamedString(L"name") ||
                        got.GetNamedString(L"domain") != wanted.GetNamedString(L"domain") ||
                        got.GetNamedString(L"path") != wanted.GetNamedString(L"path")) continue;
                    found = true; Verify(got, wanted); break;
                  }
                  const auto now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
                  if (!found && (wanted.GetNamedBoolean(L"session") || wanted.GetNamedNumber(L"expires") > now))
                    throw std::runtime_error("Cookie 回读不一致");
                }
              } else self->document.Insert(L"cookies", output);
              self->Next();
            }); return S_OK;
          }).Get()));

  }
  void RestoreCookies() {
    phase = L"Cookie 写入";
    for (const auto& value : document.GetNamedArray(L"cookies")) {
      const auto item = value.GetObject(); ComPtr<ICoreWebView2Cookie> cookie;
      Check(cookies->CreateCookie(item.GetNamedString(L"name").c_str(), item.GetNamedString(L"value").c_str(),
          item.GetNamedString(L"domain").c_str(), item.GetNamedString(L"path").c_str(), &cookie));
      Check(cookie->put_IsSecure(item.GetNamedBoolean(L"secure"))); Check(cookie->put_IsHttpOnly(item.GetNamedBoolean(L"httpOnly")));
      Check(cookie->put_SameSite(static_cast<COREWEBVIEW2_COOKIE_SAME_SITE_KIND>(static_cast<int>(item.GetNamedNumber(L"sameSite")))));
      Check(cookie->put_Expires(item.GetNamedBoolean(L"session") ? -1.0 : item.GetNamedNumber(L"expires")));
      Check(cookies->AddOrUpdateCookie(cookie.Get()));
    }
  }
  void Next() {
    if (step >= pages.size() + std::size(kOrigins)) { Finish(true); return; }
    TRANSFER_TRACE("TRANSFER navigate step " << step);
    constexpr const wchar_t* roles[]{L"ZeroOmega", L"太郎", L"肉鸽", L"书签与汉化"};
    phase = step < pages.size() ? std::wstring(L"扩展导航：") + roles[step] : L"站点存储导航 " + std::to_wstring(step - pages.size() + 1);
    destination = step < pages.size() ? Utf8ToWide(pages[step]) : std::wstring(kOrigins[step - pages.size()]) + L"/starryfish-profile-transfer";
    Check(core->Navigate(destination.c_str()));
  }
  void Storage() {
    TRANSFER_TRACE("TRANSFER storage step " << step);
    phase += L" / 存储写入与回读";
    const bool extension = step < pages.size();
    const std::wstring origin = extension ? L"" : kOrigins[step - pages.size()];
    std::wstring script = LR"((async()=>{const page=()=>Object.fromEntries(Object.keys(localStorage).map(k=>[k,localStorage.getItem(k)]));)";
    if (importing) {
      const auto data = extension ? document.GetNamedArray(L"extensions").GetObjectAt(static_cast<uint32_t>(step)) : document.GetNamedObject(L"sites").GetNamedObject(origin);
      script += L"const v=JSON.parse(" + Quote(std::wstring(data.Stringify())) + L");";
      // Imports use a fresh profile. Do not clear extension storage: reactive
      // workers treat deletion as a request to asynchronously restore defaults,
      // racing and overwriting the archived values after set() completes.
      if (extension) script += LR"(if(chrome.storage?.local){await chrome.storage.local.set(v.local);}else if(Object.keys(v.local).length)throw Error("Missing local storage capability");if(chrome.storage?.sync){await chrome.storage.sync.set(v.sync);}const p=v.page;)";
      else script += L"const p=v;";
      script += LR"(localStorage.clear();for(const [k,v] of Object.entries(p))localStorage.setItem(k,v);)";
      // Return/read back the actual resulting store, not a synthetic success.
    }
    script += extension ? LR"(return {local:chrome.storage?.local?await chrome.storage.local.get(null):{},sync:chrome.storage?.sync?await chrome.storage.sync.get(null):{},page:page()};})())" : LR"(return page();})())";
    JsonObject parameters; parameters.Insert(L"expression", JsonValue::CreateStringValue(script));
    parameters.Insert(L"awaitPromise", JsonValue::CreateBooleanValue(true)); parameters.Insert(L"returnByValue", JsonValue::CreateBooleanValue(true));
    auto self = shared_from_this();
    Check(core->CallDevToolsProtocolMethod(L"Runtime.evaluate", parameters.Stringify().c_str(),
      Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([self, extension, origin](HRESULT hr, LPCWSTR json) -> HRESULT {
        self->Guard([&] {
          Check(hr); const auto reply = JsonObject::Parse(json ? json : L"{}");
          if (reply.HasKey(L"exceptionDetails")) throw std::runtime_error("存储接口失败");
          const auto value = reply.GetNamedObject(L"result").GetNamedObject(L"value");
          if (self->importing) {
            const auto expected = extension ? self->document.GetNamedArray(L"extensions").GetObjectAt(static_cast<uint32_t>(self->step)) : self->document.GetNamedObject(L"sites").GetNamedObject(origin);
            // JSON member order may differ. Compare recursively via a canonical
            // JS comparison below rather than text equality of WinRT objects.
            if (extension) {
              // A newly installed extension may add defaults while its worker starts.
              // Every archived entry must match; unrelated new defaults are allowed.
              for (auto area : {L"local", L"sync", L"page"}) {
                const auto actualStore = value.GetNamedObject(area);
                for (const auto& pair : expected.GetNamedObject(area)) {
#ifdef STARRYFISH_TRANSFER_TEST_TRACE
                  try { self->Verify(actualStore.Lookup(pair.Key()), pair.Value()); }
                  catch (...) {
                    TRANSFER_TRACE("SYNTHETIC mismatch " << winrt::to_string(area) << "/" << winrt::to_string(pair.Key())
                        << " expected=" << winrt::to_string(pair.Value().Stringify())
                        << " actual=" << (actualStore.HasKey(pair.Key()) ? winrt::to_string(actualStore.Lookup(pair.Key()).Stringify()) : "<missing>"));
                    throw;
                  }
#else
                  self->Verify(actualStore.Lookup(pair.Key()), pair.Value());
#endif
                }
              }
            } else self->Verify(value, expected);
          } else if (extension) self->document.GetNamedArray(L"extensions").Append(value);
          else self->document.GetNamedObject(L"sites").Insert(origin, value);
          ++self->step; self->Next();
        }); return S_OK;
      }).Get()));
  }
  static void Verify(const IJsonValue& actual, const IJsonValue& expected) {
    if (actual.ValueType() != expected.ValueType()) throw std::runtime_error("资料回读不一致");
    if (actual.ValueType() == JsonValueType::Object) {
      auto a = actual.GetObject(), b = expected.GetObject(); if (a.Size() != b.Size()) throw std::runtime_error("资料回读不一致");
      for (auto pair : b) Verify(a.Lookup(pair.Key()), pair.Value());
    } else if (actual.ValueType() == JsonValueType::Array) {
      auto a = actual.GetArray(), b = expected.GetArray(); if (a.Size() != b.Size()) throw std::runtime_error("资料回读不一致");
      for (uint32_t i = 0; i < a.Size(); ++i) Verify(a.GetAt(i), b.GetAt(i));
    } else if (actual.Stringify() != expected.Stringify()) throw std::runtime_error("资料回读不一致");
  }
};
void RestoreSessionCookies(ICoreWebView2* core, const JsonObject& document) {
  ComPtr<ICoreWebView2_2> v2; Check(core->QueryInterface(IID_PPV_ARGS(&v2)));
  ComPtr<ICoreWebView2CookieManager> manager; Check(v2->get_CookieManager(&manager));
  for (const auto& value : document.GetNamedArray(L"cookies")) {
    const auto item = value.GetObject(); if (!item.GetNamedBoolean(L"session")) continue;
    ComPtr<ICoreWebView2Cookie> cookie;
    Check(manager->CreateCookie(item.GetNamedString(L"name").c_str(), item.GetNamedString(L"value").c_str(),
        item.GetNamedString(L"domain").c_str(), item.GetNamedString(L"path").c_str(), &cookie));
    Check(cookie->put_IsSecure(item.GetNamedBoolean(L"secure"))); Check(cookie->put_IsHttpOnly(item.GetNamedBoolean(L"httpOnly")));
    Check(cookie->put_SameSite(static_cast<COREWEBVIEW2_COOKIE_SAME_SITE_KIND>(static_cast<int>(item.GetNamedNumber(L"sameSite")))));
    Check(cookie->put_Expires(-1)); Check(manager->AddOrUpdateCookie(cookie.Get()));
  }
}
BrowserTransfer::BrowserTransfer() = default;
BrowserTransfer::~BrowserTransfer() { Cancel(); }
void BrowserTransfer::Start(ICoreWebView2Environment* environment, HWND parent, const std::vector<std::string>& pages,
    bool importing, JsonObject document, Done done) {
  if (state_) throw std::runtime_error("迁移已在运行");
  state_ = std::make_shared<State>(); auto state = state_; state->environment = environment; state->pages = pages;
  state->importing = importing; state->document = document; state->done = std::move(done); state->MakeDispatcher();
  const auto hr = environment->CreateCoreWebView2Controller(parent,
      Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([state](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
        state->Created(result, controller); return S_OK;
      }).Get());
  if (FAILED(hr)) { state->creating = false; state->Finish(false); }
}
std::wstring BrowserTransfer::Failure() const { return state_ ? (state_->failure.empty() ? state_->phase + L"：操作取消或超时" : state_->failure) : L"恢复未启动"; }
void BrowserTransfer::Cancel() { if (state_) { auto state = state_; state->canceled = true; state->Finish(false); } }
}
