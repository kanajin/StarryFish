#include "directory_pin.h"
#include "component_catalogue.h"
#include "core.h"
#include "launch_plan.h"
#include "tab_slots.h"
#include "application_gate.h"
#include "zero_initialization.h"
#include "target_reply.h"
#include "ui_interaction.h"
#include "user_state.h"
#include "profile_catalogue.h"
#include "proxy_preference.h"
#include "proxy_profiles.h"
#include "bookmark_navigation.h"
#include "convenience_layout.h"
#include "game_copy.h"
#include "browser_history.h"
#include "login_resume.h"
#include "compact_chrome.h"
#include "host_ui_flow.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
void RunResourceCacheChecks(const std::filesystem::path& root);

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int pane_destroyed = 0, pane_host_dispatch = 0;
LRESULT CALLBACK PaneTeardownCheck(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA,
      reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
  if (message == WM_DESTROY) {
    ++pane_destroyed;
    if (GetWindowLongPtrW(window, GWLP_USERDATA)) ++pane_host_dispatch;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}
int wmain(int argc, wchar_t** argv) {
  try {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--catalogue") {
      const auto fixture = std::filesystem::path(argv[2]);
      gbrowser::webview::DirectoryPins pins; pins.Tree(fixture);
      gbrowser::webview::ComponentCatalogue valid(fixture / L"valid");
      Require(valid.installed() && valid.entries()[1].files == 3 && valid.entries()[2].files == 3,
          "managed/native accepted catalogue decode mismatch");
      for (const auto* name : {L"corrupt", L"wrong-root"}) {
        bool rejected = false;
        try { gbrowser::webview::ComponentCatalogue invalid(fixture / name); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "invalid/tampered/other-root catalogue accepted");
      }
      std::cout << "PASS managed/native catalogue decode; tampered/root binding rejection; browser NOT_STARTED\n";
      return 0;
    }
    Require(argc == 2, "one isolated test directory required");
    {
      using gbrowser::webview::LoginResume;
      using gbrowser::webview::LoginResumeEntry;
      Require(LoginResumeEntry(L"https://game.granbluefantasy.jp/#mypage") &&
          LoginResumeEntry(L"https://game.granbluefantasy.jp/#top") &&
          LoginResumeEntry(L"https://gbf.game.mbga.jp/"), "known game login entry rejected");
      for (const wchar_t* url : {L"https://game.granbluefantasy.jp/#raid_multi/1",
          L"https://game.granbluefantasy.jp/?code=secret", L"https://connect.mobage.jp/",
          L"http://game.granbluefantasy.jp/", L"https://game.granbluefantasy.jp.evil.test/",
          L"https://game.granbluefantasy.jp/oauth/callback", L"https://game.granbluefantasy.jp/#mypage?state=secret"})
        Require(!LoginResumeEntry(url), "login recovery admitted callback/battle/spoof/query");
      const LoginResume recovery{1, 4, 9, L"https://game.granbluefantasy.jp/#mypage"};
      const LoginResume fixed_game{0, 4, 9, recovery.source};
      Require(fixed_game.Matches(0, 4, 9, recovery.source), "fixed game generation zero cannot resume");
      Require(!LoginResume{}.Matches(0, 0, 0, L""), "empty login recovery admitted");
      Require(recovery.Matches(1, 4, 9, recovery.source), "unchanged opener cannot resume");
      Require(!recovery.Matches(2, 4, 9, recovery.source) && !recovery.Matches(1, 5, 9, recovery.source) &&
          !recovery.Matches(1, 4, 10, recovery.source) &&
          !recovery.Matches(1, 4, 9, L"https://game.granbluefantasy.jp/#title"),
          "late login close/probe crossed view/navigation/popup/source identity");
      std::cout << "PASS bounded login resume identity and entry policy; browser NOT_STARTED\n";
    }
    {
      using gbrowser::webview::GameHistoryScript;
      Require(GameHistoryScript(L"https://game.granbluefantasy.jp/#raid_multi/1", false) &&
          GameHistoryScript(L"https://gbf.game.mbga.jp/#quest/supporter_raid/1/2", true),
          "trusted GBF routes lost actual session history navigation");
      for (const wchar_t* url : {L"https://game.granbluefantasy.jp/", L"https://connect.mobage.jp/#raid_multi/1",
           L"http://game.granbluefantasy.jp/#raid_multi/1", L"https://game.granbluefantasy.jp.evil.test/#raid_multi/1",
           L"https://game.granbluefantasy.jp/login#raid_multi/1", L"about:blank", L"chrome-extension://id/#raid_multi/1"})
        Require(!GameHistoryScript(url, false), "non-game/login document selected game session history");
      std::cout << "PASS GBF session history policy; login/HTTP/spoof/path/extension rejection; browser NOT_STARTED\n";
    }
    {
      using gbrowser::webview::NeedsHelperTargetNavigation;
      int bound = 0, navigations = 0; bool ready = false;
      for (int incoming : {42, 42, 42, 42}) {
        if (NeedsHelperTargetNavigation(bound, incoming, ready)) { ++navigations; ready = true; }
        bound = incoming;
      }
      Require(navigations == 1, "repeated game document loads reset Tarou panel");
      for (int incoming : {0, -1, 43}) {
        bool rejected = false;
        try { NeedsHelperTargetNavigation(bound, incoming, ready); } catch (const std::exception&) { rejected = true; }
        Require(rejected && navigations == 1, "changed target replaced the bound helper");
      }
      Require(NeedsHelperTargetNavigation(bound, bound, false), "missing helper cannot bind its existing target");
      std::cout << "PASS stable game target keeps existing Tarou panel through reload/battle; changed target rejected; no browser\n";
    }
    {
      using namespace gbrowser;
      using namespace gbrowser::webview;
      ProxyInitialization probe(1, 3, 1, "doc", "profile", 100, ProxyAction::Acgpower);
      Require(ProxyFeedback(probe, true, true) == ProxyUiResult::Waiting, "pending probe reported failure");
      probe.Query("1:begin", 3, 1, "doc", "profile", 101);
      probe.Query("1:different", 3, 1, "doc", "profile", 102);
      Require(ProxyFeedback(probe, true, true) == ProxyUiResult::RestoreSaved &&
          ProxyFeedback(probe, true, false) == ProxyUiResult::Failed &&
          ProxyFeedback(probe, false, true) == ProxyUiResult::Failed, "restore preflight confused with terminal failure");
      ProxyInitialization restored(2, 3, 1, "doc", "profile", 200, ProxyAction::Acgpower);
      restored.Query("2:begin", 3, 1, "doc", "profile", 201);
      restored.Query("2:apply", 3, 1, "doc", "profile", 202);
      restored.Query("2:matched", 3, 1, "doc", "profile", 203);
      Require(ProxyFeedback(restored, false, false) == ProxyUiResult::Matched && restored.observing(),
          "restored proxy did not stay ready");
      // A real disabled Win32 owner reproduces the modal-dialog condition,
      // without opening a browser or injecting input into the desktop.
      HWND owner = CreateWindowW(L"STATIC", L"focus check", 0, 0, 0, 0, 0, HWND_MESSAGE,
          nullptr, GetModuleHandleW(nullptr), nullptr);
      Require(owner != nullptr, "cannot create isolated focus owner");
      int calls = 0;
      const auto move = [&]() -> HRESULT { ++calls; return E_FAIL; };
      EnableWindow(owner, FALSE);
      Require(FocusEnabledHost(owner, false, move) == S_FALSE && calls == 0, "disabled owner received focus");
      EnableWindow(owner, TRUE);
      Require(FocusEnabledHost(owner, true, move) == S_FALSE && calls == 0, "closing owner received focus");
      Require(FocusEnabledHost(owner, false, move) == E_FAIL && calls == 1 && restored.observing(),
          "focus failure changed proxy readiness");
      DestroyWindow(owner);
      restored.Query("2:changed", 3, 1, "doc", "profile", 204);
      Require(ProxyFeedback(restored, true, true) == ProxyUiResult::Failed && !restored.observing(),
          "unknown observation silently restored or ignored");
      std::cout << "PASS saved-proxy intermediate UI, disabled owner focus, terminal/unknown failure; browser NOT_STARTED\n";
    }
    {
      using namespace gbrowser;
      using namespace gbrowser::webview;
      for (unsigned dpi : {96U, 144U, 192U}) {
        for (int width : {400, 600, 800, 1600}) for (size_t count : {size_t{1},size_t{2},size_t{8}}) {
          const auto toolbar = ToolbarGeometry(UiPixels(width, dpi), dpi, count);
          const std::array<Rect, 9> controls{toolbar.reload, toolbar.address, toolbar.copy, toolbar.tabs, toolbar.add, toolbar.profile,
              toolbar.minimize, toolbar.maximize, toolbar.close};
          int previous = 0;
          for (const auto& rect : controls) {
            Require(rect.width > 0 && rect.x >= previous && rect.x + rect.width <= UiPixels(width, dpi),
                "compact toolbar controls overlap or escape at minimum width/DPI");
            previous = rect.x + rect.width;
          }
          const int previous_address = (std::clamp)(toolbar.profile.x - UiPixels(142,dpi),UiPixels(100,dpi),UiPixels(236,dpi));
          Require(toolbar.address.width == MulDiv(previous_address,2,3), "layout1 address is not two-thirds of original width");
          const auto header = HeaderGeometry({20, 50, UiPixels(width, dpi), 300}, dpi);
          Require(header.title.x + header.title.width <= header.pin.x &&
              header.pin.x + header.pin.width <= header.close.x,
              "Tarou title can overwrite action buttons");
        }
        PaneState panes; panes.dual = true;
        const auto layout = ComputePaneLayout(UiPixels(1600, dpi), UiPixels(900, dpi), dpi, 40, panes, false);
        Require(layout.right.y + layout.right.height == UiPixels(900, dpi) - LeftChromeInset(dpi) &&
            layout.chrome.content.rail.y + layout.chrome.content.rail.height == layout.right.y + layout.right.height &&
            layout.chrome.content.rail.width == UiPixels(48, dpi), "layout 1 bottom trim or rail alignment changed");
        Require(layout.link.x + layout.link.width <= layout.left.x + layout.left.width,
            "one-shot button intrudes into primary game");
        Require(layout.left.x == UiPixels(6, dpi), "left gutter is not the requested two-thirds width");
        panes.dual = false; panes.game_width = 500;
        const auto single = ComputePaneLayout(UiPixels(1600, dpi), UiPixels(900, dpi), dpi, 40, panes, false);
        Require(single.right.x == LeftChromeInset(dpi) && single.right.width == UiPixels(500, dpi),
            "left gutter crops or resizes the saved game viewport");
      }
      for (unsigned dpi : {96U,144U,192U}) for (int room : {0,1,16,32,43,64,120,284,400}) {
        const IconTabLayout tabs(UiPixels(room,dpi),UiPixels(34,dpi),dpi,8);
        int previous = -1;
        for (size_t i=0;i<8;++i) {
          const auto r=tabs.Item(i);
          Require(r.x>=previous && r.x+r.width<=UiPixels(room,dpi), "overlapping icon tab escapes its strip");
          previous=r.x;
        }
        for (int x=0;x<UiPixels(room,dpi);++x) {
          const int hit=tabs.Hit(x,UiPixels(17,dpi));
          if (hit<0) continue;
          const auto r=tabs.Item(static_cast<size_t>(hit));
          Require(x>=r.x && x<r.x+r.width, "tab hit disagrees with painted rectangle");
          for (size_t i=static_cast<size_t>(hit)+1;i<8;++i) {
            const auto right=tabs.Item(i);
            Require(x<right.x || x>=right.x+right.width, "covered tab wins over the right-hand tab");
          }
        }
        Require(tabs.Hit(-1,0)==-1 && tabs.Hit(UiPixels(room,dpi),UiPixels(17,dpi))==-1,
            "tab hit accepts blank or out-of-bounds strip");
      }
      Require(FrameEdge({-2,-2}, {0,0,800,600}, 5, false) == HTTOPLEFT &&
          FrameEdge({799,599}, {0,0,800,600}, 5, false) == HTBOTTOMRIGHT &&
          FrameEdge({400,300}, {0,0,800,600}, 5, false) == HTCLIENT &&
          FrameEdge({0,0}, {0,0,800,600}, 5, true) == HTCLIENT, "custom frame edge regression");
      for (unsigned dpi : {96U,144U,192U}) for (int width : {600,800,1200,1800}) {
        const auto toolbar = BrowserToolbarGeometry(UiPixels(width,dpi),dpi,8);
        int end = toolbar.reload.x + toolbar.reload.width;
        Require(toolbar.address.x >= end && toolbar.address.width >= UiPixels(100,dpi),"layout2 address squeezed");
        end=toolbar.address.x+toolbar.address.width;
        Require(toolbar.copy.x>=end,"layout2 copy overlaps address"); end=toolbar.copy.x+toolbar.copy.width;
        for (const auto r : toolbar.functions) { Require(r.x>=end && r.width>0,"layout2 function overlap"); end=r.x+r.width; }
        Require(end<=UiPixels(width,dpi) && toolbar.tabs.x+toolbar.tabs.width<=toolbar.add.x &&
            toolbar.add.x+toolbar.add.width<toolbar.minimize.x,"layout2 top rows overflow");
        PaneState state; state.game_width=200;
        for(bool dual : {false,true}) {
          state.dual=dual;
          const auto classic=ComputePaneLayout(UiPixels(width,dpi),UiPixels(800,dpi),dpi,40,state,false,true);
          Require(classic.right.y==UiPixels(86,dpi) && classic.chrome.content.rail.width==UiPixels(6,dpi) &&
              classic.right.width==UiPixels(200,dpi),"layout2 content/rail/viewport mismatch");
        }
      }
      for(bool classic : {false,true}) Require(DecodeBrowserLayout(EncodeBrowserLayout(classic))==classic,"layout choice roundtrip failed");
      bool invalid_layout=false; try { DecodeBrowserLayout("GBWV-LAYOUT-1\n3\n"); } catch (...) {invalid_layout=true;}
      Require(invalid_layout,"unknown layout choice accepted");
      for (bool zoomed : {false, true}) {
        RECT simple{-1200,50,-400,650}; NCCALCSIZE_PARAMS full{}; full.rgrc[0] = simple;
        const RECT work{-1920,40,0,1080};
        FrameClientRect(reinterpret_cast<LPARAM>(&simple), false, zoomed, work, 8);
        FrameClientRect(reinterpret_cast<LPARAM>(&full), true, zoomed, work, 8);
        Require(EqualRect(&simple, &full.rgrc[0]) &&
            (zoomed ? EqualRect(&simple, &work) : simple.left == -1192 && simple.top == 50 && simple.bottom == 642),
            "NCCALCSIZE forms disagree or native resize borders/work area lost");
      }
      std::cout << "PASS layout 1 toolbar/header/rail/arrow and resize edges at 96/144/192 DPI; no browser\n";
    }
    for (const bool hidden : {false, true})
      Require(gbrowser::webview::DecodeHiddenLoading(gbrowser::webview::EncodeHiddenLoading(hidden)) == hidden,
          "loading display preference roundtrip mismatch");
    for (const auto* invalid : {"", "STARRYFISH-LOADING-1\nhidden", "STARRYFISH-LOADING-2\nhidden\n",
         "STARRYFISH-LOADING-1\nvisible\nextra", "STARRYFISH-LOADING-1\nHIDDEN\n"}) {
      bool rejected = false;
      try { gbrowser::webview::DecodeHiddenLoading(invalid); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "invalid loading display preference accepted");
    }
    for (const bool dark : {false, true})
      Require(gbrowser::webview::DecodeDarkAppearance(gbrowser::webview::EncodeDarkAppearance(dark)) == dark,
          "appearance roundtrip mismatch");
    for (const auto* invalid : {"", "GBWV-APPEARANCE-1\ndark", "GBWV-APPEARANCE-2\nlight\n", "GBWV-APPEARANCE-1\nLIGHT\n"}) {
      bool rejected = false;
      try { gbrowser::webview::DecodeDarkAppearance(invalid); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "invalid appearance accepted");
    }
    {
      using namespace gbrowser;
      WNDCLASSW fixture{}; fixture.lpfnWndProc = PaneTeardownCheck;
      fixture.hInstance = GetModuleHandleW(nullptr); fixture.lpszClassName = L"GbrowserIsolatedPaneTeardownCheck";
      Require(RegisterClassW(&fixture) != 0, "cannot create private teardown fixture");
      int marker = 1;
      const auto pane = CreateWindowW(fixture.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
          nullptr, fixture.hInstance, &marker);
      Require(pane != nullptr, "cannot create invisible message-only pane");
      webview::DestroyPaneWindow(pane);
      Require(pane_destroyed == 1 && pane_host_dispatch == 0 && !IsWindow(pane),
          "owned pane destruction reentered host callback dispatch");
      Require(UnregisterClassW(fixture.lpszClassName, fixture.hInstance) != FALSE, "fixture class cleanup failed");
      std::cout << "PASS invisible Win32 pane teardown detaches host dispatch; browser NOT_STARTED\n";
      for (unsigned flags = 0; flags < 32; ++flags) {
        const webview::PaneState panes{(flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0,
            (flags & 8) != 0, (flags & 16) != 0};
        const auto bytes = webview::EncodePaneState(panes);
        Require(webview::EncodePaneState(webview::DecodePaneState(bytes)) == bytes, "pane restart lost a switch");
        for (const auto dpi : {96U, 144U, 192U}) for (const auto width : {850, 1800, 3200}) {
          const auto geometry = webview::ComputePaneLayout(width, 900, dpi, 40, panes, false);
          Require(geometry.right.width > 0 && geometry.right.x + geometry.right.width <= geometry.chrome.content.rail.x,
              "game pane overlaps the function rail");
          if (panes.dual) Require(geometry.left.width == geometry.right.width &&
              geometry.right.x - geometry.left.x - geometry.left.width == UiPixels(6, dpi) &&
              geometry.link.x >= geometry.left.x && geometry.link.x + geometry.link.width <= geometry.left.x + geometry.left.width,
              "dual games lost exact equal width/narrow gap or left-only arrow hit area");
          if (panes.tarou && !panes.tarou_detached) Require(geometry.tarou.x == geometry.right.x + geometry.right.width +
              geometry.divider.width && geometry.tarou.width > 0,
              "Tarou no longer follows the right game");
          if (panes.arcarum) Require(geometry.arcarum.x >= geometry.right.x + geometry.right.width &&
              geometry.arcarum.x + geometry.arcarum.width == geometry.chrome.content.rail.x,
              "docked Arcarum overlaps games or loses body width");
          const auto detached = webview::ComputePaneLayout(width, 900, dpi, 40, panes, true);
          Require(detached.arcarum.width == 0, "detached helper still reserves dock space");
        }
      }
      const auto migrated = webview::DecodePaneState("GBWV-PANES-1\n1 1 1 1 1\n", 40);
      Require(migrated.group_percent == 53 && migrated.tarou_share == 2800 && !migrated.tarou_detached,
          "legacy one-game weights were not converted to nested group/inner ratios");
      webview::PaneState split{}; split.dual = true; split.arcarum = true; split.tarou_share = 2000;
      const auto narrow_tarou = webview::ComputePaneLayout(2000, 900, 144, 50, split, false);
      split.tarou_share = 7000;
      const auto wide_tarou = webview::ComputePaneLayout(2000, 900, 144, 50, split, false);
      Require(narrow_tarou.right.width == wide_tarou.right.width && narrow_tarou.right.x == wide_tarou.right.x &&
          narrow_tarou.tarou.width < wide_tarou.tarou.width && narrow_tarou.arcarum.width > wide_tarou.arcarum.width,
          "inner split changed game-group position/size");
      const auto more_helpers = webview::ComputePaneLayout(2000, 900, 144, 65, split, false);
      Require(more_helpers.right.width < wide_tarou.right.width && more_helpers.left.width == more_helpers.right.width &&
          more_helpers.tarou.width > wide_tarou.tarou.width && more_helpers.arcarum.width > wide_tarou.arcarum.width,
          "outer split did not move both game and both helper widths together");
      const RECT outer{900, 50, 1800, 800}, work{0, 0, 3000, 2000};
      const auto opened = webview::PrimaryAnchoredResize(outer, 406, 406, work, 400);
      Require(opened && opened->left + 406 == outer.left && opened->right == outer.right,
          "dual opening moved primary screen position or expanded right edge");
      const auto closed = webview::PrimaryAnchoredResize(*opened, -406, -406, work, 400);
      Require(closed && closed->left == outer.left && closed->right == outer.right,
          "dual closing did not retract from the left");
      Require(!webview::PrimaryAnchoredResize(RECT{50, 0, 950, 800}, 406, 406, work, 400),
          "insufficient left work area silently moved primary game");
      for (const auto* bad : {"GBWV-PANES-2\n1 0 0 1 0\n", "GBWV-PANES-1\n1 2 0 1 0\n",
          "GBWV-PANES-1\n1 0 0 1 0\nextra", "GBWV-PANES-1\n1 0 0 1\n"}) {
        bool rejected = false; try { webview::DecodePaneState(bad); } catch (...) { rejected = true; }
        Require(rejected, "malformed pane state silently changed settings");
      }
      Require(webview::GameCommandTargets(0, 4, true) == std::array<size_t, 2>{4, SIZE_MAX}, "left command hit right");
      Require(webview::GameCommandTargets(0, 1, true) == std::array<size_t, 2>{0, SIZE_MAX}, "Tarou command did not use right");
      Require(webview::GameCommandTargets(6, 0, true) == std::array<size_t, 2>{6, SIZE_MAX}, "link included an ordinary tab");
      std::cout << "PASS pane switches/restart/equal games/dock-detach geometry and navigation routing; browser NOT_STARTED\n";
      const std::wstring home = L"https://game.granbluefantasy.jp/#mypage", shop = L"https://game.granbluefantasy.jp/#shop";
      Require(webview::PlanGameCopy(home, shop) == webview::GameCopy::Navigate, "arrow did not copy primary destination");
      Require(webview::PlanGameCopy(home, home) == webview::GameCopy::Reload, "equal URL arrow did not request a reload");
      for (const auto* bad : {L"https://connect.mobage.jp/#login", L"https://game.granbluefantasy.jp/?token=private#x",
          L"https://game.granbluefantasy.jp.evil/#shop", L"https://game.granbluefantasy.jp@evil/#shop", L"about:blank"})
        Require(webview::PlanGameCopy(bad, home) == webview::GameCopy::None, "arrow copied login or non-game URL");
      for (unsigned dpi : {96U, 144U, 192U}) for (bool dual : {false, true}) {
        webview::PaneState fixed; fixed.dual = dual; fixed.arcarum = true;
        fixed.game_width = 401; fixed.game_dpi = 96;
        const auto before = webview::ComputePaneLayout(2400, 900, dpi, 50, fixed, false);
        const auto after = webview::ComputePaneLayout(2500, 900, dpi, 50, fixed, false);
        Require(before.right.width == after.right.width && before.right.x == after.right.x &&
            (!dual || before.left.width == after.left.width), "outer resize changed game geometry");
        Require(after.tarou.width + after.arcarum.width == before.tarou.width + before.arcarum.width + 100,
            "outer width delta did not belong entirely to helpers");
        fixed.tarou = false; fixed.arcarum = false;
        const auto hidden = webview::ComputePaneLayout(2500, 900, dpi, 50, fixed, false);
        Require(hidden.right.width == before.right.width, "hidden helpers enlarged games");
        const auto restored = webview::DecodePaneState(webview::EncodePaneState(fixed));
        Require(restored.game_width == 401 && restored.game_dpi == 96, "saved exact width/reference DPI lost");
      }
      const Rect visible_game{0, 0, 500, 800};
      const auto child = webview::GameViewBounds(visible_game, 70);
      Require(child.right - child.left == 570 && child.left == -70 && child.right == visible_game.width,
          "Moba child bounds removed the hidden width twice");
      int retained_inset = 70;
      for (int navigation = 0; navigation < 3; ++navigation) {
        retained_inset = webview::RetainedGameInset(retained_inset, "https://game.granbluefantasy.jp/#mypage");
        const auto reloaded = webview::GameViewBounds(visible_game, retained_inset);
        Require(EqualRect(&child, &reloaded), "game reload transiently resized its renderer");
      }
      for (const auto source : {"about:blank", "https://connect.mobage.jp/login", "https://game.granbluefantasy.jp.evil/",
                               "https://example.org/", "https://game.granbluefantasy.jp@evil/"}) {
        const auto external = webview::GameViewBounds(visible_game, webview::RetainedGameInset(retained_inset, source));
        Require(external.left == 0 && external.right == 500, "non-game page inherited a clipped viewport");
      }
      const auto old_link = webview::DecodePaneState("GBWV-PANES-2\n1 1 1 1 1 0 50 33\n");
      Require(old_link.dual && old_link.game_width == 0 && old_link.tarou_share == 3300 &&
          webview::EncodePaneState(old_link).rfind("GBWV-PANES-5\n", 0) == 0, "old toggle was not retired through migration");
      const auto old_width = webview::DecodePaneState("GBWV-PANES-3\n0 1 0 0 0 50 3300 401 144\n");
      Require(old_width.game_width == 401 && old_width.game_dpi == 144 && old_width.tarou_width == 0,
          "existing pane width/preferences lost during migration");
      auto saved_helper = old_width; saved_helper.tarou_width = 537; saved_helper.tarou_dpi = 144;
      const auto restored_helper = webview::DecodePaneState(webview::EncodePaneState(saved_helper));
      Require(restored_helper.tarou_width == 537 && restored_helper.tarou_dpi == 144,
          "closed Tarou width not retained across restart");
      const auto old_sidebar = webview::DecodePaneState("GBWV-PANES-4\n0 1 0 0 0 50 3300 401 144 537 144\n");
      Require(old_sidebar.secondary_menu_hidden && old_sidebar.secondary_menu_width == 0 && old_sidebar.tarou_width == 537,
          "previous layout lost width or guessed an unmeasured menu crop");
      for (unsigned dpi : {96U, 144U, 192U}) {
        webview::PaneState narrow; narrow.dual = true; narrow.game_width = 500;
        const auto before = webview::ComputePaneLayout(3200, 900, dpi, 50, narrow, false);
        narrow.secondary_menu_width = 64;
        const int cut = webview::SecondaryMenuCut(narrow, dpi, before.right.width);
        const auto after = webview::ComputePaneLayout(3200 - cut, 900, dpi, 50, narrow, false);
        Require(after.right.width == before.right.width && after.left.width == before.left.width - cut &&
            after.tarou.width == before.tarou.width && after.right.x == before.right.x - cut,
            "secondary menu crop changed the primary or helper instead of just the secondary");
        const auto moved = webview::PrimaryAnchoredResize({100, 20, 3300, 920}, -cut, -cut, {0, 0, 5000, 2000}, 400);
        Require(moved && moved->left + after.right.x == 100 + before.right.x && moved->right == 3300,
            "secondary menu shrink failed to anchor primary and outer right edge");
        const auto restored_menu = webview::DecodePaneState(webview::EncodePaneState(narrow));
        Require(restored_menu.secondary_menu_hidden && restored_menu.secondary_menu_width == 64,
            "secondary menu choice/width lost across restart");
        narrow.secondary_menu_width = 1000;
        for (int small : {120, 350, 800}) {
          const auto tiny = webview::ComputePaneLayout(small, 900, dpi, 50, narrow, false);
          Require(tiny.right.x + tiny.right.width <= tiny.chrome.content.rail.x,
              "bounded secondary crop overflowed a smaller monitor");
        }
        narrow.secondary_menu_hidden = false;
        const auto visible = webview::ComputePaneLayout(3200, 900, dpi, 50, narrow, false);
        Require(visible.left.width == visible.right.width, "restoring menu did not restore full secondary width");
      }
      for (size_t index = 0; index < webview::TabSlots::kViewCount; ++index)
        Require(webview::GameSurface(index) == (index == 0 || index == 4 || (index >= 6 && index < 13)),
            "display adapter included a helper/login or omitted an ordinary game tab");
      for (unsigned dpi : {96U, 144U, 192U}) for (bool dual : {false, true}) for (bool arcarum : {false, true}) {
        webview::PaneState state; state.dual = dual; state.arcarum = arcarum;
        state.game_width = 401;
        const auto before = webview::ComputePaneLayout(3200, 1000, dpi, 50, state, false);
        const int delta = webview::TarouWidthChange(before, true, false, 0, state.tarou_share, dpi);
        state.tarou = false;
        const auto tarou_closed = webview::ComputePaneLayout(3200 + delta, 1000, dpi, 50, state, false);
        Require(tarou_closed.right.width == before.right.width && tarou_closed.right.x == before.right.x &&
            tarou_closed.arcarum.width == before.arcarum.width, "Tarou close changed a game or other helper width");
        if (!arcarum) Require(tarou_closed.chrome.content.rail.x == tarou_closed.right.x + tarou_closed.right.width,
            "Tarou close left an empty helper region");
        const int reopen = webview::TarouWidthChange(tarou_closed, false, true, before.tarou.width, state.tarou_share, dpi);
        Require(abs(reopen + delta) <= 3, "Tarou reopen failed to restore width/ratio");
        Require(webview::TarouWidthChange(tarou_closed, false, false, 600, state.tarou_share, dpi) == 0,
            "floating Tarou visibility resized the main window");
      }
      std::cout << "PASS one-shot arrow/same-URL reload scope/fixed game widths/DPI/retired link migration; browser NOT_STARTED\n";
      const std::wstring name = L"gbrowser-bookmark-middle-01234567-89ab-4def-8123-456789abcdef";
      Require(webview::IsBookmarkWindowName(name), "valid bookmark classification rejected");
      for (const auto& bad : {name + L"x", std::wstring(L"_blank"), name.substr(0, name.size() - 1),
          std::wstring(L"gbrowser-bookmark-middle-01234567_89ab-4def-8123-456789abcdef"),
          std::wstring(L"gbrowser-bookmark-middle-01234567-89ab-4def-8123-456789abcdeg")})
        Require(!webview::IsBookmarkWindowName(bad), "unbounded/unknown bookmark window classification accepted");
      const std::string source = "https://game.granbluefantasy.jp/#mypage";
      const std::string target = "https://game.granbluefantasy.jp/#quest";
      Require(webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, source, target, true, true),
          "game bookmark URL rejected");
      Require(webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, source, "https://example.test/path", true, true),
          "normal custom bookmark URL rejected");
      for (const auto mode : {RunMode::Offline, RunMode::Gbf})
        Require(!webview::IsBookmarkNavigation(mode, source, target, true, true), "bookmark opened outside game mode");
      Require(!webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, source, target, false, true) &&
          !webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, source, target, true, false),
          "script/child-frame bookmark request accepted");
      for (const auto& bad : {std::string("https://game.granbluefantasy.jp.evil.test/"),
          std::string("https://example.test/"), std::string("about:blank")})
        Require(!webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, bad, target, true, true),
            "unrelated bookmark source accepted");
      for (const auto& bad : {std::string("javascript:alert(1)"), std::string("file:///C:/test"),
          std::string("chrome-extension://abcdefghijklmnopabcdefghijklmnop/popup.html"),
          std::string("https://connect.mobage.jp/login"), std::string("https://example.test/") + std::string(8192, 'a')})
        Require(!webview::IsBookmarkNavigation(RunMode::GbfOpenNetwork, source, bad, true, true),
            "non-web/auth/oversized bookmark target accepted");
      std::cout << "PASS bookmark popup classification/gesture/main-frame/source/URL boundary; browser NOT_STARTED\n";
    }
    {
      gbrowser::webview::TabPointerClose gesture;
      using Ticket = gbrowser::webview::TabSlots::Ticket;
      gesture.Down(Ticket{4, 1});
      Require(!gesture.Up(Ticket{4, 2}), "middle click closed a replacement controller");
      gesture.Down(Ticket{4, 1});
      Require(!gesture.Up(Ticket{5, 1}), "drag to another tab closed it");
      gesture.Down(Ticket{4, 1}); gesture.Cancel();
      Require(!gesture.Up(Ticket{4, 1}), "cancelled pointer gesture closed a tab");
      gesture.Down(Ticket{5, 7});
      const auto closed = gesture.Up(Ticket{5, 7});
      Require(closed && closed->index == 5 && closed->generation == 7 && !gesture.Up(Ticket{5, 7}),
          "middle click did not consume exactly one stable ticket");
      for (const auto dpi : {96U, 144U, 192U}) for (const auto percent : {1U, 20U, 50U, 80U, 99U}) {
        const auto layout = gbrowser::webview::HostWindowLayout(1800, 1000, true, dpi, percent);
        Require(layout.content.browser.width >= gbrowser::UiPixels(260, dpi) &&
            layout.content.tarou.width >= gbrowser::UiPixels(260, dpi) &&
            layout.content.browser.width + layout.content.tarou.width == layout.content.rail.x,
            "native split violated usable bounds or lost body width");
        Require(layout.add.width == 0 && layout.close.width == 0 &&
            layout.tabs.x + layout.tabs.width == 1800 - gbrowser::UiPixels(8, dpi),
            "removed tab buttons retained space");
      }
      const auto hidden = gbrowser::webview::HostWindowLayout(850, 520, false, 96, 75);
      Require(hidden.content.tarou.width == 0 && hidden.tarou_divider.width == 0 &&
          hidden.content.browser.width == hidden.content.rail.x, "hidden panel retained a split gutter");
      std::cout << "PASS middle pointer cancellation/stale ticket and native split geometry; browser NOT_STARTED\n";
      gbrowser::webview::SplitPointerGesture split;
      Require(split.Begin(500, 500, 260, 740) && !split.Begin(500, 500, 260, 740), "overlapping split gesture accepted");
      Require(split.Move(10000) == 740 && split.Move(-10000) == 260, "split escaped its minimum pane widths");
      Require(split.Cancel() == 500 && split.Move(700) == 500, "cancelled split retained an unfinished value");
      Require(split.Begin(500, 500, 260, 1700), "pixel drag did not begin");
      for (int delta = 0; delta < 40; ++delta)
        Require(split.Move(500 + delta) == 500 + delta, "drag quantized small moves into percentage jumps");
      split.Finish();
      const auto visible = gbrowser::webview::VisiblePlacement({-5000, -5000, -4000, -4000},
          {-1920, 0, 0, 1080}, {-1880, 40, 0, 1080}, 1400, 850);
      Require(visible.left == -1920 && visible.top == 0 && visible.right == -520 && visible.bottom == 850,
          "detached-monitor restoration mixed screen and workspace coordinates");
      const auto edge = gbrowser::webview::VisiblePlacement({-10, 1000, 1000, 2000},
          {-1920, 0, 0, 1080}, {-1880, 40, 0, 1080}, 1400, 850);
      Require(edge.right == -40 && edge.bottom == 1040, "partial off-screen restoration retained overhang");
      const gbrowser::webview::WindowState layout{-2000, 40, 1400, 850, 144, 70, true};
      const auto bytes = gbrowser::webview::EncodeWindowState(layout);
      const auto decoded = gbrowser::webview::DecodeWindowState(bytes);
      Require(gbrowser::webview::EncodeWindowState(decoded) == bytes && decoded.x == -2000 && decoded.maximized,
          "layout lost a secondary-screen coordinate or maximize state");
      for (const auto& bad : {bytes + "extra", std::string("GBWV-LAYOUT-2\n"), std::string("GBWV-LAYOUT-1\n0 0 1200 800 96 0 0\n")}) {
        bool rejected = false;
        try { gbrowser::webview::DecodeWindowState(bad); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "unknown/malformed layout would be silently rewritten");
      }
    }
    {
      const auto result = gbrowser::webview::ParseTargetReply("\"7:2147483647:0:2147483647\"", 7);
      Require(result.id == 2147483647 && result.reason == 0, "full positive tab ID rejected");
      Require(gbrowser::webview::ParseTargetReply("\"7:0:4:-1\"", 7).sample == -1,
          "numeric rejection diagnostics lost");
      for (const auto* value : {"null", "\"6:1:0:1\"", "\"7:2147483648:0:2147483648\"",
           "\"7:-1:0:-1\"", "\"7:0:0:0\"", "\"7:1:3:0\"", "\"7:0:5:0\"",
           "\"7:1:0:2\"", "\"7:0:3:1\"", "\"7:1:0:1:0\"", "\"7:1:0\"", "\"7:1:0:1\\n\""}) {
        bool rejected = false;
        try { gbrowser::webview::ParseTargetReply(value, 7); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "invalid/stale target reply accepted");
      }
      std::cout << "PASS real target reply parser: signed-32-bit IDs, overflow/stale/inconsistent rejection; browser NOT_STARTED\n";
    }
    {
      gbrowser::webview::TabSlots tabs;
      const auto first = *tabs.Reserve();
      Require(!tabs.Cancel(first), "pending cancelled tab released before completion");
      std::array<gbrowser::webview::TabSlots::Ticket, 6> other{};
      for (auto& ticket : other) ticket = *tabs.Reserve();
      Require(!tabs.Reserve(), "cancelled creation did not retain its bounded slot");
      std::array<gbrowser::webview::TabSlots::Ticket, 4> helpers{};
      for (auto& ticket : helpers) {
        ticket = *tabs.ReserveAuxiliary();
        Require(ticket.index >= gbrowser::webview::TabSlots::kAuxFirst, "helper reserved an ordinary tab");
      }
      Require(!tabs.ReserveAuxiliary() && !tabs.Reserve(), "helper/ordinary capacity leaked across roles");
      Require(!tabs.Cancel(helpers[0]) && !tabs.ReserveAuxiliary(), "pending helper released before actual completion");
      Require(!tabs.Complete(helpers[0]), "cancelled helper controller published");
      tabs.Release(helpers[0]);
      const auto helper = *tabs.ReserveAuxiliary();
      Require(helper.index == helpers[0].index && helper.generation != helpers[0].generation,
          "helper replacement reused stale identity");
      tabs.Release(helpers[0]); Require(tabs.Current(helper), "stale helper release cleared replacement");
      Require(!tabs.Complete(first), "cancelled controller was published");
      tabs.Release(first);
      const auto replacement = *tabs.Reserve();
      Require(replacement.index == first.index && replacement.generation != first.generation &&
          !tabs.Current(first) && !tabs.Complete(first), "late completion matched replacement tab");
      tabs.Release(first);
      Require(tabs.Current(replacement) && tabs.Complete(replacement) && tabs.Cancel(replacement),
          "new tab identity lost to stale release");
      tabs.Release(replacement); Require(!tabs.Current(replacement), "completed close retained a live tab");
      Require(!tabs.Current({0, 0}) && !tabs.Current({3, 1}) && !tabs.Current({4, 1}) && !tabs.Current({5, 1}) && !tabs.Current({99, 1}),
          "fixed game/helper/proxy/login roles became ordinary tab slots");
      std::cout << "PASS optional tab bound/cancel-before-completion/stale identity rejection; browser NOT_STARTED\n";
    }
    const auto product = gbrowser::webview::NativeLaunchPlan(L"\"C:\\Program Files\\Gbrowser\\StarryFish.exe\"");
    for (const auto mode : {gbrowser::RunMode::Offline, gbrowser::RunMode::GbfOpenNetwork})
      for (bool closing : {false, true}) for (bool failed : {false, true})
        Require(gbrowser::webview::BrowserNetworkAvailable(mode, closing, failed) ==
            (mode == gbrowser::RunMode::GbfOpenNetwork && !closing && !failed),
            "normal startup network/offline/shutdown boundary mismatch");
    const auto offline = gbrowser::webview::NativeLaunchPlan(L"StarryFish.exe --validation-offline");
    const auto network = gbrowser::webview::NativeLaunchPlan(L"StarryFish.exe --validation-gbf-open-network");
    Require(product && product->network_mode == gbrowser::RunMode::GbfOpenNetwork &&
        std::wstring_view(product->data_leaf) == L"UserData" && offline &&
        offline->network_mode == gbrowser::RunMode::Offline && std::wstring_view(offline->data_leaf) == L"Validation" &&
        network && std::wstring_view(network->data_leaf) == L"GbfValidation", "startup/data separation mismatch");
    for (const auto* argument : {L"--validation-gbf", L"--Validation-offline", L"--validation-offline=1",
         L"--validation-offline --validation-offline", L"--validation-offline --validation-gbf-open-network",
         L"--validation-gbf-open-network https://game.granbluefantasy.jp/", L"--unknown"})
      Require(!gbrowser::webview::NativeLaunchPlan(std::wstring(L"StarryFish.exe ") + argument),
          "invalid/ambiguous/fixed-site launch accepted");
    Require(gbrowser::webview::SupportedRuntimeVersion(L"154.0.3719.0") &&
        gbrowser::webview::SupportedRuntimeVersion(L"155.1.0.1"), "supported runtime rejected");
    for (const auto* version : {L"153.9.9999.0", L"", L"154.0", L"154.0.1.0 dev", L"154..1.0",
         L"154.0.1.0.0", L"154.0.2147483648.0", L"+154.0.1.0"})
      Require(!gbrowser::webview::SupportedRuntimeVersion(version), "invalid/old/preview runtime accepted");
    std::cout << "PASS strict product/diagnostic startup separation and stable runtime baseline; browser NOT_STARTED\n";
    const auto root = std::filesystem::path(argv[1]);
    gbrowser::RequireSafePath(root);
    Require(root.is_absolute() && std::filesystem::is_directory(root) && std::filesystem::is_empty(root),
            "must use a new empty temporary directory");
    RunResourceCacheChecks(root);
    {
      const auto path = root / L"layout.state";
      gbrowser::webview::UserStateFile state(path);
      const auto bytes = gbrowser::webview::EncodeWindowState({});
      state.Save(bytes); state.Save(bytes);
      const auto changed = gbrowser::webview::EncodeWindowState({10, 20, 1300, 900, 96, 60, false});
      state.Save(changed);
      gbrowser::webview::UserStateFile readback(path);
      Require(readback.bytes() && *readback.bytes() == changed, "atomic state update did not publish complete bytes");
      { std::ofstream different(path); different << "unexpected user replacement"; }
      bool rejected = false;
      try { state.Save(bytes); } catch (const std::exception&) { rejected = true; }
      Require(rejected && *gbrowser::webview::UserStateFile(path).bytes() == "unexpected user replacement",
          "changed user state was overwritten");
      std::filesystem::create_hard_link(path, root / L"layout-link.state");
      rejected = false;
      try { gbrowser::webview::UserStateFile linked(path); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "linked settings file accepted");
      std::cout << "PASS bounded atomic user state, unexpected replacement and hardlink rejection; browser NOT_STARTED\n";
    }
    {
      using namespace gbrowser::webview;
      const auto installation = root / L"Profiles"; std::filesystem::create_directory(installation);
      ProfileCatalogue profiles(installation, L"UserData");
      Require(profiles.state().selected == "default" && ProfileDataPath(installation, L"UserData", "default") == installation / L"UserData",
          "existing default UDF would migrate");
      bool rejected = false;
      try { profiles.Create(L""); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "empty profile name accepted");
      const auto id = profiles.Create(L"测试 / 非路径名称");
      Require(gbrowser::IsProfileId(id) && profiles.state().selected == "default" &&
          ProfileDataPath(installation, L"UserData", id) == installation / L"Profiles-UserData" / gbrowser::Utf8ToWide(id),
          "new profile selected early or accepted a display name as path");
      Require(!std::filesystem::exists(ProfileDataPath(installation, L"UserData", id)), "creation read or copied browser data");
      profiles.Activate(id); profiles.Rename(id, L"测试资料");
      profiles.Retire("default", id);
      Require(profiles.Find("default", true).retired && profiles.state().selected == id,
          "retirement changed active identity");
      rejected = false;
      try { profiles.Retire(id, id); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "active profile could be retired");
      profiles.Restore("default");
      ProfileCatalogue readback(installation, L"UserData");
      Require(readback.state().selected == id && readback.Find(id).name == L"测试资料" && !readback.Find("default").retired,
          "profile metadata/recycle state did not survive restart");
      const auto bytes = EncodeProfiles(readback.state());
      Require(EncodeProfiles(DecodeProfiles(bytes)) == bytes, "profile canonical roundtrip failed");
      for (const auto& bad : {bytes + "extra", std::string("GBWV-PROFILES-1\ndefault\n../bad 0 61\n"),
          std::string("GBWV-PROFILES-1\ndefault\ndefault 1 61\n"),
          std::string("GBWV-PROFILES-1\ndefault\ndefault 0 61\ndefault 0 62\n")}) {
        rejected = false; try { DecodeProfiles(bad); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "bad profile metadata/path or duplicate identity accepted");
      }
      rejected = false;
      try { ProfileDataPath(installation, L"UserData", "../bad"); } catch (const std::exception&) { rejected = true; }
      Require(rejected && ProfileCatalogue(installation, L"Validation").state().selected == "default",
          "mode isolation or derived path validation failed");
      std::cout << "PASS profile path/default preservation, activation, recycle/restart and metadata rejection; browser NOT_STARTED\n";
    }
    {
      using namespace gbrowser::webview;
      Require(DecodeInstallChoice("GBWV-INSTALL-1\nacgpower\n") == gbrowser::ProxyAction::Acgpower &&
          DecodeInstallChoice("GBWV-INSTALL-1\nisland\n") == gbrowser::ProxyAction::Island &&
          DecodeInstallChoice("GBWV-INSTALL-1\ndirect\n") == gbrowser::ProxyAction::Direct, "installer choice decoding");
      bool invalid_install_choice = false;
      try { DecodeInstallChoice("GBWV-INSTALL-1\nunknown\n"); } catch (const std::exception&) { invalid_install_choice = true; }
      Require(invalid_install_choice, "invalid installer choice accepted");
      const auto data = root / L"ProxyPreference"; std::filesystem::create_directory(data);
      ProxyPreference state(data); Require(!state.state().choice && !state.state().uncertain, "fresh profile selected a proxy");
      state.BeginWrite();
      Require(ProxyPreference(data).state().uncertain, "interrupted write could be replayed after restart");
      bool rejected = false;
      try { state.BeginWrite(); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "unknown write was resubmitted");
      state.BeginWrite(true);
      Require(ProxyPreference(data).state().uncertain && !ProxyPreference(data).state().choice,
          "explicit new selection cleared an unresolved write before readback");
      rejected = false;
      try { ProxyPreference(data).BeginWrite(); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "manual recovery granted automatic restart replay");
      state.Matched(gbrowser::ProxyAction::Acgpower);
      const auto saved = ProxyPreference(data).state();
      Require(saved.choice == gbrowser::ProxyAction::Acgpower && !saved.uncertain, "matched choice was not retained");
      state.BeginWrite(); state.NoWrite();
      Require(!state.state().uncertain && state.state().choice == saved.choice, "known no-write erased the successful choice");
      const std::string custom="自用 \"节点\" & 💙";
      state.Matched(gbrowser::ProxyAction::Custom,custom);
      Require(ProxyPreference(data).state().custom_name==custom && ProxyPreference(data).state().choice==gbrowser::ProxyAction::Custom,
          "custom profile selection was not retained");
      state.BeginWrite();
      Require(ProxyPreference(data).state().uncertain && ProxyPreference(data).state().custom_name==custom,
          "unknown custom submission lost its selected name");
      state.NoWrite();
      const auto menu=DecodeProxyProfileMenu("SFPM1|"+ProxyProfileHex(custom)+"|646972656374,"+ProxyProfileHex(custom));
      Require(menu.current==custom && menu.names.size()==2 && menu.names[1]==custom,"custom menu names roundtrip failed");
      for (const auto* bad:{"SFPM1||646972656374,646972656374","SFPM1||00","SFPM1|ff|646972656374","SFPM1||646972656374,",
          "SFPM1|61|646972656374","SFPM1||zz"}) {
        rejected=false;try {DecodeProxyProfileMenu(bad);} catch (...) {rejected=true;}
        Require(rejected,"invalid custom proxy menu accepted");
      }
      for (const auto* bad : {"GBWV-PROXY-1\nacgpower 2\n", "GBWV-PROXY-1\ninitialize 0\n", "GBWV-PROXY-1\nnone 0\nextra"}) {
        rejected = false; try { DecodeProxyPreference(bad); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "malformed proxy record accepted as authority");
      }
      std::cout << "PASS per-UDF proxy choice, unknown submission guard and fresh readback record; browser NOT_STARTED\n";
    }
    {
      const auto installation = root / L"AppUpdate"; std::filesystem::create_directory(installation);
      const auto marker = installation / L"application-update.pending";
      gbrowser::webview::RequireCompleteApplicationUpdate(installation);
      { std::ofstream output(marker); output << "own synthetic pending update"; }
      bool rejected = false;
      try { gbrowser::webview::RequireCompleteApplicationUpdate(installation); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "pending app update permitted native environment startup");
      std::filesystem::remove(marker); std::filesystem::create_directory(marker);
      rejected = false;
      try { gbrowser::webview::RequireCompleteApplicationUpdate(installation); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "directory update gate permitted native startup");
      std::filesystem::remove(marker); gbrowser::webview::RequireCompleteApplicationUpdate(installation);
      std::cout << "PASS shared native application startup gate: missing/file/directory; browser NOT_STARTED\n";
    }
    {
      gbrowser::webview::DirectoryPins pins;
      const auto created = root / L"PinnedAncestry" / L"NewLeaf";
      pins.CreateTree(created);
      Require(std::filesystem::is_directory(created), "verified new ancestry not created");
      auto lower = created.native(); CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
      const auto normalized = pins.Tree(lower);
      Require(normalized == created && gbrowser::UnpackedExtensionId(normalized) == gbrowser::UnpackedExtensionId(created),
          "launcher casing changed the verified Chromium extension path identity");
      bool invalid = false;
      try { pins.CreateTree(L"relative\\state"); } catch (const std::exception&) { invalid = true; }
      Require(invalid, "relative new state directory accepted");
    }
    {
      const auto fresh = root / L"ZeroSetup"; std::filesystem::create_directory(fresh);
      gbrowser::webview::ZeroInitialization setup(fresh);
      Require(!setup.Prepare(false), "existing untracked Zero configuration was reset");
      Require(setup.Prepare(true), "fresh Zero did not persist setup before Add");
      // Simulate a new host after Add completed but before options document.
      gbrowser::webview::ZeroInitialization restart(fresh);
      Require(restart.Prepare(false), "unstarted setup lost on runtime registration restart");
      Require(restart.Begin(), "prepared setup did not begin once");
      Require(!setup.Prepare(true) && !restart.Begin(), "started/unknown setup was replayed");
      const auto bad = root / L"ZeroBad"; std::filesystem::create_directory(bad);
      std::filesystem::create_directory(bad / L"zeroomega-initialization.started");
      bool rejected = false;
      try { gbrowser::webview::ZeroInitialization(bad).Prepare(true); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "directory marker treated as known submitted configuration");
      std::filesystem::remove(bad / L"zeroomega-initialization.started");
      { std::ofstream corrupt(bad / L"zeroomega-initialization.prepared"); corrupt << "unexpected marker bytes"; }
      rejected = false;
      try { gbrowser::webview::ZeroInitialization(bad).Prepare(false); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "corrupt pending setup caused reset");
      rejected = false;
      try { gbrowser::webview::ZeroInitialization(root / L"MissingParent").Prepare(true); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "missing parent inferred an already submitted configuration");
      std::cout << "PASS first Zero setup: pre-Add intent/restart/once-only/unknown and bad marker rejection; browser NOT_STARTED\n";
    }
    const auto source = root / L"Source", moved = root / L"Moved";
    std::filesystem::create_directory(source);
    {
      gbrowser::webview::DirectoryPins pins;
      pins.Tree(source);
      Require(!MoveFileExW(source.c_str(), moved.c_str(), 0), "held source directory was renamed");
      Require(std::filesystem::exists(source) && !std::filesystem::exists(moved), "source binding changed");
      std::ofstream writer(source / L"ordinary-state.txt");
      writer << "synthetic source guard only\n";
      writer.close(); Require(bool(writer), "ordinary directory writes were blocked");
      bool rejected = false;
      try { pins.One(source / L"ordinary-state.txt"); } catch (const std::exception&) { rejected = true; }
      Require(rejected, "a regular file was accepted as directory pin");
    }
    Require(MoveFileExW(source.c_str(), moved.c_str(), 0) != FALSE, "directory pin did not drain");
    Require(MoveFileExW(moved.c_str(), source.c_str(), 0) != FALSE, "isolated source could not be restored");
    const auto profile = root / L"OwnRegistration", other = root / L"OtherRegistration";
    std::filesystem::create_directory(profile); std::filesystem::create_directory(other);
    gbrowser::webview::ComponentRegistrations registrations(root, profile);
    const std::string first(64, 'a'), changed(64, 'b');
    Require(!registrations.Matches(1, first), "missing receipt became a loaded registration");
    registrations.Record(1, first);
    Require(registrations.Matches(1, first) && !registrations.Matches(1, changed), "loaded helper tree mismatch");
    std::filesystem::copy_file(profile / L"loaded-tarou.bin", other / L"loaded-tarou.bin");
    bool wrong_profile = false;
    try { gbrowser::webview::ComponentRegistrations elsewhere(root, other); elsewhere.Matches(1, first); }
    catch (const std::exception&) { wrong_profile = true; }
    Require(wrong_profile, "loaded registration reused across profile roots");
    registrations.Record(1, changed);
    Require(registrations.Matches(1, changed) && !registrations.Matches(1, first), "changed tree not recorded exactly");
    { std::ofstream interrupted(profile / L"loaded-tarou.bin.pending"); interrupted << "own interrupted publication"; }
    bool unknown = false;
    try { registrations.Matches(1, changed); } catch (const std::exception&) { unknown = true; }
    Require(unknown, "interrupted receipt treated as missing/successful");
    registrations.Record(2, first);
    { std::ofstream corrupt(profile / L"loaded-arcarum.bin", std::ios::binary | std::ios::trunc); corrupt << "own corruption"; }
    bool tampered = false;
    try { registrations.Matches(2, first); } catch (const std::exception&) { tampered = true; }
    Require(tampered, "corrupt receipt silently caused a reinstall");
    registrations.Record(3, first);
    Require(registrations.Matches(3, first) && !registrations.Matches(3, changed),
            "fixed script successful-load receipt not bound to source tree");
    std::ofstream report(root / L"result.txt");
    report << "DIRECTORY_RENAME_BLOCKED_WHILE_HELD\nORDINARY_WRITE_ALLOWED\nFILE_AS_DIRECTORY_REJECTED\n"
              "DIRECTORY_RENAME_ALLOWED_AFTER_DRAIN\nBROWSER_NOT_STARTED\n";
    report.close(); Require(bool(report), "could not retain test evidence");
    std::cout << "PASS native directory pin: held binding/write/file rejection/drain; browser NOT_STARTED\n";
    std::cout << "PASS per-profile successful-load receipt: changed tree/profile/tamper/unknown rejection; no runtime\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
