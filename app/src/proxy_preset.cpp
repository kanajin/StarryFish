#include "proxy_preset.h"
#include "proxy_preset_assets.h"

namespace gbrowser {
std::string_view InitialProxyOptions() {
  // The compiled value and installer input come from one versioned JSON file.
  // Shared by explicit no-replace export and confirmed original-worker import.
  // Reading this value or copying it into Release does not itself apply a proxy.
  return kInitialProxyOptions;
}
}
