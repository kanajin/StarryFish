#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <string>

namespace gbrowser {
// Shared streaming digest for independently expected public component bytes.
// A matching digest establishes byte identity, never origin or install approval.
class FileDigest {
 public:
  FileDigest();
  ~FileDigest();
  FileDigest(const FileDigest&) = delete;
  FileDigest& operator=(const FileDigest&) = delete;
  void Add(const void* data, size_t size);
  std::string Finish();
 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
};
}
