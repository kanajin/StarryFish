#include "shell_model.h"
#include <iostream>
#include <stdexcept>

namespace { void Check(bool ok) { if (!ok) throw std::runtime_error("Shell model check failed"); } }
int main() {
  using namespace starryfish::shell;
  try {
    auto full = IconTabs(300, 32, 8);
    Check(full.size() == 8 && full[1].x == 36 && full[0].width == 32);
    auto overlap = IconTabs(60, 32, 8);
    Check(overlap.back().x + overlap.back().width == 60);
    Check(HitTab(overlap, 31, 15) == 7);  // Right tab wins, even over selected tab.
    Check(HitTab(overlap, 60, 15) == -1);
    Check(HitTab(IconTabs(0, 0, 8), 0, 0) == -1);
    for (int width : {640, 800, 1280}) {
      for (bool two_rows : {false, true}) {
        const auto layout = Layout(width, 820, two_rows, 8);
        Check(layout.content.y == (two_rows ? 86 : 44));
        Check(layout.content.width == width - (two_rows ? 12 : 54));
        Check(layout.address.x + layout.address.width <= width);
        Check(layout.tabs.x + layout.tabs.width < layout.close.x);
      }
    }
    DocumentEpoch view(1, 7), other_profile(2, 7), other_view(1, 8);
    auto first = view.Navigate("frame-a", "https://game.granbluefantasy.jp");
    auto current = view.Navigate("frame-a", "https://game.granbluefantasy.jp");
    other_profile.Navigate("frame-a", "https://game.granbluefantasy.jp");
    other_profile.Navigate("frame-a", "https://game.granbluefantasy.jp");
    other_view.Navigate("frame-a", "https://game.granbluefantasy.jp");
    other_view.Navigate("frame-a", "https://game.granbluefantasy.jp");
    Check(!view.Current(first) && view.Current(current));
    Check(view.AllowsGameMessage(current, true, "https://game.granbluefantasy.jp"));
    Check(!view.AllowsGameMessage(current, false, "https://game.granbluefantasy.jp"));
    Check(!view.AllowsGameMessage(current, true, "https://game.granbluefantasy.jp.evil.test"));
    Check(!other_profile.Current(current) && !other_view.Current(current));
    auto untrusted = view.Navigate("frame-b", "https://example.invalid");
    Check(!view.AllowsGameMessage(untrusted, true, "https://game.granbluefantasy.jp"));
    view.Invalidate(); Check(!view.Current(untrusted));
    DocumentEpoch unbound_profile(0, 1), unbound_view(1, 0);
    Check(!unbound_profile.Current(unbound_profile.Navigate("frame-a", "https://game.granbluefantasy.jp")));
    Check(!unbound_view.Current(unbound_view.Navigate("frame-a", "https://game.granbluefantasy.jp")));
    Check(!view.Current(view.Navigate("", "https://game.granbluefantasy.jp")));
    std::cout << "PASS shell geometry, overlap priority and document callback boundaries\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
