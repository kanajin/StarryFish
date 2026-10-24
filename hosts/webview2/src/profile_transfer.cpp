#include "profile_transfer.h"
#include "core.h"
#include "user_state.h"
#include "character_shortcuts.h"
#include "proxy_preference.h"
#include "directory_pin.h"
#include "zero_initialization.h"
#include <bcrypt.h>
#include <shobjidl.h>
#include <wrl.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace gbrowser::webview::transfer {
using namespace winrt::Windows::Data::Json;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
void Require(bool ok) { if (!ok) throw std::runtime_error("资料迁移未完成；文件、密码或运行时响应无效"); }
void Hr(HRESULT hr) { Require(SUCCEEDED(hr)); }
void Nt(NTSTATUS status) { Require(status >= 0); }
struct Algorithm { BCRYPT_ALG_HANDLE h = nullptr; ~Algorithm() { if (h) BCryptCloseAlgorithmProvider(h, 0); } };
struct Key { BCRYPT_KEY_HANDLE h = nullptr; ~Key() { if (h) BCryptDestroyKey(h); } };
struct Secret {
  std::vector<unsigned char> bytes;
  ~Secret() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
};
struct File { HANDLE h; ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
constexpr char kMagic[] = "SFPACK01";
constexpr size_t kHeader = 8 + 16 + 12 + 16;
std::string Crypt(const std::string& input, const std::wstring& password, bool encrypt) {
  Require(password.size() >= 12 && password.size() <= 256);
  Require(encrypt ? (!input.empty() && input.size() <= kMaximumBytes) :
      (input.size() > kHeader && input.size() <= kMaximumBytes + kHeader && input.compare(0, 8, kMagic, 8) == 0));
  std::array<unsigned char, kHeader> header{};
  if (encrypt) {
    memcpy(header.data(), kMagic, 8);
    Nt(BCryptGenRandom(nullptr, header.data() + 8, 28, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
  } else memcpy(header.data(), input.data(), kHeader);
  Algorithm hash, aes;
  Nt(BCryptOpenAlgorithmProvider(&hash.h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
  Secret key; key.bytes.resize(32);
  Nt(BCryptDeriveKeyPBKDF2(hash.h, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(password.data())),
      static_cast<ULONG>(password.size() * sizeof(wchar_t)), header.data() + 8, 16, 600000,
      key.bytes.data(), 32, 0));
  Nt(BCryptOpenAlgorithmProvider(&aes.h, BCRYPT_AES_ALGORITHM, nullptr, 0));
  Nt(BCryptSetProperty(aes.h, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
  Key cipher; Nt(BCryptGenerateSymmetricKey(aes.h, &cipher.h, nullptr, 0, key.bytes.data(), 32, 0));
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = header.data() + 24; info.cbNonce = 12;
  info.pbTag = header.data() + 36; info.cbTag = 16;
  info.pbAuthData = header.data(); info.cbAuthData = 24;
  const auto count = static_cast<ULONG>(input.size() - (encrypt ? 0 : kHeader));
  std::string output(count, '\0'); ULONG written = 0;
  auto bytes = reinterpret_cast<PUCHAR>(const_cast<char*>(input.data() + (encrypt ? 0 : kHeader)));
  const auto status = encrypt ? BCryptEncrypt(cipher.h, bytes, count, &info, nullptr, 0,
      reinterpret_cast<PUCHAR>(output.data()), count, &written, 0) :
      BCryptDecrypt(cipher.h, bytes, count, &info, nullptr, 0, reinterpret_cast<PUCHAR>(output.data()), count, &written, 0);
  if (status < 0) { SecureZeroMemory(output.data(), output.size()); Require(false); }
  Require(written == count);
  return encrypt ? std::string(reinterpret_cast<char*>(header.data()), kHeader) + output : output;
}
void StringMap(const JsonObject& value) {
  Require(value.Size() <= 16384);
  for (const auto& pair : value) Require(pair.Key().size() <= 8192 && pair.Value().ValueType() == JsonValueType::String);
}
constexpr const wchar_t* kStates[]{L"window-layout.state", L"game-panes.state", L"tarou-window.state",
  L"appearance.state", L"layout-choice.state", L"loading-display.state", L"static-cache.state", L"shortcut-keys.state", L"skill-overlay.state"};
}
std::string Encrypt(const std::string& plain, const std::wstring& password) { return Crypt(plain, password, true); }
std::string Decrypt(const std::string& archive, const std::wstring& password) { return Crypt(archive, password, false); }
std::string ReadArchive(const std::filesystem::path& path) {
  File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
  Require(file.h != INVALID_HANDLE_VALUE);
  BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
  Require(GetFileType(file.h) == FILE_TYPE_DISK && GetFileInformationByHandle(file.h, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && info.nNumberOfLinks == 1 &&
      GetFileSizeEx(file.h, &size) && size.QuadPart > static_cast<LONGLONG>(kHeader) && size.QuadPart <= kMaximumBytes + kHeader);
  std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
  Require(ReadFile(file.h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size()); return bytes;
}
void WriteArchive(const std::filesystem::path& path, const std::string& archive) {
  Require(archive.size() <= kMaximumBytes + kHeader);
  // Never replace an earlier backup. An interrupted new file remains identifiable
  // by its failed authentication; no plaintext staging file exists.
  File file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
  Require(file.h != INVALID_HANDLE_VALUE); DWORD written = 0;
  Require(WriteFile(file.h, archive.data(), static_cast<DWORD>(archive.size()), &written, nullptr) &&
      written == archive.size() && FlushFileBuffers(file.h));
}
void Validate(const JsonObject& document) {
  Require(document.GetNamedNumber(L"version") == 1 && document.Stringify().size() <= kMaximumBytes / 2);
  Require(document.GetNamedString(L"name").size() <= 64);
  const auto settings = document.GetNamedObject(L"settings"); Require(settings.Size() == std::size(kStates));
  for (auto name : kStates) {
    const auto bytes = WideToUtf8(std::wstring(settings.GetNamedString(name))); Require(bytes.size() <= 65536);
    if (std::wstring_view(name) == L"window-layout.state" || std::wstring_view(name) == L"tarou-window.state") DecodeWindowState(bytes);
    else if (std::wstring_view(name) == L"game-panes.state") DecodePaneState(bytes);
    else if (std::wstring_view(name) == L"appearance.state") DecodeDarkAppearance(bytes);
    else if (std::wstring_view(name) == L"layout-choice.state") DecodeBrowserLayout(bytes);
    else if (std::wstring_view(name) == L"loading-display.state") DecodeHiddenLoading(bytes);
    else if (std::wstring_view(name) == L"shortcut-keys.state") characters::Decode(bytes);
    else if (std::wstring_view(name) == L"static-cache.state") Require(bytes == "STARRYFISH-CACHE-ON\n" || bytes == "STARRYFISH-CACHE-OFF\n");
    else Require(bytes == "ON\n" || bytes == "OFF\n");
  }
  DecodeProxyPreference(WideToUtf8(std::wstring(document.GetNamedString(L"proxy"))));
  const auto extensions = document.GetNamedArray(L"extensions"); Require(extensions.Size() == 4);
  for (const auto& item : extensions) {
    const auto value = item.GetObject(); value.GetNamedObject(L"local"); value.GetNamedObject(L"sync"); StringMap(value.GetNamedObject(L"page"));
  }
  const auto sites = document.GetNamedObject(L"sites"); Require(sites.Size() == 6);
  for (const auto& site : sites) {
    const auto origin = std::wstring(site.Key());
    Require(origin == L"https://game.granbluefantasy.jp" || origin == L"http://game.granbluefantasy.jp" ||
        origin == L"https://gbf.game.mbga.jp" || origin == L"http://gbf.game.mbga.jp" ||
        origin == L"https://connect.mobage.jp" || origin == L"https://ssl.sp.mbga.jp");
    StringMap(site.Value().GetObject());
  }
  const auto cookies = document.GetNamedArray(L"cookies"); Require(cookies.Size() <= 10000);
  for (const auto& item : cookies) {
    const auto cookie = item.GetObject(); Require(cookie.GetNamedString(L"name").size() <= 4096 && cookie.GetNamedString(L"value").size() <= 65536);
    Require(cookie.GetNamedString(L"domain").size() <= 253 && cookie.GetNamedString(L"path").size() <= 8192);
    const auto same_site = cookie.GetNamedNumber(L"sameSite");
    Require(same_site >= 0 && same_site <= 2 && std::floor(same_site) == same_site);
    cookie.GetNamedNumber(L"expires"); cookie.GetNamedBoolean(L"secure"); cookie.GetNamedBoolean(L"httpOnly"); cookie.GetNamedBoolean(L"session");
  }
}

JsonObject ConfigurationOnly(const JsonObject& document) {
  Validate(document);
  auto result = JsonObject::Parse(document.Stringify());
  result.Insert(L"cookies", JsonArray{});
  return result;
}

std::string PrepareImport(ProfileCatalogue& profiles, const std::filesystem::path& installation,
    std::wstring_view leaf, const std::string& previous, const JsonObject& document) {
  Validate(document);
  auto base = std::wstring(document.GetNamedString(L"name")).substr(0, 48);
  // Do not split a UTF-16 surrogate pair when shortening a display name.
  if (!base.empty() && base.back() >= 0xd800 && base.back() <= 0xdbff) base.pop_back();
  std::wstring name;
  for (unsigned n = 1; n <= 65; ++n) {
    name = base + (n == 1 ? L"（导入）" : L"（导入 " + std::to_wstring(n) + L"）");
    if (std::none_of(profiles.state().entries.begin(), profiles.state().entries.end(),
        [&](const auto& entry) { return entry.name == name; })) break;
  }
  const auto id = profiles.Create(name);
  try {
    const auto target = ProfileDataPath(installation, leaf, id);
    RequireSafePath(target); DirectoryPins pins; pins.CreateTree(target); pins.CreateTree(target / L"HostSettings");
    for (auto pair : document.GetNamedObject(L"settings"))
      UserStateFile(target / L"HostSettings" / pair.Key().c_str()).Save(WideToUtf8(std::wstring(pair.Value().GetString())));
    UserStateFile(target / L"proxy-choice.state").Save(WideToUtf8(std::wstring(document.GetNamedString(L"proxy"))));
    ZeroInitialization zero(target); zero.Prepare(true); zero.Begin();
    return id;
  } catch (...) { try { profiles.Retire(id, previous); } catch (...) {} throw; }
}

namespace {
struct PasswordPrompt { HWND edit = nullptr, confirm = nullptr; bool done = false, accepted = false, importing = false; std::wstring password; };
LRESULT CALLBACK PasswordProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto p = reinterpret_cast<PasswordPrompt*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) { p = static_cast<PasswordPrompt*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p)); }
  if (p && msg == WM_COMMAND && LOWORD(wp) == IDOK) {
    std::array<wchar_t, 257> a{}, b{}; GetWindowTextW(p->edit, a.data(), 257); GetWindowTextW(p->confirm, b.data(), 257);
    if (wcslen(a.data()) < 12 || (!p->importing && wcscmp(a.data(), b.data())))
      MessageBoxW(hwnd, L"密码至少 12 个字符，导出时两次输入须一致。", L"迁移密码", MB_OK);
    else { p->password = a.data(); p->accepted = true; p->done = true; }
    SecureZeroMemory(a.data(), sizeof(a)); SecureZeroMemory(b.data(), sizeof(b)); return 0;
  }
  if (p && (msg == WM_CLOSE || (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL))) { p->done = true; return 0; }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
}
bool Choose(HWND owner, bool importing, Request& request) {
  ComPtr<IFileDialog> dialog;
  Hr(CoCreateInstance(importing ? CLSID_FileOpenDialog : CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
  DWORD flags = 0; Hr(dialog->GetOptions(&flags)); Hr(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR));
  COMDLG_FILTERSPEC filter{L"StarryFish 加密资料", L"*.sfprofile"}; Hr(dialog->SetFileTypes(1, &filter));
  Hr(dialog->SetDefaultExtension(L"sfprofile")); Hr(dialog->SetTitle(importing ? L"导入为新资料" : L"导出当前资料（请选择新的文件名）"));
  if (dialog->Show(owner) == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
  ComPtr<IShellItem> item; Hr(dialog->GetResult(&item)); PWSTR path = nullptr; Hr(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
  request.path = path; CoTaskMemFree(path);
  PasswordPrompt prompt; prompt.importing = importing;
  WNDCLASSW cls{}; cls.lpfnWndProc = PasswordProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"StarryFishTransferPassword";
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
  HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"资料迁移密码", WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 540, importing ? 225 : 265, owner, nullptr, cls.hInstance, &prompt); Require(window != nullptr);
  CreateWindowW(L"STATIC", importing ? L"请输入导出时的密码。导入会新建资料并应用窗口设置。" : L"文件包含登录态，请设置至少 12 字符的密码并妥善保管。\n密码无法找回；请勿将资料文件和密码一起分享。",
      WS_CHILD | WS_VISIBLE, 16, 12, 490, 48, window, nullptr, cls.hInstance, nullptr);
  prompt.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
      16, 68, 490, 28, window, nullptr, cls.hInstance, nullptr); SendMessageW(prompt.edit, EM_SETLIMITTEXT, 256, 0);
  prompt.confirm = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL | (importing ? 0 : WS_VISIBLE),
      16, 104, 490, 28, window, nullptr, cls.hInstance, nullptr); SendMessageW(prompt.confirm, EM_SETLIMITTEXT, 256, 0);
  CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 280, importing ? 124 : 160, 100, 30, window, reinterpret_cast<HMENU>(IDOK), cls.hInstance, nullptr);
  CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 400, importing ? 124 : 160, 100, 30, window, reinterpret_cast<HMENU>(IDCANCEL), cls.hInstance, nullptr);
  EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); SetFocus(prompt.edit);
  MSG msg{};
  while (!prompt.done) {
    const auto result = GetMessageW(&msg, nullptr, 0, 0);
    if (result <= 0) { if (!result) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
    if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  EnableWindow(owner, TRUE); DestroyWindow(window); SetForegroundWindow(owner);
  request.password = std::move(prompt.password); return prompt.accepted;
}
}
