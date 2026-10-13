#include "resource_cache.h"
#include "file_digest.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
std::filesystem::path EntryFile(const std::filesystem::path& root, const std::string& url) {
  gbrowser::FileDigest digest; digest.Add(url.data(), url.size()); return root / (digest.Finish() + ".sfc");
}
}
void RunResourceCacheChecks(const std::filesystem::path& root) {
  using namespace gbrowser::webview::cache;
  const auto now = Now(); SYSTEMTIME utc{}; GetSystemTime(&utc);
  wchar_t date[WINHTTP_TIME_FORMAT_BUFSIZE]{}; Require(WinHttpTimeFromSystemTime(&utc, date) != FALSE, "HTTP date formatting failed");
  std::string date_ascii; for (const auto* p = date; *p; ++p) date_ascii += static_cast<char>(*p);
  const std::string url = "https://prd-game-a-granbluefantasy.akamaized.net/assets_en/img/sp/test.png?v=123";
  Request request{url, "GET", {}};
  Headers headers{{"Content-Type", "image/png"}, {"Cache-Control", "public, max-age=3600"}, {"Date", date_ascii},
      {"Content-Length", "4"}, {"Access-Control-Allow-Origin", "*"}, {"Cross-Origin-Resource-Policy", "cross-origin"}, {"X-Content-Type-Options", "nosniff"}};
  Require(GameDocument("https://game.granbluefantasy.jp/#mypage") && GameDocument("https://gbf.game.mbga.jp/#mypage") &&
      !GameDocument("https://connect.mobage.jp/") && !GameDocument("https://game.granbluefantasy.jp.evil/"), "cache document scope mismatch");
  Require(Eligible(request), "valid static resource rejected");
  for (const auto* bad : {"http://prd-game-a-granbluefantasy.akamaized.net/assets/img/a.png", "https://evil.test/assets/a.png",
      "https://game.granbluefantasy.jp/raid/start.json", "https://game.granbluefantasy.jp/assets/a.html", "https://game.granbluefantasy.jp/assets/../a.png",
      "https://game.granbluefantasy.jp/assets/%2e%2e/a.png", "https://game.granbluefantasy.jp/assets/a.png?uid=123", "https://game.granbluefantasy.jp/assets/a.png#hash",
      "https://game.granbluefantasy.jp@evil.test/assets/a.png", "https://game.granbluefantasy.jp:443/assets/a.png"})
    Require(!Eligible({bad, "GET", {}}), "unsafe URL admitted to static cache");
  Require(!Eligible({url, "POST", {}}), "POST cached");
  for (const auto* header : {"Cookie", "Authorization", "Range", "If-Range", "If-None-Match", "If-Modified-Since", "Cache-Control", "Pragma"})
    Require(!Eligible({url, "GET", {{header, "x"}}}), "private/conditional/force-refresh request cached");
  auto admitted = Admit(request, 200, headers, now); Require(admitted.has_value(), "public response not admitted");
  for (int status : {206, 301, 302, 304, 401, 403, 404, 500}) Require(!Admit(request, status, headers, now), "non-200 cached");
  for (const auto* header : {"Set-Cookie", "Vary", "Content-Range", "Content-Disposition", "WWW-Authenticate", "Pragma"}) {
    auto modified = headers; modified.emplace_back(header, "x"); Require(!Admit(request, 200, modified, now), "unsafe response cached");
  }
  for (const auto* control : {"max-age=3600", "public, max-age=0", "public, max-age=3600, no-cache", "public, max-age=3600, private",
      "public, max-age=3600, no-store", "public, max-age=1, max-age=300", "public, max-age=garbage"}) {
    auto modified = headers; modified[1].second = control; Require(!Admit(request, 200, modified, now), "freshness/private directive ignored");
  }
  for (const auto* encoding : {"gzip", "br", "deflate"}) {
    auto modified = headers; modified.emplace_back("Content-Encoding", encoding); Require(!Admit(request, 200, modified, now), "ambiguous compressed body cached");
  }
  auto modified = headers; modified.emplace_back("Age", "3600"); Require(!Admit(request, 200, modified, now), "upstream age ignored");
  modified.back().second = "3500"; auto aged = Admit(request, 200, modified, now); Require(aged && aged->expires <= now + 100, "Age reset freshness");
  modified = headers; modified[1].second += ", s-maxage=0"; Require(!Admit(request, 200, modified, now), "shared TTL zero ignored");
  modified[1].second = "public, max-age=3600, s-maxage=20";
  auto shared = Admit(request, 200, modified, now); Require(shared && shared->expires <= now + 20, "shared TTL did not override max-age");
  modified = headers; modified[0].second = "text/html"; Require(!Admit(request, 200, modified, now), "HTML error/login body cached");
  modified = headers; modified[3].second = "999999999"; Require(!Admit(request, 200, modified, now), "oversized body admitted");
  modified = headers; modified.emplace_back("X-Test", "a\r\nInjected: value"); Require(!Admit(request, 200, modified, now), "response header injection accepted");
  Entry entry = *admitted; entry.body = "DATA";
  const auto replay = ReplayHeaders(entry, now);
  Require(replay.find("Access-Control-Allow-Origin: *") != replay.npos && replay.find("Cross-Origin-Resource-Policy: cross-origin") != replay.npos &&
      replay.find("nosniff") != replay.npos && replay.find("Content-Length: 4") != replay.npos && replay.find("Cache-Control: no-store") != replay.npos,
      "replay dropped security headers or reset native freshness");
  Require(ReplayHeaders(entry, entry.expires).empty() && ReplayHeaders(entry, now - 1).empty(), "expired/time-reversed replay allowed");
  const auto disk_root = root / L"StaticCache";
  {
    DiskCache disk(disk_root); Require(!disk.Lookup(url, now), "cold lookup unexpectedly hit");
    disk.Store(entry, now); auto hit = disk.Lookup(url, now);
    Require(hit && hit->body == "DATA" && hit->headers == entry.headers, "disk response bytes/metadata changed");
    Require(!disk.Lookup(url + "4", now), "distinct version used stale cached resource");
  }
  {
    DiskCache reopened(disk_root); Require(reopened.Lookup(url, now).has_value(), "cache did not survive restart");
    Require(!reopened.Lookup(url, entry.expires), "expired resource returned");
    reopened.Store(entry, now);
    { std::fstream file(EntryFile(disk_root, url), std::ios::in | std::ios::out | std::ios::binary); file.seekp(-1, std::ios::end); file.put('X'); }
    Require(!reopened.Lookup(url, now), "corrupted bytes replayed"); reopened.Store(entry, now);
    std::ofstream(disk_root / L"user-note.txt") << "retain";
    reopened.Clear(); Require(!reopened.Lookup(url, now) && std::filesystem::exists(disk_root / L"user-note.txt"), "clear touched unknown file");
    const auto pending = disk_root / (std::string(64, 'a') + ".tmp"); std::ofstream(pending) << "partial";
    reopened.Clear(); Require(!std::filesystem::exists(pending), "clear reported success but retained incomplete temp file");
    reopened.Store(entry, now);
    std::filesystem::create_hard_link(EntryFile(disk_root, url), root / L"cache-link-evidence");
    Require(!reopened.Lookup(url, now), "hardlinked cache read accepted");
    bool rejected = false; try { reopened.Clear(); } catch (...) { rejected = true; }
    Require(rejected && std::filesystem::exists(root / L"cache-link-evidence"), "clear deleted a hardlinked target");
  }
  {
    DiskCache limited(root / L"SmallCache", 700);
    limited.Store(entry, now); auto second = entry; second.url += "4"; limited.Store(second, now);
    Require(limited.stats().bytes <= 700 && limited.Lookup(second.url, now).has_value() && !limited.Lookup(entry.url, now), "cache quota/eviction failed");
  }
  {
    static_assert(kDiskLimit == 10ULL * 1024 * 1024 * 1024, "10 GiB quota changed");
    const auto idle_root = root / L"IdleCache";
    Entry week = entry; week.expires = now + kIdleLimit;
    DiskCache disk(idle_root, kDiskLimit, now); disk.Store(week, now);
    Require(disk.Lookup(url, now + 60).has_value(), "fresh idle fixture missing");
    // Looking up bytes without successfully supplying a browser response is
    // not a hit and must not prolong retention, including across restart.
    {
      DiskCache reopened(idle_root, kDiskLimit, now + kIdleLimit);
      Require(reopened.stats().files == 1, "exact one-week boundary evicted early");
      reopened.PurgeIdle(now + kIdleLimit + 1);
      Require(reopened.stats().files == 0, "unacknowledged lookup renewed idle age");
    }
    disk.Clear(); disk.Store(week, now); Require(disk.Lookup(url, now + 100).has_value(), "hit fixture missing");
    disk.Hit(url, disk.Lookup(url, now + 100)->generation + 1, now + 100); // stale/wrong identity is ignored
    {
      DiskCache reopened(idle_root, kDiskLimit, now + kIdleLimit + 1);
      Require(reopened.stats().files == 0, "wrong generation renewed idle age");
    }
    disk.Clear(); disk.Store(week, now); Require(disk.Lookup(url, now + 100).has_value(), "second hit fixture missing");
    disk.Hit(url, disk.Lookup(url, now + 100)->generation, now + 100);
    {
      DiskCache reopened(idle_root, kDiskLimit, now + kIdleLimit + 1);
      Require(reopened.stats().files == 1, "successful hit timestamp lost on restart");
      reopened.PurgeIdle(now + kIdleLimit + 100);
      Require(reopened.stats().files == 1, "hit week boundary evicted early");
      reopened.PurgeIdle(now + kIdleLimit + 101);
      Require(reopened.stats().files == 0, "idle maintenance did not delete aged hit");
    }
    disk.Clear(); disk.Store(week, now);
    const auto old_generation = disk.Lookup(url, now)->generation;
    disk.Store(week, now); // Identical URL and stored second, different object.
    disk.Hit(url, old_generation, now + 100);
    {
      DiskCache reopened(idle_root, kDiskLimit, now + kIdleLimit + 1);
      Require(reopened.stats().files == 0, "late hit renewed same-second replacement");
    }
    disk.Clear(); disk.Store(week, now); Require(disk.Lookup(url, now + 100).has_value(), "freshness fixture missing");
    disk.Hit(url, disk.Lookup(url, now + 100)->generation, now + 100);
    Require(!disk.Lookup(url, week.expires), "hit extended server freshness");
    disk.Store(week, now);
    DiskCache restarted(idle_root, kDiskLimit, now + kIdleLimit + 1);
    Require(restarted.stats().files == 0, "startup did not prune never-hit resource");
  }
  // Exercise actual COM stream marshaling, background disk IO and results on a
  // temporary directory. No browser, game traffic or user profiles are opened.
  const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); Require(SUCCEEDED(com), "cache check COM init failed");
  HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(ready != nullptr, "cache result event missing");
  {
    Worker worker(root / L"WorkerCache", [ready] { SetEvent(ready); });
    const auto wait = [&]() {
      const auto started = GetTickCount64();
      for (;;) {
        Require(GetTickCount64() - started < 10000, "cache worker deadline");
        DWORD index = 0; Require(SUCCEEDED(CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES, 10000, 1, &ready, &index)), "cache worker timeout");
        auto results = worker.Take(); if (results.empty()) continue; // Initialization also wakes the host.
        Require(results.size() == 1, "worker completion duplicated"); return std::move(results.front());
      }
    };
    IStream* content = SHCreateMemStream(reinterpret_cast<const BYTE*>("DATA"), 4); Require(content != nullptr, "fixture stream missing");
    Require(worker.Store(1, entry, content), "stream capture submission failed"); content->Release();
    auto write = wait(); Require(write.id == 1 && write.stats.writes == 1 && write.stats.initialized, "background stream capture failed");
    Require(worker.Lookup(2, url), "lookup submission failed"); auto found = wait();
    Require(found.id == 2 && found.entry && found.entry->body == "DATA", "background lookup mismatch");
    worker.Hit(*found.entry); // Acknowledgment drains before queued clear/shutdown.
    Require(worker.Clear(3), "clear submission failed"); auto cleared = wait(); Require(cleared.id == 3 && cleared.cleared, "clear completion failed");
    Require(worker.Lookup(4, url), "lookup after clear not queued"); Require(!wait().entry, "clear retained cache body");
    const auto shutdown_started = GetTickCount64();
    while (!worker.Shutdown()) {
      Require(GetTickCount64() - shutdown_started < 10000, "async shutdown did not finish");
      DWORD index = 0;
      Require(SUCCEEDED(CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES, 10000, 1, &ready, &index)), "shutdown completion missing");
    }
    Require(!worker.Lookup(5, url), "shutdown accepted new work");
  }
  CloseHandle(ready); CoUninitialize();
  std::cout << "PASS public static HTTP policy, freshness/Age/versions, exact replay, restart, corruption, 10 GiB quota, persistent acknowledged hits, one-week idle cleanup, hardlinks, clear and COM worker; browser NOT_STARTED\n";
}
