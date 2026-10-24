#include "profile_transfer.h"
#include "user_state.h"
#include "character_shortcuts.h"
#include "proxy_preference.h"
#include "core.h"
#include <WebView2EnvironmentOptions.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <array>

using namespace gbrowser;
using namespace gbrowser::webview;
using namespace winrt::Windows::Data::Json;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("synthetic migration runtime check failed"); }
void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }
void Pump(const std::function<bool()>& done) {
  const auto start = GetTickCount64();
  while (!done()) {
    Require(GetTickCount64() - start < 90000); MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
  }
}
JsonObject Fixture() {
  JsonObject doc, settings;
  doc.Insert(L"version", JsonValue::CreateNumberValue(1)); doc.Insert(L"name", JsonValue::CreateStringValue(L"Synthetic migration"));
  auto put = [&](const wchar_t* name, const std::string& value) { settings.Insert(name, JsonValue::CreateStringValue(Utf8ToWide(value))); };
  put(L"window-layout.state", EncodeWindowState({})); put(L"tarou-window.state", EncodeWindowState({}));
  put(L"game-panes.state", EncodePaneState({})); put(L"appearance.state", EncodeDarkAppearance(true));
  put(L"layout-choice.state", EncodeBrowserLayout(true)); put(L"loading-display.state", EncodeHiddenLoading(true));
  put(L"shortcut-keys.state", characters::Encode({})); put(L"static-cache.state", "STARRYFISH-CACHE-OFF\n"); put(L"skill-overlay.state", "ON\n");
  doc.Insert(L"settings", settings); doc.Insert(L"proxy", JsonValue::CreateStringValue(Utf8ToWide(EncodeProxyPreference({}))));
  JsonArray extensions;
  for (int i = 0; i < 4; ++i) extensions.Append(i == 3 ? JsonObject::Parse(LR"({"local":{},"sync":{},"page":{"test":"persist"}})") : JsonObject::Parse(LR"({"local":{"specPlayerBuff":["101","202"],"specBossBuff":["303"],"onlyShowSpecBuff":true,"translation":{"enabled":false}},"sync":{"setting":42},"page":{"test":"persist"}})"));
  doc.Insert(L"extensions", extensions); JsonObject sites;
  for (auto origin : {L"https://game.granbluefantasy.jp", L"http://game.granbluefantasy.jp", L"https://gbf.game.mbga.jp", L"http://gbf.game.mbga.jp", L"https://connect.mobage.jp", L"https://ssl.sp.mbga.jp"})
    sites.Insert(origin, JsonObject::Parse(LR"({"gbf-bookmark:data":"[\"synthetic bookmark\"]","gbf-bookmark:config":"{\"color\":\"red\"}","translation":"synthetic"})"));
  doc.Insert(L"sites", sites);
  doc.Insert(L"cookies", JsonArray::Parse(LR"([{"name":"synthetic-session","value":"NOT_A_REAL_LOGIN","domain":".granbluefantasy.jp","path":"/","expires":-1,"session":true,"secure":true,"httpOnly":true,"sameSite":1},{"name":"synthetic-persistent","value":"NOT_A_REAL_LOGIN_EITHER","domain":".mobage.jp","path":"/","expires":2208988800,"session":false,"secure":true,"httpOnly":false,"sameSite":2}])"));
  doc.GetNamedArray(L"cookies").GetObjectAt(1).Insert(L"expires", JsonValue::CreateNumberValue(static_cast<double>(std::time(nullptr)) + 86400.123456));
  transfer::Validate(doc); return doc;
}
struct Runtime {
  HWND window = nullptr;
  ComPtr<ICoreWebView2Environment> environment;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> core;
  std::vector<std::string> pages;
  HANDLE process = nullptr;
  explicit Runtime(const std::filesystem::path& root, bool product = false) {
    std::cout << "STEP create isolated runtime\n" << std::flush;
    std::filesystem::create_directory(root);
    window = CreateWindowExW(0, L"STATIC", L"Synthetic profile transfer", WS_OVERLAPPED, 0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr);
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>(); Check(options->put_AreBrowserExtensionsEnabled(TRUE));
    // No external navigation; background network disabled in this synthetic harness.
    Check(options->put_AdditionalBrowserArguments(L"--disable-background-networking"));
    bool ready = false; HRESULT result = E_FAIL;
    Check(CreateCoreWebView2EnvironmentWithOptions(nullptr, (root / L"Data").c_str(), options.Get(),
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT hr, ICoreWebView2Environment* value) -> HRESULT {
        result = hr; environment = value; ready = true; return S_OK;
      }).Get())); Pump([&] { return ready; }); Check(result);
    std::cout << "STEP create fixture controller\n" << std::flush;
    ready = false;
    Check(environment->CreateCoreWebView2Controller(window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
      [&](HRESULT hr, ICoreWebView2Controller* value) -> HRESULT { result = hr; controller = value; ready = true; return S_OK; }).Get()));
    Pump([&] { return ready; }); Check(result); Check(controller->put_IsVisible(FALSE)); Check(controller->get_CoreWebView2(&core));
    EventRegistrationToken popup{};
    Check(core->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT { return args->put_Handled(TRUE); }).Get(), &popup));
    UINT pid = 0; Check(core->get_BrowserProcessId(&pid)); process = OpenProcess(SYNCHRONIZE, FALSE, pid); Require(process != nullptr);
    ComPtr<ICoreWebView2_13> v13; Check(core.As(&v13)); ComPtr<ICoreWebView2Profile> base; Check(v13->get_Profile(&base));
    ComPtr<ICoreWebView2Profile7> profile; Check(base.As(&profile));
    std::cout << "STEP install fixture extensions\n" << std::flush;
    for (int i = 0; i < 4; ++i) {
      auto extension = root / (L"Extension" + std::to_wstring(i));
      std::string entry = "page.html";
      if (product) {
        std::array<wchar_t, MAX_PATH> executable{}; GetModuleFileNameW(nullptr, executable.data(), MAX_PATH);
        const auto payload = std::filesystem::path(executable.data()).parent_path();
        const wchar_t* folders[]{L"Components/zeroomega", L"Components/tarou", L"Components/arcarum-dungeon", L"Scripts"};
        const char* entries[]{"options.html", "src/views/sidePanel/main.html", "src/panel/index.html", "status.html"};
        if (!std::filesystem::exists(extension)) std::filesystem::copy(payload / folders[i], extension, std::filesystem::copy_options::recursive);
        entry = entries[i];
      } else {
      std::filesystem::create_directory(extension);
      { std::ofstream f(extension / L"manifest.json"); f << (i == 3 ? R"({"manifest_version":3,"name":"Synthetic profile transfer","version":"1.0","content_scripts":[{"matches":["https://game.granbluefantasy.jp/*"],"js":["content.js"],"run_at":"document_start","world":"MAIN"},{"matches":["https://game.granbluefantasy.jp/*"],"js":["content.js"],"run_at":"document_start","world":"ISOLATED"}]})" : R"({"manifest_version":3,"name":"Synthetic profile transfer","version":"1.0","permissions":["storage"]})"); }
      { std::ofstream f(extension / L"page.html"); f << "<!doctype html><title>Synthetic</title><script src=page.js></script>"; }
      { std::ofstream f(extension / L"page.js"); f << "localStorage.setItem('UNWANTED_PAGE_SCRIPT','ran');"; }
      { std::ofstream f(extension / L"content.js"); f << "localStorage.setItem('UNWANTED_CONTENT_SCRIPT','ran');"; }
      }
      ready = false; std::wstring id;
      Check(profile->AddBrowserExtension(extension.c_str(), Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
        [&](HRESULT hr, ICoreWebView2BrowserExtension* value) -> HRESULT {
          result = hr; if (value) { LPWSTR text = nullptr; value->get_Id(&text); id = text ? text : L""; CoTaskMemFree(text); }
          ready = true; return S_OK;
        }).Get())); Pump([&] { return ready; }); Check(result); Require(!id.empty());
      pages.push_back("chrome-extension://" + WideToUtf8(id) + "/" + entry);
    }
  }
  ~Runtime() noexcept(false) { if (controller) controller->Close(); core.Reset(); controller.Reset(); environment.Reset();
    if (process) { Pump([&] { return WaitForSingleObject(process, 0) == WAIT_OBJECT_0; }); CloseHandle(process); }
    if (window) DestroyWindow(window);
  }
  JsonObject Run(bool importing, JsonObject document) {
    bool ready = false, success = false; JsonObject output; transfer::BrowserTransfer transfer;
    std::cout << "STEP start transfer importing=" << importing << "\n" << std::flush;
    transfer.Start(environment.Get(), window, pages, importing, document, [&](bool ok, JsonObject result) { success = ok; output = result; ready = true; });
    Pump([&] { return ready; }); std::cout << "STEP transfer callback " << importing << " success " << success << "\n" << std::flush; if (!success) std::wcerr << transfer.Failure() << L"\n"; Require(success); return output;
  }
};
}
int wmain(int argc, wchar_t** argv) {
  try {
    Require(argc == 2); winrt::init_apartment(winrt::apartment_type::single_threaded);
    const std::filesystem::path root(argv[1]); Require(std::filesystem::is_empty(root));
    std::cout << "STEP build synthetic document\n" << std::flush;
    auto doc = Fixture();
    auto config = transfer::ConfigurationOnly(doc);
    Require(config.GetNamedArray(L"cookies").Size() == 0 && doc.GetNamedArray(L"cookies").Size() == 2);
    Require(config.GetNamedObject(L"settings").Stringify() == doc.GetNamedObject(L"settings").Stringify());
    Require(config.GetNamedArray(L"extensions").Stringify() == doc.GetNamedArray(L"extensions").Stringify());
    Require(config.GetNamedObject(L"sites").Stringify() == doc.GetNamedObject(L"sites").Stringify());
    {
      Runtime configTarget(root / L"ConfigurationOnly");
      configTarget.Run(true, config);
      auto checked = configTarget.Run(false, config);
      Require(checked.GetNamedArray(L"cookies").Size() == 0);
      Require(checked.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local").GetNamedBoolean(L"onlyShowSpecBuff"));
      Require(checked.GetNamedObject(L"sites").GetNamedObject(L"https://game.granbluefantasy.jp").HasKey(L"gbf-bookmark:data"));
    }

    const auto installation = root / L"Installation"; std::filesystem::create_directory(installation);
    ProfileCatalogue profiles(installation, L"UserData");
    std::cout << "STEP prepare imported profile on disk\n" << std::flush;
    const auto id = transfer::PrepareImport(profiles, installation, L"UserData", "default", doc);
    profiles.Retire(id, "default");
    const auto second_id = transfer::PrepareImport(profiles, installation, L"UserData", "default", doc);
    Require(id != second_id && profiles.Find(id, true).name != profiles.Find(second_id).name);
    Require(UserStateFile(ProfileDataPath(installation, L"UserData", second_id) / L"HostSettings" / L"shortcut-keys.state").bytes().has_value());
    transfer::WriteArchive(root / L"synthetic-ui.sfprofile", transfer::Encrypt(WideToUtf8(std::wstring(doc.Stringify())), L"Synthetic-test-password"));

    for (int invalid = 0; invalid < 4; ++invalid) {
      auto bad = JsonObject::Parse(doc.Stringify());
      if (invalid == 0) bad.Insert(L"version", JsonValue::CreateNumberValue(99));
      if (invalid == 1) bad.GetNamedObject(L"settings").Insert(L"../escape", JsonValue::CreateStringValue(L"bad"));
      if (invalid == 2) bad.GetNamedObject(L"sites").Remove(L"https://game.granbluefantasy.jp");
      if (invalid == 3) bad.GetNamedObject(L"settings").Insert(L"shortcut-keys.state", JsonValue::CreateStringValue(L"corrupt"));
      bool rejected = false; try { transfer::Validate(bad); } catch (...) { rejected = true; } Require(rejected);
    }
    std::vector<std::string> first_ids;
    {
      Runtime first(root / L"MachineA"); first_ids = first.pages;
      first.Run(true, doc); doc = first.Run(false, doc); transfer::Validate(doc);
      std::cout << "STEP verify cookies/scripts/schema\n" << std::flush;
      Require(doc.GetNamedArray(L"cookies").Size() == 2);
      Require(!doc.GetNamedObject(L"sites").GetNamedObject(L"https://game.granbluefantasy.jp").HasKey(L"UNWANTED_CONTENT_SCRIPT"));
      Require(!doc.GetNamedArray(L"extensions").GetObjectAt(0).GetNamedObject(L"page").HasKey(L"UNWANTED_PAGE_SCRIPT"));
      const auto encrypted = transfer::Encrypt(WideToUtf8(std::wstring(doc.Stringify())), L"Synthetic-test-password");
      doc = JsonObject::Parse(Utf8ToWide(transfer::Decrypt(encrypted, L"Synthetic-test-password")));
    }
    {
      Runtime second(root / L"MachineB"); Require(first_ids != second.pages);
      {
        transfer::BrowserTransfer canceled; bool complete = false; int calls = 0;
        canceled.Start(second.environment.Get(), second.window, second.pages, false, doc,
            [&](bool ok, JsonObject) { Require(!ok); ++calls; complete = true; });
        canceled.Cancel(); Pump([&] { return complete; }); Require(calls == 1);
      }
      second.Run(true, doc); const auto restored = second.Run(false, doc); transfer::Validate(restored);
      Require(restored.GetNamedArray(L"cookies").Size() == 2);
      Require(restored.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local").GetNamedBoolean(L"onlyShowSpecBuff"));
      Require(restored.GetNamedObject(L"sites").GetNamedObject(L"https://game.granbluefantasy.jp").HasKey(L"gbf-bookmark:data"));
    }
    {
      Runtime reopened(root / L"MachineB"); transfer::RestoreSessionCookies(reopened.core.Get(), doc);
      const auto after_restart = reopened.Run(false, doc); transfer::Validate(after_restart);
      Require(after_restart.GetNamedArray(L"cookies").Size() == 2);
      Require(after_restart.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local").GetNamedBoolean(L"onlyShowSpecBuff"));
    }
    JsonObject productDocument;
    {
      Runtime source(root / L"ProductSource", true);
      productDocument = source.Run(false, doc); transfer::Validate(productDocument);
      // Use Tarou's real serialized value types and deliberately old timestamp.
      // Clearing its store triggers the reactive worker to overwrite these.
      auto tarou = productDocument.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local");
      tarou.Insert(L"lastUpdateTodo", JsonValue::CreateStringValue(L"1600000000"));
      tarou.Insert(L"specPlayerBuff", JsonValue::CreateStringValue(L"[\"101\",\"202\"]"));
      tarou.Insert(L"specBossBuff", JsonValue::CreateStringValue(L"[\"303\"]"));
      tarou.Insert(L"onlyShowSpecBuff", JsonValue::CreateStringValue(L"true"));
      source.Run(true, productDocument);
      productDocument = source.Run(false, productDocument);
      Require(productDocument.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local").GetNamedString(L"lastUpdateTodo") == L"1600000000");
      transfer::WriteArchive(root / L"product.sfprofile", transfer::Encrypt(WideToUtf8(std::wstring(productDocument.Stringify())), L"Synthetic-test-password"));
      Runtime target(root / L"ProductTarget", true);
      auto parsed = JsonObject::Parse(Utf8ToWide(transfer::Decrypt(transfer::ReadArchive(root / L"product.sfprofile"), L"Synthetic-test-password")));
      target.Run(true, parsed);
      std::cout << "PASS actual product components export/archive/import across different directories\n" << std::flush;
    }
    {
      Runtime reopened(root / L"ProductTarget", true);
      const auto restored = reopened.Run(false, productDocument);
      auto tarou = restored.GetNamedArray(L"extensions").GetObjectAt(1).GetNamedObject(L"local");
      Require(tarou.GetNamedString(L"lastUpdateTodo") == L"1600000000");
      Require(tarou.GetNamedString(L"specPlayerBuff") == L"[\"101\",\"202\"]");
      Require(tarou.GetNamedString(L"specBossBuff") == L"[\"303\"]");
      Require(tarou.GetNamedString(L"onlyShowSpecBuff") == L"true");
      std::cout << "PASS actual Tarou customized Buff settings and timestamp survive restart\n" << std::flush;
    }
    std::cout << "PASS isolated WebView2: four extensions with changed IDs, local/sync/page stores, six site origins, session+HttpOnly+Secure+SameSite cookies, encrypted archive; no user profiles or game network used\n";
    return 0;
  } catch (...) { std::cerr << "FAIL synthetic profile migration runtime; no data dumped\n"; return 1; }
}
