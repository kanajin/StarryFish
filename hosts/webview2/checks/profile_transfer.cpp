#include "profile_transfer.h"
#include <iostream>
#include <stdexcept>

void RunProfileTransferChecks(const std::filesystem::path& root) {
  using namespace gbrowser::webview::transfer;
  auto require = [](bool ok) { if (!ok) throw std::runtime_error("profile archive check failed"); };
  const std::string fixture = "synthetic-cookie=not-a-real-login; buff=[101,202]; bookmarks=测试";
  const std::wstring password = L"Synthetic-only-passphrase-2026";
  const auto archive = Encrypt(fixture, password);
  require(archive.find("synthetic-cookie") == std::string::npos);
  require(Decrypt(archive, password) == fixture);
  require(Encrypt(fixture, password) != archive);
  auto rejected = [&](const std::string& value, const std::wstring& pass) {
    try { Decrypt(value, pass); } catch (...) { return true; } return false;
  };
  require(rejected(archive, L"Wrong-password-2026"));
  for (size_t offset : {size_t(0), size_t(8), size_t(24), size_t(36), archive.size() - 1}) {
    auto corrupt = archive; corrupt[offset] ^= 1; require(rejected(corrupt, password));
  }
  require(rejected(archive.substr(0, 30), password));
  require(rejected(archive + "trailing", password));
  const auto path = root / L"synthetic-profile.sfprofile";
  WriteArchive(path, archive); require(Decrypt(ReadArchive(path), password) == fixture);
  bool refused = false; try { WriteArchive(path, archive); } catch (...) { refused = true; }
  require(refused); require(ReadArchive(path) == archive);
  std::cout << "PASS profile archive: portable roundtrip/random salt+nonce/wrong password/tamper/truncation/append/no overwrite; synthetic data only\n";
}
