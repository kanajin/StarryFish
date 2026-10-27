#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace starryfish::shell {
struct Rect { int x = 0, y = 0, width = 0, height = 0; };
struct Palette { uint32_t background, surface, selected, ink, muted, accent; };
// Extracted from the Windows blue palette and icon-tab rules. Coordinates are
// logical pixels; platform adapters perform DPI conversion, drawing and input.
Palette BluePalette(bool dark);
std::vector<Rect> IconTabs(int width, int height, size_t count);
int HitTab(const std::vector<Rect>& tabs, int x, int y);
struct Toolbar {
  Rect reload, address, tabs, add, title, minimize, maximize, close, menu, theme;
  Rect content;
};
Toolbar Layout(int width, int height, bool two_rows, size_t tabs);

struct DocumentTicket {
  uint64_t profile = 0, view = 0, navigation = 0;
  std::string main_frame;
  bool operator==(const DocumentTicket&) const = default;
};
class DocumentEpoch {
 public:
  DocumentEpoch(uint64_t profile, uint64_t view) : profile_(profile), view_(view) {}
  DocumentTicket Navigate(std::string main_frame, std::string origin);
  void Invalidate();
  bool Current(const DocumentTicket& ticket) const;
  bool AllowsGameMessage(const DocumentTicket& ticket, bool actual_main_frame,
                         const std::string& actual_origin) const;
 private:
  uint64_t profile_, view_, generation_ = 0;
  bool live_ = false;
  std::string frame_, origin_;
};
}  // namespace starryfish::shell
