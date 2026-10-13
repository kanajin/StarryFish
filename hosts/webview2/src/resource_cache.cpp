#include "resource_cache.h"
#include "core.h"
#include "file_digest.h"
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace gbrowser::webview::cache {
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("static cache unavailable"); }
std::string Lower(std::string text) {
  for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return text;
}
std::string Trimmed(std::string s) {
  const auto first = s.find_first_not_of(" \t"), last = s.find_last_not_of(" \t");
  return first == s.npos ? "" : s.substr(first, last - first + 1);
}
std::string Header(const Headers& headers, const std::string& name) {
  std::string result;
  for (const auto& h : headers) if (Lower(h.first) == name) {
    if (!result.empty()) result += ',';
    result += Trimmed(h.second);
  }
  return result;
}
bool Has(const Headers& headers, const std::string& name) {
  return std::any_of(headers.begin(), headers.end(), [&](const auto& h) { return Lower(h.first) == name; });
}
std::optional<uint64_t> Number(const std::string& text) {
  if (text.empty() || text.size() > 12) return {};
  uint64_t n = 0;
  for (char c : text) { if (c < '0' || c > '9') return {}; n = n * 10 + c - '0'; }
  return n;
}
std::optional<uint64_t> HttpDate(const std::string& text) {
  SYSTEMTIME system{};
  FILETIME file{};
  if (!WinHttpTimeToSystemTime(Utf8ToWide(text).c_str(), &system) || !SystemTimeToFileTime(&system, &file)) return {};
  const uint64_t ticks = (static_cast<uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
  if (ticks < 116444736000000000ULL) return {};
  return (ticks - 116444736000000000ULL) / 10000000;
}
std::string Digest(const std::string& bytes) {
  FileDigest digest; digest.Add(bytes.data(), bytes.size()); return digest.Finish();
}
bool Hex(const std::string& s) {
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == s.npos;
}
bool StaticUrl(const std::string& url) {
  if (url.size() > 4096 || url.compare(0, 8, "https://") != 0) return false;
  const auto start = url.find('/', 8);
  if (start == url.npos) return false;
  const auto host = url.substr(8, start - 8);
  if (host != "prd-game-a-granbluefantasy.akamaized.net" && host != "game.granbluefantasy.jp" && host != "gbf.game.mbga.jp") return false;
  auto path = url.substr(start);
  if (path.compare(0, 8, "/assets/") != 0 && path.compare(0, 11, "/assets_en/") != 0) return false;
  // No URL normalization or query stripping: versioned URLs remain distinct.
  // Queries are restricted to public asset versions, never account tokens.
  const auto query = path.find('?');
  if (query != path.npos) {
    auto version = path.substr(query + 1); path.resize(query);
    for (const auto* prefix : {"v=", "ver=", "version=", "t="})
      if (version.compare(0, strlen(prefix), prefix) == 0) { version.erase(0, strlen(prefix)); break; }
    if (version.empty() || version.size() > 64 || version.find_first_not_of("0123456789abcdefABCDEF._-") != version.npos) return false;
  }
  if (path.find("..") != path.npos || path.find("//") != path.npos ||
      path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.") != path.npos) return false;
  const auto dot = path.rfind('.'); if (dot == path.npos) return false;
  const auto extension = Lower(path.substr(dot));
  for (const auto* allowed : {".png", ".jpg", ".jpeg", ".webp", ".gif", ".js", ".css", ".woff", ".woff2", ".mp3", ".ogg"})
    if (extension == allowed) return true;
  return false;
}
struct File {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void Regular(HANDLE file) {
  BY_HANDLE_FILE_INFORMATION info{};
  Require(file != INVALID_HANDLE_VALUE && GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) && info.nNumberOfLinks == 1);
}
std::optional<std::string> Read(const std::filesystem::path& path) {
  File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) return {};
  Regular(file.value);
  LARGE_INTEGER size{}; Require(GetFileSizeEx(file.value, &size) && size.QuadPart > 0 && size.QuadPart <= kBodyLimit + 32768);
  std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD count = 0;
  Require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size());
  return bytes;
}
uint64_t LastHit(HANDLE file) {
  FILETIME time{}; Require(GetFileTime(file, nullptr, nullptr, &time) != FALSE);
  const uint64_t ticks = (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  return ticks < 116444736000000000ULL ? 0 : (ticks - 116444736000000000ULL) / 10000000;
}
void SetLastHit(HANDLE file, uint64_t now) {
  Require(now <= 32503680000ULL);
  const uint64_t ticks = now * 10000000 + 116444736000000000ULL;
  FILETIME time{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  Require(SetFileTime(file, nullptr, nullptr, &time) != FALSE);
}
bool Idle(uint64_t used, uint64_t now) { return now < used || now - used > kIdleLimit; }
void DeleteRegular(const std::filesystem::path& path) {
  File file{CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) return;
  Regular(file.value); FILE_DISPOSITION_INFO disposition{TRUE};
  Require(SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE);
}
std::string Encode(const Entry& e) {
  const auto payload = std::to_string(e.stored) + " " + std::to_string(e.expires) + " " + std::to_string(e.url.size()) + " " +
      std::to_string(e.headers.size()) + " " + std::to_string(e.body.size()) + "\n" + e.url + e.headers + e.body;
  return "STARRYFISH-CACHE-1\n" + Digest(payload) + "\n" + payload;
}
Entry Decode(const std::string& bytes) {
  constexpr std::string_view magic = "STARRYFISH-CACHE-1\n";
  const auto separator = magic.size() + 64;
  Require(bytes.compare(0, magic.size(), magic) == 0 && bytes.size() > separator + 1 && bytes[separator] == '\n');
  const auto payload = bytes.substr(separator + 1); Require(Digest(payload) == bytes.substr(magic.size(), 64));
  const auto newline = payload.find('\n'); Require(newline != payload.npos && newline < 150);
  std::istringstream fields(payload.substr(0, newline)); Entry e; size_t u = 0, h = 0, b = 0;
  Require(static_cast<bool>(fields >> e.stored >> e.expires >> u >> h >> b) && u <= 4096 && h <= 16384 && b > 0 && b <= kBodyLimit &&
      newline + 1 + u + h + b == payload.size());
  auto offset = newline + 1; e.url = payload.substr(offset, u); offset += u;
  e.headers = payload.substr(offset, h); offset += h; e.body = payload.substr(offset, b);
  Require(StaticUrl(e.url) && e.expires > e.stored && e.expires - e.stored <= 7 * 86400 && Encode(e) == bytes);
  return e;
}
}
uint64_t Now() {
  FILETIME file{}; GetSystemTimeAsFileTime(&file);
  return (((static_cast<uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime) - 116444736000000000ULL) / 10000000;
}
bool GameDocument(const std::string& url) {
  for (const auto* prefix : {"https://game.granbluefantasy.jp/", "https://gbf.game.mbga.jp/"})
    if (url.compare(0, strlen(prefix), prefix) == 0) return true;
  return false;
}
bool Eligible(const Request& r) {
  if (r.method != "GET" || !StaticUrl(r.url)) return false;
  for (const auto* h : {"authorization", "proxy-authorization", "cookie", "range", "if-range", "if-none-match", "if-modified-since", "cache-control", "pragma"})
    if (Has(r.headers, h)) return false;
  return true;
}
std::optional<Entry> Admit(const Request& request, int status, const Headers& headers, uint64_t now) {
  if (!Eligible(request) || status != 200) return {};
  for (const auto* h : {"set-cookie", "set-cookie2", "vary", "content-range", "content-disposition", "www-authenticate", "pragma"})
    if (Has(headers, h)) return {};
  const auto encoding = Lower(Header(headers, "content-encoding"));
  if (!encoding.empty() && encoding != "identity") return {}; // GetContent compression representation is not assumed.
  const auto mime = Lower(Header(headers, "content-type"));
  bool type = false;
  for (const auto* prefix : {"image/png", "image/jpeg", "image/webp", "image/gif", "text/css", "text/javascript", "application/javascript", "font/woff", "font/woff2", "application/font-woff", "audio/mpeg", "audio/ogg"})
    if (mime == prefix || mime.compare(0, strlen(prefix) + 1, std::string(prefix) + ";") == 0) type = true;
  if (!type) return {};
  const auto path = request.url.substr(0, request.url.find('?'));
  const auto suffix = Lower(path.substr(path.rfind('.')));
  if ((suffix == ".js" && mime.find("javascript") == mime.npos) ||
      (suffix == ".css" && mime.compare(0, 8, "text/css") != 0) ||
      ((suffix == ".png" || suffix == ".jpg" || suffix == ".jpeg" || suffix == ".webp" || suffix == ".gif") && mime.compare(0, 6, "image/") != 0) ||
      ((suffix == ".woff" || suffix == ".woff2") && mime.find("woff") == mime.npos) ||
      ((suffix == ".mp3" || suffix == ".ogg") && mime.compare(0, 6, "audio/") != 0)) return {};
  const auto length = Number(Header(headers, "content-length"));
  if (!length || !*length || *length > kBodyLimit) return {};
  std::optional<uint64_t> ttl, shared_ttl; bool is_public = false;
  std::istringstream directives(Lower(Header(headers, "cache-control"))); std::string directive;
  while (std::getline(directives, directive, ',')) {
    directive = Trimmed(directive);
    if (directive == "public") is_public = true;
    if (directive.compare(0, 8, "no-cache") == 0 || directive.compare(0, 8, "no-store") == 0 || directive.compare(0, 7, "private") == 0) return {};
    if (directive.compare(0, 8, "max-age=") == 0) {
      if (ttl) return {}; ttl = Number(directive.substr(8)); if (!ttl) return {};
    }
    if (directive.compare(0, 9, "s-maxage=") == 0) {
      if (shared_ttl) return {}; shared_ttl = Number(directive.substr(9)); if (!shared_ttl) return {};
    }
  }
  if (shared_ttl) ttl = shared_ttl;
  const auto date = HttpDate(Header(headers, "date"));
  const auto age = Has(headers, "age") ? Number(Header(headers, "age")) : std::optional<uint64_t>(0);
  if (!is_public || !ttl || !date || !age || *date > now + 300) return {};
  const auto elapsed = (std::max)(*age, now > *date ? now - *date : 0);
  if (*ttl <= elapsed) return {};
  Entry result; result.url = request.url; result.stored = now;
  result.expected_length = static_cast<size_t>(*length);
  result.expires = now + (std::min)(*ttl - elapsed, uint64_t{7 * 86400});
  for (const auto& header : headers) {
    const auto name = Lower(header.first);
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != name.npos ||
        header.second.find_first_of("\r\n\0", 0, 3) != header.second.npos) return {};
    for (unsigned char c : header.second) if (c < 32 && c != '\t') return {};
    if (name == "content-length" || name == "content-encoding" || name == "cache-control" || name == "age" ||
        name == "connection" || name == "transfer-encoding" || name == "keep-alive" || name == "trailer" || name == "upgrade") continue;
    result.headers += header.first + ": " + header.second + "\r\n";
  }
  if (result.headers.size() > 16384) return {};
  return result;
}
std::string ReplayHeaders(const Entry& entry, uint64_t now) {
  if (now < entry.stored || now >= entry.expires) return {};
  // Do not create another independent freshness window in Chromium's disk cache.
  return entry.headers + "Cache-Control: no-store\r\nContent-Length: " + std::to_string(entry.body.size()) + "\r\n";
}
DiskCache::DiskCache(const std::filesystem::path& root, uint64_t limit, uint64_t now) : root_(root), limit_(limit) {
  pins_.CreateTree(root_);
  size_t seen = 0;
  for (const auto& file : std::filesystem::directory_iterator(root_)) {
    Require(++seen <= 240000);
    const auto name = file.path().filename().string();
    if (name.size() != 68 || !Hex(name.substr(0, 64))) continue;
    if (name.substr(64) == ".tmp") { DeleteRegular(file.path()); continue; }
    if (name.substr(64) != ".sfc") continue;
    // Attribute queries must not follow a link, even during inventory.
    File handle{CreateFileW(file.path().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    Regular(handle.value); LARGE_INTEGER size{};
    Require(GetFileSizeEx(handle.value, &size) && size.QuadPart >= 0 && size.QuadPart <= kBodyLimit + 32768);
    items_[name.substr(0, 64)] = {static_cast<uint64_t>(size.QuadPart), LastHit(handle.value), ++next_generation_}; stats_.bytes += size.QuadPart;
  }
  stats_.files = items_.size(); PurgeIdle(now); Trim(0); stats_.available = true;
}
void DiskCache::Remove(const std::string& key) {
  const auto found = items_.find(key); if (found == items_.end()) return;
  DeleteRegular(root_ / (key + ".sfc")); stats_.bytes -= found->second.size; items_.erase(found); stats_.files = items_.size();
}
void DiskCache::PurgeIdle(uint64_t now) {
  for (auto it = items_.begin(); it != items_.end();) {
    const auto current = it++;
    if (Idle(current->second.used, now)) {
      try { Remove(current->first); } catch (...) { ++stats_.errors; }
    }
  }
}
void DiskCache::Hit(const std::string& url, uint64_t generation, uint64_t now) {
  const auto key = Digest(url); const auto found = items_.find(key);
  // Only the host's successful replay acknowledges a hit. A late completion
  // after clear/replacement cannot revive or touch another cache object.
  if (found == items_.end() || found->second.generation != generation || now < found->second.used) return;
  File file{CreateFileW((root_ / (key + ".sfc")).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
  Regular(file.value); SetLastHit(file.value, now); found->second.used = now;
}
void DiskCache::Trim(uint64_t incoming) {
  while (!items_.empty() && (stats_.bytes + incoming > limit_ || items_.size() >= 120000)) {
    auto oldest = std::min_element(items_.begin(), items_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
    Remove(oldest->first);
  }
}
std::optional<Entry> DiskCache::Lookup(const std::string& url, uint64_t now) {
  const auto key = Digest(url);
  try {
    const auto found = items_.find(key);
    if (found != items_.end()) {
      const auto bytes = Read(root_ / (key + ".sfc"));
      if (bytes) {
        auto entry = Decode(*bytes);
        if (entry.url == url && !Idle(found->second.used, now) && now >= entry.stored && now < entry.expires) {
          entry.generation = found->second.generation; return entry;
        }
      }
      Remove(key);
    }
  } catch (...) { ++stats_.errors; }
  ++stats_.misses; return {};
}
void DiskCache::Store(const Entry& entry, uint64_t now) {
  if (entry.body.empty() || entry.body.size() > kBodyLimit || ReplayHeaders(entry, now).empty() || !StaticUrl(entry.url)) return;
  const auto bytes = Encode(entry); if (bytes.size() > limit_) return;
  const auto key = Digest(entry.url);
  // Remove an old cache object through its checked handle, never overwrite a
  // potentially redirected file. A miss during publication simply uses network.
  Remove(key); Trim(bytes.size());
  const auto pending = root_ / (key + ".tmp"), destination = root_ / (key + ".sfc");
  {
    File file{CreateFileW(pending.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    Regular(file.value);
    struct PendingFile {
      HANDLE handle; bool published = false;
      ~PendingFile() {
        if (!published) { FILE_DISPOSITION_INFO disposition{TRUE}; SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)); }
      }
    } cleanup{file.value};
    DWORD count = 0;
    Require(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file.value));
    SetLastHit(file.value, now);
    const auto target = destination.native();
    std::vector<unsigned char> memory(sizeof(FILE_RENAME_INFO) + (target.size() + 1) * sizeof(wchar_t));
    auto* rename = new (memory.data()) FILE_RENAME_INFO{}; rename->ReplaceIfExists = FALSE;
    rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    memcpy(rename->FileName, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
    Require(SetFileInformationByHandle(file.value, FileRenameInfo, rename, static_cast<DWORD>(memory.size())) != FALSE);
    cleanup.published = true;
  }
  items_[key] = {bytes.size(), now, ++next_generation_}; stats_.bytes += bytes.size(); stats_.files = items_.size(); ++stats_.writes;
}
void DiskCache::Clear() {
  while (!items_.empty()) Remove(items_.begin()->first);
  size_t seen = 0;
  for (const auto& file : std::filesystem::directory_iterator(root_)) {
    Require(++seen <= 240000);
    const auto name = file.path().filename().string();
    if (name.size() == 68 && Hex(name.substr(0, 64)) && (name.substr(64) == ".tmp" || name.substr(64) == ".sfc"))
      DeleteRegular(file.path());
  }
}

Worker::Worker(std::filesystem::path root, std::function<void()> notify) : notify_(std::move(notify)), thread_([this, root] {
  Run(root);
  { std::lock_guard<std::mutex> lock(mutex_); finished_ = true; }
  notify_();
}) {}
bool Worker::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; wake_.notify_one(); return finished_;
}
Worker::~Worker() { Shutdown(); thread_.join(); }
bool Worker::Submit(Job job) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || jobs_.size() >= (job.kind == Kind::Hit ? 64U : 32U) || queued_bytes_ + job.entry.expected_length > 32 * 1024 * 1024) return false;
  const auto size = job.entry.expected_length;
  if (job.kind == Kind::Lookup) jobs_.push_front(std::move(job)); else jobs_.push_back(std::move(job));
  queued_bytes_ += size; wake_.notify_one(); return true;
}
bool Worker::Lookup(uint64_t id, std::string url) { return Submit({Kind::Lookup, id, std::move(url), {}}); }
bool Worker::Store(uint64_t id, Entry entry, IStream* content) {
  IStream* marshaled = nullptr;
  if (!content || FAILED(CoMarshalInterThreadInterfaceInStream(IID_IStream, content, &marshaled))) return false;
  try { if (Submit({Kind::Store, id, {}, std::move(entry), marshaled})) return true; } catch (...) {}
  CoReleaseMarshalData(marshaled); marshaled->Release(); return false;
}
void Worker::Hit(const Entry& entry) {
  Submit({Kind::Hit, 0, entry.url, {}, nullptr, entry.generation, Now()});
}
bool Worker::Clear(uint64_t id) { return Submit({Kind::Clear, id, {}, {}}); }
std::deque<Result> Worker::Take() {
  std::lock_guard<std::mutex> lock(mutex_); std::deque<Result> result;
  // Bound the amount of response-stream construction in each UI dispatch.
  while (!results_.empty() && result.size() < 2) { result.push_back(std::move(results_.front())); results_.pop_front(); }
  return result;
}
Stats Worker::stats() { std::lock_guard<std::mutex> lock(mutex_); return stats_; }
void Worker::Run(const std::filesystem::path& root) {
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  struct Apartment { HRESULT result; ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); } } com{apartment};
  std::unique_ptr<DiskCache> disk; uint64_t errors = 0;
  try { disk = std::make_unique<DiskCache>(root); } catch (...) { ++errors; }
  { std::lock_guard<std::mutex> lock(mutex_); stats_ = disk ? disk->stats() : Stats{}; stats_.errors += errors; stats_.initialized = true; }
  notify_();
  auto maintenance = std::chrono::steady_clock::now() + std::chrono::minutes(1);
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_); wake_.wait_until(lock, maintenance, [&] { return stopping_ || !jobs_.empty(); });
      if (stopping_ && jobs_.empty()) return;
      if (std::chrono::steady_clock::now() >= maintenance) {
        lock.unlock();
        try { if (disk) disk->PurgeIdle(Now()); } catch (...) { ++errors; }
        lock.lock();
        stats_ = disk ? disk->stats() : Stats{}; stats_.errors += errors; stats_.initialized = true;
        maintenance = std::chrono::steady_clock::now() + std::chrono::minutes(1);
      }
      if (jobs_.empty()) continue;
      job = std::move(jobs_.front()); jobs_.pop_front(); queued_bytes_ -= job.entry.expected_length;
    }
    Result result; result.id = job.id;
    try {
      if (job.kind == Kind::Store) {
        IStream* stream = nullptr;
        const auto acquired = CoGetInterfaceAndReleaseStream(job.marshaled, IID_IStream, reinterpret_cast<void**>(&stream));
        job.marshaled = nullptr;
        struct Stream { IStream* value; ~Stream() { if (value) value->Release(); } } hold{stream};
        Require(SUCCEEDED(apartment) && SUCCEEDED(acquired) && stream && job.entry.expected_length > 0 && job.entry.expected_length <= kBodyLimit);
        LARGE_INTEGER zero{}; Require(SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)));
        job.entry.body.resize(job.entry.expected_length); ULONG read = 0;
        Require(SUCCEEDED(stream->Read(job.entry.body.data(), static_cast<ULONG>(job.entry.body.size()), &read)) && read == job.entry.body.size());
        char extra = 0; read = 0; Require(SUCCEEDED(stream->Read(&extra, 1, &read)) && !read);
      }
      if (disk) {
        if (job.kind == Kind::Lookup) result.entry = disk->Lookup(job.url, Now());
        else if (job.kind == Kind::Store) disk->Store(job.entry, Now());
        else if (job.kind == Kind::Hit) disk->Hit(job.url, job.generation, job.hit_time);
        else { disk->Clear(); result.cleared = true; }
      }
    } catch (...) { ++errors; }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stats_ = disk ? disk->stats() : Stats{}; stats_.errors += errors; stats_.initialized = true; result.stats = stats_;
      if (job.id) results_.push_back(std::move(result));
    }
    if (job.id) notify_();
  }
}
}
