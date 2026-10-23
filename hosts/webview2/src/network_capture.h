#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "skill_cooldown.h"
struct ICoreWebView2;

namespace gbrowser::webview::capture {
class Session {
 public:
  // The optional millisecond clock is monotonic; injection is for deterministic checks only.
  explicit Session(size_t capacity = 128 * 1024 * 1024, size_t record_limit = 2 * 1024 * 1024,
      std::function<uint64_t()> monotonic_clock = {});
  ~Session();
  std::string Call(const std::string& method, const std::string& arguments);
  std::string Active();
  std::wstring Label();
  void Tick();
  void Stop();
  void Clear();
  void Add(const std::string& capture_id, size_t view, const std::string& request_id,
      const std::string& kind, const std::string& json);
  void Loss(const std::string& capture_id);
  void Filtered(const std::string& capture_id);
 private:
  friend void CheckObserverEvents();
  struct State;
  std::unique_ptr<State> state_;
};

class Observer {
 public:
  Observer(ICoreWebView2* core, Session& session, size_t view, std::function<bool()> eligible);
  ~Observer();
  void Tick();
  void Listen(bool enabled, std::function<void(const cooldown::Snapshot&)> changed,
      std::function<bool(const std::string&)> document = {});
  void ResetCooldown();
 private:
  friend void CheckObserverEvents();
  struct State;
  std::shared_ptr<State> state_;
};

class Server {
 public:
  explicit Server(Session& session);
  ~Server();
 private:
  struct State;
  std::unique_ptr<State> state_;
};

int RunMcp(DWORD browser_pid);
void RunCaptureChecks();
void CheckObserverEvents();
void CheckCaptureTransport();
}
