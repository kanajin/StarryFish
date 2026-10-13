#include "component_catalogue.h"
#include "components.h"
#include "core.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gbrowser::webview {
namespace {
void Need(bool value) { if (!value) throw std::runtime_error("安装组件身份不完整或已改变；保留现有组件，需明确重新核对"); }
std::string Entropy(const std::filesystem::path& root) {
  auto name = root.native();
  const auto length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, name.data(),
      static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr, 0);
  Need(length > 0); std::wstring lowered(static_cast<size_t>(length), L'\0');
  Need(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, name.data(), static_cast<int>(name.size()),
      lowered.data(), length, nullptr, nullptr, 0) == length);
  return "GbrowserWebView2.SourceCatalogue.v1\n" + WideToUtf8(lowered);
}
}
ComponentCatalogue::ComponentCatalogue(const std::filesystem::path& application) {
  static_assert(std::size(kComponents) == 3);
  for (size_t index = 0; index < entries_.size(); ++index) {
    const auto& value = kComponents[index];
    entries_[index] = {value.name, value.entry, value.tree, value.files, value.bytes};
  }
  const auto path = application / L"component-catalogue.bin";
  RequireSafePath(path);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) return;
  Need(file != INVALID_HANDLE_VALUE);
  try {
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    Need(GetFileInformationByHandle(file, &info) && GetFileType(file) == FILE_TYPE_DISK &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        info.nNumberOfLinks == 1 && GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 16384);
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart)); DWORD read = 0;
    Need(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    auto entropy = Entropy(application);
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB output{};
    Need(CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE);
    std::unique_ptr<BYTE, decltype(&LocalFree)> held(output.pbData, LocalFree);
    constexpr size_t width = 64 + 4 + 4;
    Need(output.cbData == 7 + width * 3 && std::memcmp(output.pbData, "GBWV01\n", 7) == 0);
    for (size_t index = 0; index < entries_.size(); ++index) {
      const auto item = output.pbData + 7 + index * width;
      auto& entry = entries_[index]; entry.tree.assign(reinterpret_cast<const char*>(item), 64);
      Need(std::all_of(entry.tree.begin(), entry.tree.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
      }));
      uint32_t count = 0, length = 0; std::memcpy(&count, item + 64, 4); std::memcpy(&length, item + 68, 4);
      Need(count > 0 && count <= 1024 && length <= 32 * 1024 * 1024);
      entry.files = count; entry.bytes = length;
      // ZeroOmega is fixed offline product code. Installation cannot replace
      // its identity, page or permissions by supplying another catalogue.
      if (index == 0) Need(entry.tree == kComponents[0].tree && entry.files == kComponents[0].files && entry.bytes == kComponents[0].bytes);
    }
    handle_ = file;
  } catch (...) { CloseHandle(file); throw; }
}
ComponentCatalogue::~ComponentCatalogue() { if (handle_) CloseHandle(static_cast<HANDLE>(handle_)); }

ComponentRegistrations::ComponentRegistrations(std::filesystem::path application, std::filesystem::path data)
    : entropy_(Entropy(application) + "\nLoadedRegistration.v1\n" + Entropy(data)), data_(std::move(data)) {
  RequireSafePath(application); RequireSafePath(data_);
}
std::filesystem::path ComponentRegistrations::Path(size_t role) const {
  Need(role >= 1 && role <= 3);
  constexpr const wchar_t* names[]{L"loaded-tarou.bin", L"loaded-arcarum.bin", L"loaded-scripts.bin"};
  return data_ / names[role - 1];
}
bool ComponentRegistrations::Matches(size_t role, const std::string& tree) const {
  const auto path = Path(role);
  auto pending = path; pending += L".pending";
  RequireSafePath(path); RequireSafePath(pending);
  // A publication with an unknown outcome is not an absent registration.
  Need(GetFileAttributesW(pending.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) return false;
  Need(file != INVALID_HANDLE_VALUE);
  struct File { HANDLE value; ~File() { CloseHandle(value); } } held{file};
  BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
  Need(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
      info.nNumberOfLinks == 1 && GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 16384);
  std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart)); DWORD read = 0;
  Need(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
  DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
  DATA_BLOB extra{static_cast<DWORD>(entropy_.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy_.data()))};
  DATA_BLOB output{};
  Need(CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE);
  std::unique_ptr<BYTE, decltype(&LocalFree)> plain(output.pbData, LocalFree);
  Need(output.cbData == 72 && std::memcmp(output.pbData, "GBWR01\n", 7) == 0 && output.pbData[7] == role);
  const std::string loaded(reinterpret_cast<const char*>(output.pbData + 8), 64);
  Need(std::all_of(loaded.begin(), loaded.end(), [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }));
  return loaded == tree;
}
void ComponentRegistrations::Record(size_t role, const std::string& tree) const {
  const auto path = Path(role);
  Need(tree.size() == 64 && std::all_of(tree.begin(), tree.end(), [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }));
  std::array<BYTE, 72> bytes{};
  std::memcpy(bytes.data(), "GBWR01\n", 7); bytes[7] = static_cast<BYTE>(role); std::memcpy(bytes.data() + 8, tree.data(), 64);
  DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
  DATA_BLOB extra{static_cast<DWORD>(entropy_.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy_.data()))};
  DATA_BLOB output{};
  Need(CryptProtectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE);
  std::unique_ptr<BYTE, decltype(&LocalFree)> encrypted(output.pbData, LocalFree);
  auto pending = path; pending += L".pending";
  RequireSafePath(path); RequireSafePath(pending);
  HANDLE file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  Need(file != INVALID_HANDLE_VALUE);
  DWORD written = 0;
  const bool ready = WriteFile(file, output.pbData, output.cbData, &written, nullptr) && written == output.cbData && FlushFileBuffers(file);
  const auto closed = CloseHandle(file);
  Need(ready && closed);
  // A failed publication keeps the pending evidence; never automatically
  // delete it, infer success or reinstall again after an unknown outcome.
  Need(MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE);
  Need(Matches(role, tree));
}
}
