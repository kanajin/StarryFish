#include "ui_theme.h"
#include "compact_chrome.h"
#include "convenience_layout.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace gbrowser::webview;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double Luminance(COLORREF color) {
  const auto channel=[](int value) { const double v=value/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4); };
  return .2126*channel(GetRValue(color))+.7152*channel(GetGValue(color))+.0722*channel(GetBValue(color));
}
double Contrast(COLORREF a,COLORREF b) { const auto x=Luminance(a),y=Luminance(b);return ((std::max)(x,y)+.05)/((std::min)(x,y)+.05); }
}
// Offscreen native drawing only. No WebView/runtime/profile/network or input automation.
int wmain(int argc,wchar_t** argv) {
  try {
    Require(argc==4,"output directory, public component tree, DPI required");
    const unsigned dpi=static_cast<unsigned>(std::stoul(argv[3])); Require(dpi==96 || dpi==144 || dpi==192,"unsupported preview DPI");
    const auto scale=[dpi](int n) {return MulDiv(n,static_cast<int>(dpi),96);};
    const auto scaled=[&](RECT r) {return RECT{scale(r.left),scale(r.top),scale(r.right),scale(r.bottom)};};
    const std::filesystem::path output(argv[1]),components(argv[2]);
    Require(std::filesystem::create_directory(output),"preview output must be new");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_TAB_CLASSES};InitCommonControlsEx(&init);
    UiTheme theme;
    theme.LoadIcon(0,components/L"zeroomega/img/icons/omega-48.png");
    theme.LoadIcon(1,components/L"tarou/assets/icon-48.png");
    theme.LoadIcon(2,components/L"arcarum-dungeon/icons/icon-48.png");
    HWND owner=CreateWindowW(L"STATIC",L"isolated drawing fixture",WS_OVERLAPPED,0,0,1100,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(owner!=nullptr,"cannot create hidden drawing owner");
    const auto font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HWND tabs=CreateWindowW(L"STATIC",L"",WS_CHILD|SS_NOTIFY,scale(318),scale(8),scale(704),scale(34),owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(tabs,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    const std::vector<size_t> tab_order{0,6};
    const IconTabLayout tab_layout(scale(704),scale(34),dpi,tab_order.size());
    const auto hit_rect=tab_layout.Item(0);
    for (int y : {hit_rect.y,hit_rect.y+hit_rect.height-1})
      Require(tab_layout.Hit(hit_rect.x+hit_rect.width/2,y)==0,"painted tab edge misses its hit target");
    Microsoft::WRL::ComPtr<IStream> favicon;
    Require(SUCCEEDED(SHCreateStreamOnFileEx((components/L"tarou/assets/icon-48.png").c_str(),
        STGM_READ|STGM_SHARE_DENY_NONE,0,FALSE,nullptr,&favicon)),"cannot open public favicon drawing fixture");
    Require(theme.SetTabIcon(0,favicon.Get()),"valid PNG favicon fixture rejected");
    const BYTE bad[]={0,1,2,3};Microsoft::WRL::ComPtr<IStream> corrupt;corrupt.Attach(SHCreateMemStream(bad,sizeof(bad)));
    Require(!theme.SetTabIcon(6,corrupt.Get()) && !theme.SetTabIcon(6,nullptr),"invalid favicon did not fall back");
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),scale(1100),-scale(640),1,32,BI_RGB,0,0,0,0,0};void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);ReleaseDC(nullptr,screen);
    Require(bitmap && dc,"cannot create preview bitmap");const auto old=SelectObject(dc,bitmap);
    UINT bytes=0,count=0;Gdiplus::GetImageEncodersSize(&count,&bytes);std::vector<BYTE> encoders(bytes);
    Gdiplus::GetImageEncoders(count,bytes,reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoders.data()));
    CLSID png{};bool found=false;
    for(UINT i=0;i<count;++i) { const auto& codec=reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoders.data())[i];
      if(wcscmp(codec.MimeType,L"image/png")==0){png=codec.Clsid;found=true;break;} }
    Require(found,"PNG encoder unavailable");
    for(bool browser_layout : {false,true}) for(bool dark : {false,true}) {
      theme.SetDark(dark);const auto& c=theme.colors();
      // Compare actual button pixels, without input injection or visible windows.
      // Pointer/hidden focus must match idle; keyboard focus remains discernible.
      HWND focus_button=CreateWindowW(L"BUTTON",L"",WS_CHILD|BS_OWNERDRAW,0,0,40,40,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
      Require(focus_button!=nullptr,"cannot create focus drawing fixture");
      const auto render_focus=[&](UINT state) {
        DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.hwndItem=focus_button;draw.hDC=dc;
        draw.rcItem=scaled({0,0,40,40});draw.itemState=state;
        theme.Button(draw,font,UiIcon::Dual,false,true,true,dpi,true);
        GdiFlush();std::vector<DWORD> pixels;
        const auto source=static_cast<const DWORD*>(bits);
        for(int y=0;y<scale(40);++y)
          pixels.insert(pixels.end(),source+y*scale(1100),source+y*scale(1100)+scale(40));
        return pixels;
      };
      SendMessageW(owner,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
      const auto idle=render_focus(0);
      Require(render_focus(ODS_FOCUS|ODS_NOFOCUSRECT)==idle,"suppressed focus changes button pixels");
      Require(render_focus(ODS_FOCUS)!=idle,"keyboard focus cue missing");
      SendMessageW(owner,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
      Require(render_focus(ODS_FOCUS)==idle,"hidden pointer focus leaves an outline");
      SendMessageW(owner,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
      Require(render_focus(ODS_FOCUS)!=idle,"keyboard focus cue did not return");
      DestroyWindow(focus_button);
      for(auto surface : {c.background,c.surface,c.hover,c.selected}) {
        Require(Contrast(c.ink,surface)>=4.5,"body contrast below AA");
        Require(Contrast(c.muted,surface)>=4.5,"secondary text contrast below AA");
        Require(Contrast(c.accent,surface)>=3,"icon contrast below AA");
      }
      theme.Background(dc,scaled({0,0,1100,640}));
      const auto bounds=[](gbrowser::Rect r) {return RECT{r.x,r.y,r.x+r.width,r.y+r.height};};
      auto top=ToolbarGeometry(scale(1100),dpi,2);
      const auto browser=BrowserToolbarGeometry(scale(1100),dpi,2);
      if(browser_layout) top={browser.address,browser.copy,browser.tabs,browser.functions[5],browser.minimize,browser.maximize,browser.close,browser.add,browser.reload};
      theme.Round(dc,bounds(top.address),scale(9),c.surface,c.border);
      auto address=bounds(top.address);InflateRect(&address,-scale(8),0);
      theme.Text(dc,font,L"game.granbluefantasy.jp",address,c.ink,DT_LEFT);
      SetWindowPos(tabs,nullptr,top.tabs.x,top.tabs.y,top.tabs.width,top.tabs.height,SWP_NOZORDER|SWP_NOACTIVATE);
      SetViewportOrgEx(dc,top.tabs.x,top.tabs.y,nullptr);theme.Tabs(tabs,dc,tab_order,0,dpi);SetViewportOrgEx(dc,0,0,nullptr);
      PaneState state;state.dual=true;state.game_width=310;state.game_dpi=96;
      const auto panes=ComputePaneLayout(scale(1100),scale(640),dpi,50,state,false,browser_layout);
      for(const auto game : {panes.left,panes.right}) {
        theme.Fill(dc,bounds(game),dark?RGB(17,24,38):RGB(235,242,247));
        theme.Text(dc,font,game.x==panes.right.x?L"主游戏视口":L"副游戏视口",bounds(game),c.muted);
      }
      auto content=panes.tarou;content.y+=scale(40);content.height-=scale(40);
      theme.Fill(dc,bounds(content),RGB(32,32,32));
      theme.Text(dc,font,L"插件继续使用自身样式",bounds(content),RGB(213,219,224));
      const auto header=HeaderGeometry(panes.tarou,dpi);
      DRAWITEMSTRUCT title{};title.hDC=dc;title.rcItem=bounds(header.title);theme.Header(title,font,scale(22));
      const auto button=[&](const wchar_t* label,UiIcon icon,gbrowser::Rect rect,bool selected=false,bool only=true,bool flat=true) {
        HWND w=CreateWindowW(L"BUTTON",label,WS_CHILD|BS_OWNERDRAW,0,0,40,40,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.hwndItem=w;draw.hDC=dc;draw.rcItem=bounds(rect);
        theme.Button(draw,font,icon,false,selected,only,dpi,flat);DestroyWindow(w);
      };
      const std::array<UiIcon,5> icons{UiIcon::Components,UiIcon::Tarou,UiIcon::Proxy,UiIcon::Arcarum,UiIcon::Dual};
      if(browser_layout) {
        const std::array<UiIcon,7> actions{UiIcon::Tarou,UiIcon::Arcarum,UiIcon::Proxy,UiIcon::Dual,UiIcon::Components,UiIcon::Profile,UiIcon::More};
        for(size_t i=0;i<actions.size();++i) button(L"",actions[i],browser.functions[i],i==0||i==3);
      } else {
        for(size_t i=0;i<icons.size();++i)
          button(L"",icons[i],{panes.chrome.content.rail.x+scale(4),scale(48+static_cast<int>(i)*44),scale(40),scale(40)},i==1||i==4);
        button(L"更多",UiIcon::More,{scale(1056),scale(590),scale(40),scale(40)});
        button(L"StarryFish · 默认资料",UiIcon::Profile,top.profile,false,false);
      }
      button(L"刷新",UiIcon::Reload,top.reload);
      button(L"复制",UiIcon::Copy,top.copy);
      button(L"新建标签",UiIcon::Plus,top.add);
      button(L"最小化",UiIcon::Minimize,top.minimize);
      button(L"最大化",UiIcon::Maximize,top.maximize);
      button(L"关闭浏览器",UiIcon::Close,top.close);
      button(L"关闭太郎",UiIcon::Close,header.close);
      button(L"拆出太郎",UiIcon::Detach,header.pin);
      button(L"同步一次",UiIcon::Arrow,{panes.link.x,panes.link.y+(panes.link.height-scale(40))/2,scale(40),scale(40)},false,true,false);
      Gdiplus::Bitmap image(bitmap,nullptr);
      const auto name=browser_layout?(dark?L"layout2-dark.png":L"layout2-light.png"):(dark?L"dark.png":L"light.png");
      Require(image.Save((output/name).c_str(),&png,nullptr)==Gdiplus::Ok,"cannot save preview");
      std::cout<<(dark?"dark":"light")<<" contrast PASS; actual native renderer PNG saved\n";
    }
    // Same native painter at an intentionally crowded width; never create the
    // old common-control up/down buttons, and never raise a selected left pill.
    const std::vector<size_t> crowded{0,6,7,8,9,10,11,12};
    for (bool dark:{false,true}) {
      theme.SetDark(dark);theme.Background(dc,scaled({0,0,1100,640}));
      SetWindowPos(tabs,nullptr,0,0,scale(80),scale(34),SWP_NOZORDER|SWP_NOACTIVATE);
      theme.Tabs(tabs,dc,crowded,0,dpi);
      Gdiplus::Bitmap canvas(bitmap,nullptr);
      std::unique_ptr<Gdiplus::Bitmap> image(canvas.Clone(0,0,scale(80),scale(34),PixelFormat32bppARGB));
      Require(image && image->Save((output/(dark?L"tabs-overlap-dark.png":L"tabs-overlap-light.png")).c_str(),&png,nullptr)==Gdiplus::Ok,
          "cannot save overlapping tab drawing");
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DeleteObject(font);DestroyWindow(owner);
    std::cout<<"PASS offscreen palettes; browser NOT_STARTED\n";return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
