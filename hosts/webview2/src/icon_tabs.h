#pragma once
#include "ui_interaction.h"

namespace gbrowser::webview {
inline int IconTabsPreferredWidth(size_t count, unsigned dpi) {
  const int n = static_cast<int>((std::min)(count, size_t{16}));
  return n ? n * UiPixels(32, dpi) + (n - 1) * UiPixels(4, dpi) : 0;
}
// Every pill keeps its icon padding. Less room reduces the common advance;
// later/right pills cover earlier/left pills, including the selected pill.
// Painting, pointer selection, tooltips and middle close use this one geometry.
class IconTabLayout {
 public:
  IconTabLayout(int width, int height, unsigned dpi, size_t count)
      : count_((std::min)(count, size_t{16})),
        width_((std::max)(0, width)), height_((std::max)(0, height)),
        pill_((std::min)(width_, UiPixels(32, dpi))),
        row_((std::min)(height_, UiPixels(30, dpi))),
        occupied_((std::min)(width_, IconTabsPreferredWidth(count_, dpi))) {}
  Rect Item(size_t position) const {
    if (position >= count_) return {};
    const int x = count_ > 1 ? static_cast<int>(position * (occupied_ - pill_) / (count_ - 1)) : 0;
    return {x, (height_ - row_) / 2, pill_, row_};
  }
  int Hit(int x, int y) const {
    for (size_t i = count_; i > 0; --i) {
      const auto r = Item(i - 1);
      if (r.width && r.height && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
        return static_cast<int>(i - 1);
    }
    return -1;
  }
 private:
  size_t count_;
  int width_, height_, pill_, row_, occupied_;
};
}
