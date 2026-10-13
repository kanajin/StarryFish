#include <windows.h>
#include "file_digest.h"
#include <array>
#include <limits>
#include <stdexcept>

namespace gbrowser {
namespace {
void Require(bool ok) {
  if (!ok) throw std::runtime_error("无法核对固定文件摘要。未提交组件或导出文件。");
}
}
FileDigest::FileDigest() {
  Require(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0);
  if (BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm_, 0); algorithm_ = nullptr; Require(false);
  }
}
FileDigest::~FileDigest() {
  if (hash_) BCryptDestroyHash(hash_);
  if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
}
void FileDigest::Add(const void* data, size_t size) {
  Require(size <= std::numeric_limits<ULONG>::max() && (!size || data) &&
      BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0) >= 0);
}
std::string FileDigest::Finish() {
  std::array<unsigned char, 32> digest{};
  Require(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0);
  std::string result; result.reserve(64);
  constexpr char alphabet[] = "0123456789abcdef";
  for (auto byte : digest) { result += alphabet[byte >> 4]; result += alphabet[byte & 15]; }
  return result;
}
}
