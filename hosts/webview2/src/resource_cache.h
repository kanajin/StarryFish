#pragma once
#include "directory_pin.h"
#include <objbase.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gbrowser::webview::cache {
using Headers = std::vector<std::pair<std::string, std::string>>;
constexpr size_t kBodyLimit = 8 * 1024 * 1024;
constexpr uint64_t kDiskLimit = 10ULL * 1024ULL * 1024 * 1024;
constexpr uint64_t kIdleLimit = 7 * 86400;
struct Request { std::string url, method; Headers headers; };
struct Entry {
  std::string url, headers, body;
  uint64_t stored = 0, expires = 0;
  uint64_t generation = 0; // In-process identity; not part of the on-disk payload.
  size_t expected_length = 0; // Capture only; never trust an unbounded stream.
};
struct Stats {
  uint64_t misses = 0, writes = 0, bytes = 0, files = 0, errors = 0;
  bool available = false, initialized = false;
};
uint64_t Now();
bool GameDocument(const std::string& url);
bool Eligible(const Request& request);
std::optional<Entry> Admit(const Request& request, int status, const Headers& headers, uint64_t now);
std::string ReplayHeaders(const Entry& entry, uint64_t now);

// Single worker owns all disk operations. Only hashed names within a pinned
// cache directory are touched; malformed files are misses, never game failures.
class DiskCache {
 public:
  explicit DiskCache(const std::filesystem::path& root, uint64_t limit = kDiskLimit, uint64_t now = Now());
  std::optional<Entry> Lookup(const std::string& url, uint64_t now);
  void Store(const Entry& entry, uint64_t now);
  void Hit(const std::string& url, uint64_t generation, uint64_t now);
  void PurgeIdle(uint64_t now);
  void Clear();
  Stats stats() const { return stats_; }
 private:
  struct Item { uint64_t size, used, generation; };
  void Remove(const std::string& key);
  void Trim(uint64_t incoming);
  std::filesystem::path root_;
  DirectoryPins pins_;
  std::map<std::string, Item> items_;
  uint64_t limit_, next_generation_ = 0;
  Stats stats_;
};

struct Result { uint64_t id = 0; std::optional<Entry> entry; Stats stats; bool cleared = false; };
class Worker {
 public:
  Worker(std::filesystem::path root, std::function<void()> notify);
  ~Worker();
  bool Shutdown(); // Signal, then let the UI pump completions until finished.
  bool Lookup(uint64_t id, std::string url);
  bool Store(uint64_t id, Entry entry, IStream* content);
  void Hit(const Entry& entry);
  bool Clear(uint64_t id);
  std::deque<Result> Take();
  Stats stats();
 private:
  enum class Kind { Lookup, Store, Clear, Hit };
  struct Job { Kind kind; uint64_t id; std::string url; Entry entry; IStream* marshaled = nullptr; uint64_t generation = 0, hit_time = 0; };
  bool Submit(Job job);
  void Run(const std::filesystem::path& root);
  std::function<void()> notify_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::deque<Result> results_;
  Stats stats_;
  size_t queued_bytes_ = 0;
  bool stopping_ = false, finished_ = false;
  std::thread thread_;
};
}
