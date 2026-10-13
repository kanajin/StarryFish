#include "backup.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace gbrowser {
namespace {
constexpr size_t kHeader = 52, kTag = 16, kPrefix = kHeader + kTag;
constexpr uint32_t kIterations = 600000;
constexpr std::array<uint8_t, 8> kMagic{'G', 'B', 'R', 'B', 'A', 'K', '0', '1'};
void Require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
void Checked(NTSTATUS status) { Require(status >= 0, "Backup cryptographic operation failed"); }
struct Algorithm {
  BCRYPT_ALG_HANDLE handle = nullptr;
  ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
  Algorithm() = default;
  Algorithm(const Algorithm&) = delete;
  Algorithm& operator=(const Algorithm&) = delete;
};
struct Key {
  BCRYPT_KEY_HANDLE handle = nullptr;
  ~Key() { if (handle) BCryptDestroyKey(handle); }
  Key() = default;
  Key(const Key&) = delete;
  Key& operator=(const Key&) = delete;
};
struct DerivedKey {
  std::array<uint8_t, 32> bytes{};
  ~DerivedKey() { SecureZeroMemory(bytes.data(), bytes.size()); }
};
struct WipeBuffer {
  BackupBytes bytes;
  explicit WipeBuffer(size_t size) : bytes(size) {}
  ~WipeBuffer() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
};
void Put32(BackupBytes& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
uint32_t Get32(const BackupBytes& bytes, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
  return value;
}
void Crypt(bool encrypt, const BackupBytes& header, const uint8_t* input, ULONG length,
           uint8_t* output, uint8_t* tag, const SensitiveBytes& password) {
  Algorithm prf, aes;
  Checked(BCryptOpenAlgorithmProvider(&prf.handle, BCRYPT_SHA256_ALGORITHM, nullptr,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG));
  DerivedKey derived;
  Checked(BCryptDeriveKeyPBKDF2(prf.handle,
      const_cast<PUCHAR>(password.bytes().data()), static_cast<ULONG>(password.bytes().size()),
      const_cast<PUCHAR>(header.data() + 24), 16, kIterations, derived.bytes.data(),
      static_cast<ULONG>(derived.bytes.size()), 0));
  Checked(BCryptOpenAlgorithmProvider(&aes.handle, BCRYPT_AES_ALGORITHM, nullptr, 0));
  Checked(BCryptSetProperty(aes.handle, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0));
  DWORD object_size = 0, returned = 0;
  Checked(BCryptGetProperty(aes.handle, BCRYPT_OBJECT_LENGTH,
      reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0));
  Require(returned == sizeof(object_size) && object_size > 0 && object_size <= 1024 * 1024,
          "Invalid backup key object size");
  WipeBuffer object(object_size);
  Key key; // Destroy CNG key before wiping/freeing its object buffer.
  Checked(BCryptGenerateSymmetricKey(aes.handle, &key.handle, object.bytes.data(), object_size,
      derived.bytes.data(), static_cast<ULONG>(derived.bytes.size()), 0));
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(header.data() + 40); info.cbNonce = 12;
  info.pbAuthData = const_cast<PUCHAR>(header.data()); info.cbAuthData = static_cast<ULONG>(kHeader);
  info.pbTag = tag; info.cbTag = static_cast<ULONG>(kTag);
  ULONG written = 0;
  const auto status = encrypt
      ? BCryptEncrypt(key.handle, const_cast<PUCHAR>(input), length, &info, nullptr, 0,
                      output, length, &written, 0)
      : BCryptDecrypt(key.handle, const_cast<PUCHAR>(input), length, &info, nullptr, 0,
                      output, length, &written, 0);
  // On authentication failure the caller's private plaintext buffer is wiped;
  // no bytes are returned. Do not log input, passwords, keys, or plaintext.
  Require(status >= 0 && written == length, "Backup password is incorrect or data could not be authenticated");
}
}  // namespace

void ValidateBackupPassword(const SensitiveBytes& password) {
  Require(!password.bytes().empty() && password.bytes().size() <= 1024,
          "Backup password must contain 1 to 1024 bytes");
}
SensitiveBytes::SensitiveBytes(BackupBytes&& bytes) noexcept { bytes_.swap(bytes); }
SensitiveBytes::~SensitiveBytes() {
  if (!bytes_.empty()) SecureZeroMemory(bytes_.data(), bytes_.size());
}
SensitiveBytes::SensitiveBytes(SensitiveBytes&& other) noexcept { bytes_.swap(other.bytes_); }
SensitiveBytes& SensitiveBytes::operator=(SensitiveBytes&& other) noexcept {
  if (this != &other) {
    if (!bytes_.empty()) SecureZeroMemory(bytes_.data(), bytes_.size());
    bytes_.clear(); bytes_.swap(other.bytes_);
  }
  return *this;
}
BackupBytes SealBackup(const SensitiveBytes& payload, const SensitiveBytes& password) {
  ValidateBackupPassword(password);
  Require(!payload.bytes().empty() && payload.bytes().size() <= kBackupPayloadLimit,
          "Backup payload must contain 1 byte to 16 MiB");
  BackupBytes packet(kPrefix + payload.bytes().size());
  std::copy(kMagic.begin(), kMagic.end(), packet.begin());
  Put32(packet, 8, 1); Put32(packet, 12, 1); Put32(packet, 16, kIterations);
  Put32(packet, 20, static_cast<uint32_t>(payload.bytes().size()));
  Checked(BCryptGenRandom(nullptr, packet.data() + 24, 28, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
  Crypt(true, packet, payload.bytes().data(), static_cast<ULONG>(payload.bytes().size()),
        packet.data() + kPrefix, packet.data() + kHeader, password);
  return packet;
}
SensitiveBytes OpenBackup(const BackupBytes& packet, const SensitiveBytes& password) {
  ValidateBackupPassword(password);
  Require(packet.size() >= kPrefix && std::equal(kMagic.begin(), kMagic.end(), packet.begin()),
          "Invalid backup envelope");
  Require(Get32(packet, 8) == 1 && Get32(packet, 12) == 1 && Get32(packet, 16) == kIterations,
          "Unsupported backup version or cryptographic parameters");
  const auto length = Get32(packet, 20);
  Require(length > 0 && length <= kBackupPayloadLimit && packet.size() == kPrefix + length,
          "Invalid backup envelope length");
  WipeBuffer plaintext(length);
  // CNG's tag pointer has a mutable API type; use a copy for the borrowed packet.
  std::array<uint8_t, kTag> tag{};
  std::copy_n(packet.data() + kHeader, kTag, tag.data());
  Crypt(false, packet, packet.data() + kPrefix, length, plaintext.bytes.data(), tag.data(), password);
  return SensitiveBytes(std::move(plaintext.bytes));
}
}  // namespace gbrowser
