#include "shell_model.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace starryfish::shell {
Palette BluePalette(bool dark) {
  return dark ? Palette{0xff172540, 0xff203250, 0xff314e77, 0xffe2ecf7, 0xffb4c7dc, 0xff9dccf2}
              : Palette{0xffe2eef6, 0xfff2f7fb, 0xffc0dbee, 0xff1e394f, 0xff476075, 0xff24638f};
}

std::vector<Rect> IconTabs(int width, int height, size_t count) {
  count = std::min(count, size_t{16});
  width = std::max(0, width); height = std::max(0, height);
  const int pill = std::min(width, 32), row = std::min(height, 30);
  const int occupied = std::min(width, static_cast<int>(count) * 36 - (count ? 4 : 0));
  std::vector<Rect> result;
  for (size_t i = 0; i < count; ++i) {
    const int x = count > 1 ? static_cast<int>(i * (occupied - pill) / (count - 1)) : 0;
    result.push_back({x, (height - row) / 2, pill, row});
  }
  return result;
}

int HitTab(const std::vector<Rect>& tabs, int x, int y) {
  for (size_t i = tabs.size(); i > 0; --i) {
    const auto& r = tabs[i - 1];
    if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
      return static_cast<int>(i - 1);
  }
  return -1;
}

Toolbar Layout(int width, int height, bool two_rows, size_t tabs) {
  width = std::max(0, width); height = std::max(0, height);
  Toolbar out;
  const int window_x = std::max(0, width - 112);
  out.minimize = {window_x, 4, 36, 36}; out.maximize = {window_x + 36, 4, 36, 36};
  out.close = {window_x + 72, 4, 36, 36};
  const int preferred = static_cast<int>(std::min(tabs, size_t{16})) * 36 - (tabs ? 4 : 0);
  if (two_rows) {
    const int tab_width = std::min(preferred, std::max(0, window_x - 180));
    out.tabs = {8, 6, tab_width, 32}; out.add = {12 + tab_width, 6, 30, 30};
    out.title = {48 + tab_width, 4, std::max(0, window_x - tab_width - 52), 36};
    out.reload = {8, 46, 36, 32}; out.address = {50, 46, std::max(0, width - 154), 32};
    out.theme = {width - 96, 46, 36, 32}; out.menu = {width - 50, 46, 36, 32};
  } else {
    // Same 2/3-width compact address box and 48-DIP right rail as Windows.
    const int title_width = width >= 800 ? 190 : width >= 600 ? 130 : 36;
    const int title_x = std::max(0, window_x - title_width - 6);
    const int address_width = std::clamp(title_x - 142, 100, 236) * 2 / 3;
    out.reload = {8, 4, 36, 36}; out.address = {50, 6, address_width, 32};
    const int tabs_x = 50 + address_width + 12;
    const int tab_width = std::min(preferred, std::max(0, title_x - tabs_x - 42));
    out.tabs = {tabs_x, 6, tab_width, 32}; out.add = {tabs_x + tab_width + 4, 6, 30, 30};
    out.title = {title_x, 4, title_width, 36};
    out.menu = {width - 42, 50, 36, 36}; out.theme = {width - 42, 94, 36, 36};
  }
  const int top = two_rows ? 86 : 44, rail = two_rows ? 6 : 48;
  out.content = {6, top, std::max(0, width - rail - 6), std::max(0, height - top - 6)};
  return out;
}

DocumentTicket DocumentEpoch::Navigate(std::string main_frame, std::string origin) {
  Invalidate();
  if (profile_ != 0 && view_ != 0 &&
      generation_ != std::numeric_limits<uint64_t>::max() && !main_frame.empty()) {
    frame_ = std::move(main_frame); origin_ = std::move(origin); live_ = true;
  }
  return {profile_, view_, generation_, frame_};
}
void DocumentEpoch::Invalidate() {
  live_ = false; frame_.clear(); origin_.clear();
  if (generation_ != std::numeric_limits<uint64_t>::max()) ++generation_;
}
bool DocumentEpoch::Current(const DocumentTicket& ticket) const {
  return live_ && ticket == DocumentTicket{profile_, view_, generation_, frame_};
}
bool DocumentEpoch::AllowsGameMessage(const DocumentTicket& ticket, bool actual_main_frame,
                                     const std::string& actual_origin) const {
  constexpr char game[] = "https://game.granbluefantasy.jp";
  return Current(ticket) && actual_main_frame && origin_ == game && actual_origin == game;
}
}  // namespace starryfish::shell
