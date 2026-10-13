#pragma once
#include "core.h"

namespace gbrowser {
// Retained FR04 synthetic storage evidence and unresolved unload-veto diagnosis.
// This offline fixture is not a GBF page, migration adapter or release feature.
inline constexpr char kStorageProbeOrigin[] = "https://gbrowser.invalid";
inline constexpr char kStorageProbePage[] = "https://gbrowser.invalid/storage-probe/";
inline constexpr char kStorageProbeScript[] = "https://gbrowser.invalid/storage-probe/probe.js";
// Only exact GET resources are served. The CEF adapter additionally disables
// default network handling for the whole parsed reserved origin.
bool IsStorageProbeRequest(const std::string& url, const std::string& method,
                           const std::string& initiator, bool navigation, bool download);
struct StorageProbeResource { std::string mime, body; };
StorageProbeResource MakeStorageProbeResource(const std::string& url,
                                             const std::string& profile);
const char* StorageProbeCsp();
}  // namespace gbrowser
