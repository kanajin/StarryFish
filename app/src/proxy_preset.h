#pragma once
#include <string_view>

namespace gbrowser {
// FR13: complete schema-2 options input for ZeroOmega 3.5.2's native restore,
// not the old development profile fragment. Import replaces extension options;
// Contains acgpower and the fixed ShimakazeGo snapshot with 岛风go alias;
// startup selects direct. Native restore actively applies it; this is not a
// side-effect-free profile merge or a proof that proxy application succeeds.
std::string_view InitialProxyOptions();
}
