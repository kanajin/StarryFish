#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <shlwapi.h>
#include <wrl.h>
#include <gdiplus.h>
#include "icon_tabs.h"
#include "tab_slots.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace gbrowser::webview {
enum class UiIcon { None, Profile, Proxy, Tarou, Arcarum, Dual, Copy, Components, More, Arrow, Detach, Close, Minimize, Maximize, Restore, Reload, Plus, Page, Keyboard };
struct UiPalette { COLORREF background, surface, hover, selected, ink, muted, accent, border; };
inline UiPalette BluePalette(bool dark) {
  return dark ? UiPalette{RGB(23,37,64), RGB(32,50,80), RGB(16,29,52), RGB(49,78,119),
      RGB(226,236,247), RGB(180,199,220), RGB(157,204,242), RGB(102,136,170)} :
      UiPalette{RGB(226,238,246), RGB(242,247,251), RGB(210,229,242), RGB(192,219,238),
      RGB(30,57,79), RGB(71,96,117), RGB(36,99,143), RGB(109,145,170)};
}
// A shared, cached native renderer: no web/theme injection and no icon I/O in paint.
class UiTheme {
 public:
  UiTheme() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) token_ = 0;
    SetDark(false);
  }
  ~UiTheme() {
    for (auto& icon : tab_icons_) icon.Reset();
    for (auto& icon : icons_) icon.reset();
    DeleteObject(background_); DeleteObject(surface_);
    if (token_) Gdiplus::GdiplusShutdown(token_);
  }
  UiTheme(const UiTheme&) = delete;
  UiTheme& operator=(const UiTheme&) = delete;
  bool dark() const { return dark_; }
  const UiPalette& colors() const { return palette_; }
  void SetDark(bool dark) {
    dark_ = dark; palette_ = BluePalette(dark);
    DeleteObject(background_); DeleteObject(surface_);
    background_ = CreateSolidBrush(palette_.background); surface_ = CreateSolidBrush(palette_.surface);
  }
  void LoadIcon(size_t index, const std::filesystem::path& path) {
    if (!token_ || index >= icons_.size()) return;
    icons_[index].reset(Gdiplus::Bitmap::FromFile(path.c_str()));
    if (icons_[index] && icons_[index]->GetLastStatus() != Gdiplus::Ok) icons_[index].reset();
  }
  void ClearTabIcon(size_t index) { if (index < tab_icons_.size()) tab_icons_[index].Reset(); }
  bool SetTabIcon(size_t index, IStream* source) {
    if (index >= tab_icons_.size()) return false;
    ClearTabIcon(index);
    if (!token_ || !source) return false;
    // Copy only bounded image bytes. Keep the private stream alive for GDI+;
    // no website/icon URL, profile file or disk/network I/O enters paint.
    STATSTG stat{};
    if (FAILED(source->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart || stat.cbSize.QuadPart > 256 * 1024) return false;
    LARGE_INTEGER start{};
    if (FAILED(source->Seek(start, STREAM_SEEK_SET, nullptr))) return false;
    std::vector<BYTE> bytes(static_cast<size_t>(stat.cbSize.QuadPart)); ULONG read = 0;
    if (FAILED(source->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size()) return false;
    auto& icon = tab_icons_[index];
    icon.stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!icon.stream) return false;
    icon.bitmap.reset(Gdiplus::Bitmap::FromStream(icon.stream.Get(), FALSE));
    if (!icon.bitmap || icon.bitmap->GetLastStatus() != Gdiplus::Ok || !icon.bitmap->GetWidth() ||
        !icon.bitmap->GetHeight() || icon.bitmap->GetWidth() > 256 || icon.bitmap->GetHeight() > 256) {
      icon.Reset(); return false;
    }
    return true;
  }
  HBRUSH Control(HDC dc, bool input) const {
    SetTextColor(dc, palette_.ink); SetBkColor(dc, input ? palette_.surface : palette_.background);
    return input ? surface_ : background_;
  }
  void Fill(HDC dc, RECT rect, COLORREF color) const {
    const auto brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
  }
  void Background(HDC dc, RECT rect) const { FillRect(dc, &rect, background_); }
  void Window(HWND window) const {
    if (!window) return;
    const BOOL dark = dark_; const DWORD round = 2;
    // Public Windows 11 attributes are best effort on older supported Windows.
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, 33, &round, sizeof(round));
    DwmSetWindowAttribute(window, 35, &palette_.background, sizeof(COLORREF));
    DwmSetWindowAttribute(window, 36, &palette_.ink, sizeof(COLORREF));
  }
  void Round(HDC dc, RECT rect, int radius, COLORREF fill, COLORREF border) const {
    if (!token_) { Fill(dc, rect, fill); return; }
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    const float x = static_cast<float>(rect.left) + .5f, y = static_cast<float>(rect.top) + .5f;
    const float w = static_cast<float>(rect.right - rect.left) - 1, h = static_cast<float>(rect.bottom - rect.top) - 1;
    if (w <= 0 || h <= 0) return;
    const float d = (std::min)(static_cast<float>(radius * 2), (std::min)(w, h));
    path.AddArc(x,y,d,d,180,90); path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90); path.AddArc(x,y+h-d,d,d,90,90); path.CloseFigure();
    Gdiplus::SolidBrush brush(Color(fill)); Gdiplus::Pen pen(Color(border));
    g.FillPath(&brush, &path); g.DrawPath(&pen, &path);
  }
  void Text(HDC dc, HFONT font, const wchar_t* text, RECT rect, COLORREF color, UINT align = DT_CENTER) const {
    const auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);
  }
  void Icon(HDC dc, UiIcon icon, RECT rect, COLORREF color) const {
    if (!token_ || icon == UiIcon::None) return;
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const int source = icon == UiIcon::Proxy ? 0 : icon == UiIcon::Tarou ? 1 : icon == UiIcon::Arcarum ? 2 : -1;
    if (source >= 0) {
      const auto& image = icons_[static_cast<size_t>(source)];
      if (image) { g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(image.get(), static_cast<int>(rect.left), static_cast<int>(rect.top),
            static_cast<int>(rect.right-rect.left), static_cast<int>(rect.bottom-rect.top)); }
      return; // Missing third-party artwork never gets a misleading replacement.
    }
    g.TranslateTransform(static_cast<float>(rect.left), static_cast<float>(rect.top));
    g.ScaleTransform(static_cast<float>(rect.right-rect.left)/24.f, static_cast<float>(rect.bottom-rect.top)/24.f);
    Gdiplus::Pen pen(Color(color), 1.7f); pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const auto line = [&](float x,float y,float xx,float yy) { g.DrawLine(&pen,x,y,xx,yy); };
    switch (icon) {
      case UiIcon::Profile: g.DrawEllipse(&pen,8.f,3.f,8.f,8.f); g.DrawArc(&pen,4.f,13.f,16.f,14.f,180.f,180.f); break;
      case UiIcon::Dual: g.DrawRectangle(&pen,3.f,4.f,18.f,16.f); line(12,4,12,20); break;
      case UiIcon::Copy: g.DrawRectangle(&pen,8.f,8.f,12.f,13.f); line(4,16,4,3); line(4,3,16,3); break;
      case UiIcon::Components:
        for (int y : {4,14}) for (int x : {4,14}) g.DrawRectangle(&pen,static_cast<float>(x),static_cast<float>(y),6.f,6.f); break;
      case UiIcon::More: { Gdiplus::SolidBrush brush(Color(color)); for (int x : {4,11,18}) g.FillEllipse(&brush,static_cast<float>(x),10.f,3.f,3.f); break; }
      case UiIcon::Arrow: // Send the right-hand page toward the left pane, once.
        line(18,7,21,7); line(21,7,21,17); line(21,17,18,17);
        line(17,12,4,12); line(4,12,9,7); line(4,12,9,17); break;
      case UiIcon::Minimize: line(5,12,19,12); break;
      case UiIcon::Maximize: g.DrawRectangle(&pen,5.f,5.f,14.f,14.f); break;
      case UiIcon::Restore: g.DrawRectangle(&pen,4.f,8.f,12.f,12.f); line(8,4,20,4); line(20,4,20,16); break;
      case UiIcon::Detach: line(7,3,17,3); line(9,3,9,10); line(15,3,15,10); line(9,10,5,14); line(5,14,19,14); line(19,14,15,10); line(12,14,12,22); break;
      case UiIcon::Close: line(6,6,18,18); line(18,6,6,18); break;
      case UiIcon::Reload: g.DrawArc(&pen,5.f,5.f,14.f,14.f,40.f,285.f); line(19,4,19,10); line(19,10,13,10); break;
      case UiIcon::Plus: line(5,12,19,12); line(12,5,12,19); break;
      case UiIcon::Keyboard: g.DrawRectangle(&pen,2.f,5.f,20.f,14.f);
        for (int y : {9, 12}) for (int x : {6, 10, 14, 18}) line(static_cast<float>(x),static_cast<float>(y),static_cast<float>(x)+1,static_cast<float>(y));
        line(7,16,17,16); break;
      case UiIcon::Page: g.DrawEllipse(&pen,3.f,3.f,18.f,18.f); g.DrawEllipse(&pen,8.f,3.f,8.f,18.f);
        line(3,12,21,12); break;
      default: break;
    }
  }
  void Button(const DRAWITEMSTRUCT& draw, HFONT font, UiIcon icon = UiIcon::None,
              bool stacked = false, bool selected = false, bool icon_only = false, unsigned render_dpi = 0, bool flat = false) const {
    RECT rect = draw.rcItem; Background(draw.hDC, rect);
    const int dpi = static_cast<int>(render_dpi ? render_dpi : GetDpiForWindow(draw.hwndItem));
    const auto s = [dpi](int n) { return MulDiv(n, dpi ? dpi : 96, 96); };
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool hover = GetPropW(draw.hwndItem, L"Gbrowser.UiHover") != nullptr;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool focus = (draw.itemState & ODS_FOCUS) && !(draw.itemState & ODS_NOFOCUSRECT) &&
        !(SendMessageW(draw.hwndItem, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    InflateRect(&rect, -1, -1);
    const auto fill = pressed ? palette_.hover : hover ? palette_.hover : flat ? palette_.background : selected ? palette_.selected : palette_.surface;
    Round(draw.hDC, rect, s(8), fill, focus ? palette_.accent : fill);
    const auto ink = disabled ? palette_.muted : palette_.ink;
    const int size = s(icon == UiIcon::Arrow ? 28 : stacked ? 20 : icon_only ? 22 : 16);
    const int center = (rect.left + rect.right)/2;
    const int top = stacked ? rect.top + s(4) : (rect.top+rect.bottom-size)/2;
    RECT image{center-size/2,top,center-size/2+size,top+size};
    wchar_t text[128]{}; GetWindowTextW(draw.hwndItem,text,128);
    RECT label = rect;
    if (stacked) { label.top = top + size + s(1); label.bottom -= s(2); }
    else if (!icon_only && icon != UiIcon::None) {
      image.left = rect.left+s(5); image.right = image.left+size; label.left = image.right+s(3); label.right -= s(3);
    }
    Icon(draw.hDC,icon,image,selected ? palette_.accent : ink);
    if (!icon_only) Text(draw.hDC,font,text,label,ink);
    if (flat && selected) {
      RECT marker{center-s(5),rect.bottom-s(3),center+s(5),rect.bottom-s(1)};
      Fill(draw.hDC,marker,palette_.accent);
    }
  }
  void Header(const DRAWITEMSTRUCT& draw, HFONT font, int size) const {
    Background(draw.hDC,draw.rcItem);
    const int top=(draw.rcItem.top+draw.rcItem.bottom-size)/2;
    RECT icon{draw.rcItem.left+size/2,top,draw.rcItem.left+size*3/2,top+size};
    Icon(draw.hDC,UiIcon::Tarou,icon,palette_.ink);
    RECT text=draw.rcItem; text.left=icon.right+size/2;
    Text(draw.hDC,font,L"Tarou",text,palette_.ink,DT_LEFT);
  }
  void Tabs(HWND window, HDC dc, const std::vector<size_t>& order, size_t selected, unsigned render_dpi = 0) const {
    RECT client{}; GetClientRect(window,&client); Background(dc,client);
    const unsigned dpi=render_dpi ? render_dpi : GetDpiForWindow(window);
    const IconTabLayout layout(client.right, client.bottom, dpi, order.size());
    for (size_t i=0;i<order.size();++i) {
      const auto item = layout.Item(i);
      if (!item.width || !item.height) continue;
      RECT rect{item.x, item.y, item.x+item.width, item.y+item.height};
      Fill(dc,rect,palette_.background); // The right pill covers the left pill, irrespective of selection.
      Round(dc,rect,MulDiv(8,static_cast<int>(dpi),96),order[i]==selected?palette_.surface:palette_.background,palette_.border);
      const int size=MulDiv(16,static_cast<int>(dpi),96);
      const int x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;
      const RECT image{x,y,x+size,y+size};
      const int saved=SaveDC(dc); IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
      auto* bitmap=order[i]<tab_icons_.size()?tab_icons_[order[i]].bitmap.get():nullptr;
      if (bitmap && token_) {
        Gdiplus::Graphics g(dc);g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(bitmap,x,y,size,size);
      } else Icon(dc,UiIcon::Page,image,palette_.muted);
      RestoreDC(dc,saved);
      if (order[i]==selected && GetFocus()==window && !(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)) {
        InflateRect(&rect,-3,-3);DrawFocusRect(dc,&rect);
      }
    }
  }
  static void AttachButton(HWND window) { SetWindowSubclass(window,ButtonProc,91,0); }
  // Owner-drawn menu items retain native command IDs, keyboard navigation and text.
  struct MenuItem { std::wstring text; bool separator=false, checked=false; };
  std::vector<std::unique_ptr<MenuItem>> PrepareMenu(HMENU menu) const {
    std::vector<std::unique_ptr<MenuItem>> items;
    for (int i=0;i<GetMenuItemCount(menu);++i) {
      wchar_t text[256]{}; MENUITEMINFOW info{sizeof(info)};
      info.fMask=MIIM_FTYPE|MIIM_STRING|MIIM_STATE|MIIM_SUBMENU; info.dwTypeData=text; info.cch=256;
      if (!GetMenuItemInfoW(menu,static_cast<UINT>(i),TRUE,&info)) continue;
      if (info.hSubMenu) {
        auto children = PrepareMenu(info.hSubMenu);
        for (auto& child : children) items.push_back(std::move(child));
      }
      auto item=std::make_unique<MenuItem>(); item->text=text;
      item->separator=(info.fType&MFT_SEPARATOR)!=0; item->checked=(info.fState&MFS_CHECKED)!=0;
      info.fMask=MIIM_FTYPE|MIIM_DATA; info.fType|=MFT_OWNERDRAW; info.dwItemData=reinterpret_cast<ULONG_PTR>(item.get());
      SetMenuItemInfoW(menu,static_cast<UINT>(i),TRUE,&info); items.push_back(std::move(item));
    }
    return items;
  }
  void MeasureMenu(MEASUREITEMSTRUCT& item, HWND owner, HFONT font) const {
    auto data=reinterpret_cast<MenuItem*>(item.itemData); if (!data) return;
    HDC dc=GetDC(owner); const auto old=SelectObject(dc,font); SIZE size{};
    GetTextExtentPoint32W(dc,data->text.c_str(),static_cast<int>(data->text.size()),&size);
    SelectObject(dc,old); ReleaseDC(owner,dc);
    const int dpi=static_cast<int>(GetDpiForWindow(owner));
    item.itemWidth=static_cast<UINT>((std::max)(MulDiv(190,dpi,96),static_cast<int>(size.cx)+MulDiv(52,dpi,96)));
    item.itemHeight=static_cast<UINT>(MulDiv(data->separator?9:34,dpi,96));
  }
  void Menu(const DRAWITEMSTRUCT& draw, HFONT font, unsigned dpi) const {
    auto data=reinterpret_cast<MenuItem*>(draw.itemData); if (!data) return;
    RECT rect=draw.rcItem; Background(draw.hDC,rect);
    const int inset=MulDiv(8,static_cast<int>(dpi),96);
    if (data->separator) { rect.left+=inset;rect.right-=inset;rect.top=(rect.top+rect.bottom)/2;rect.bottom=rect.top+1;Fill(draw.hDC,rect,palette_.border);return; }
    if (draw.itemState&ODS_SELECTED) { InflateRect(&rect,-3,-2);Round(draw.hDC,rect,inset,palette_.hover,palette_.hover); }
    rect=draw.rcItem;rect.left+=inset*3;rect.right-=inset;
    // DT_EXPANDTABS preserves the existing shortcut labels without changing IDs.
    const auto old=SelectObject(draw.hDC,font);SetBkMode(draw.hDC,TRANSPARENT);SetTextColor(draw.hDC,palette_.ink);
    if (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) SetTextColor(draw.hDC,palette_.muted);
    DrawTextW(draw.hDC,data->text.c_str(),-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_EXPANDTABS|DT_NOPREFIX);
    if (data->checked) { rect=draw.rcItem;rect.right=rect.left+inset*3;Text(draw.hDC,font,L"✓",rect,palette_.accent); }
    SelectObject(draw.hDC,old);
  }
 private:
  static Gdiplus::Color Color(COLORREF c) { return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c)); }
  static LRESULT CALLBACK ButtonProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    // Pointer focus must not leave a keyboard focus ring. Keep native focus and
    // activation intact; keyboard use restores the single rounded focus cue.
    if (m==WM_LBUTTONDOWN || m==WM_LBUTTONDBLCLK) {
      SendMessageW(w,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
      InvalidateRect(w,nullptr,FALSE);
    } else if (m==WM_KEYDOWN) {
      SendMessageW(w,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
      InvalidateRect(w,nullptr,FALSE);
    }
    if (m==WM_MOUSEMOVE && !GetPropW(w,L"Gbrowser.UiHover")) {
      SetPropW(w,L"Gbrowser.UiHover",reinterpret_cast<HANDLE>(1));
      TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);InvalidateRect(w,nullptr,FALSE);
    }
    if (m==WM_MOUSELEAVE || m==WM_NCDESTROY) { RemovePropW(w,L"Gbrowser.UiHover");InvalidateRect(w,nullptr,FALSE); }
    if (m==WM_NCDESTROY) RemoveWindowSubclass(w,ButtonProc,id);
    return DefSubclassProc(w,m,wp,lp);
  }
  ULONG_PTR token_=0;
  struct TabIcon {
    Microsoft::WRL::ComPtr<IStream> stream;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    void Reset() { bitmap.reset(); stream.Reset(); }
  };
  std::array<TabIcon,TabSlots::kViewCount> tab_icons_{};
  std::array<std::unique_ptr<Gdiplus::Bitmap>,3> icons_{};
  bool dark_=false;
  UiPalette palette_{};
  HBRUSH background_=nullptr,surface_=nullptr;
};
}
