#include "network_capture.h"
#include <wrl.h>
#include <WebView2.h>
#include "capture_json.h"
#include "gbf_policy.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace gbrowser::webview::capture {
namespace {
uint64_t WallTime() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}
bool IsExtensionUrl(const std::string& url) {
  const auto text = Lower(url);
  return text.rfind("chrome-extension://", 0) == 0 || text.rfind("extension://", 0) == 0;
}
struct InitiatorSources {
  bool extension = false, page = false, incomplete = false;
  size_t visited = 0;
};
void InspectInitiator(const IJsonValue& value, InitiatorSources& sources, unsigned depth = 0) {
  if (depth > 32 || sources.visited >= 4096) { sources.incomplete = true; return; }
  ++sources.visited;
  if (value.ValueType() == JsonValueType::Object) {
    for (const auto& item : value.GetObject()) {
      if (sources.visited >= 4096) { sources.incomplete = true; return; }
      if (item.Key() == L"url" && item.Value().ValueType() == JsonValueType::String) {
        const auto url = Lower(winrt::to_string(item.Value().GetString()));
        sources.extension |= IsExtensionUrl(url);
        sources.page |= url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0 ||
            url.rfind("blob:https://", 0) == 0 || url.rfind("blob:http://", 0) == 0;
      }
      InspectInitiator(item.Value(), sources, depth + 1);
    }
  } else if (value.ValueType() == JsonValueType::Array) {
    for (const auto& item : value.GetArray()) {
      if (sources.visited >= 4096) { sources.incomplete = true; return; }
      InspectInitiator(item, sources, depth + 1);
    }
  }
}
bool IsCapturableChildTarget(const JsonObject& info) {
  const auto type = Text(info, L"type");
  return (type == "iframe" || type == "worker") && !IsExtensionUrl(Text(info, L"url"));
}
JsonObject Metadata(const JsonObject& input) {
  JsonObject output;
  bool redacted = false;
  for (const auto* name : {L"url", L"documentURL"}) if (input.HasKey(name)) Put(output, name, RedactUrl(Text(input, name), redacted));
  for (const auto* name : {L"method", L"mimeType", L"protocol", L"type", L"frameId", L"loaderId", L"target_session", L"cdp_request_id"})
    if (input.HasKey(name)) Put(output, name, Text(input, name));
  for (const auto* name : {L"status", L"timestamp", L"wallTime", L"encodedDataLength"})
    if (input.HasKey(name)) output.SetNamedValue(name, input.GetNamedValue(name));
  if (input.HasKey(L"headers")) output.SetNamedValue(L"headers", Headers(input.GetNamedObject(L"headers"), redacted));
  Flag(output, L"redacted", redacted);
  return output;
}
}

struct Session::State {
  struct Record { uint64_t id, observed_tick; std::string json; };
  std::mutex mutex;
  std::deque<Record> records;
  std::function<uint64_t()> now;
  size_t capacity, limit, bytes = 0;
  uint64_t serial = 0, next = 1, dropped = 0, expired = 0, evicted_through = 0;
  uint64_t truncated = 0, filtered = 0, started = 0, deadline = 0, monotonic_deadline = 0, retention = 0;
  std::string id, status = "idle", mode = "bounded";
  void Clear() {
    records.clear(); bytes = 0; id.clear(); status = "cleared"; mode = "bounded"; retention = 0;
    started = 0; deadline = 0; monotonic_deadline = 0; next = 1; dropped = 0; expired = 0; evicted_through = 0;
    truncated = 0; filtered = 0;
  }
  void EvictFront() {
    evicted_through = (std::max)(evicted_through, records.front().id);
    bytes -= records.front().json.size() + 128; records.pop_front();
  }
  void Expire() {
    if (status != "active") return;
    const auto tick = now();
    if (mode == "bounded") { if (tick >= monotonic_deadline) status = "expired"; return; }
    // Page/CDP timestamps and wall-clock changes never control retention.
    while (!records.empty() && tick >= records.front().observed_tick &&
        tick - records.front().observed_tick >= retention * 1000) {
      EvictFront(); ++expired;
    }
  }
  JsonObject Status() const {
    JsonObject result;
    Put(result, L"capture_id", id); Put(result, L"state", status); Put(result, L"mode", mode);
    Number(result, L"retention_seconds", static_cast<double>(retention));
    Number(result, L"started_at_ms", static_cast<double>(started)); Number(result, L"deadline_ms", static_cast<double>(deadline));
    Number(result, L"buffer_bytes", static_cast<double>(bytes)); Number(result, L"record_count", static_cast<double>(records.size()));
    Number(result, L"dropped_records", static_cast<double>(dropped)); Number(result, L"expired_records", static_cast<double>(expired));
    Number(result, L"truncated_records", static_cast<double>(truncated)); Number(result, L"filtered_requests", static_cast<double>(filtered));
    Number(result, L"capacity_bytes", static_cast<double>(capacity)); Number(result, L"record_limit_bytes", static_cast<double>(limit));
    Number(result, L"next_cursor", static_cast<double>(next - 1));
    return result;
  }
};
Session::Session(size_t capacity, size_t record_limit, std::function<uint64_t()> monotonic_clock)
    : state_(std::make_unique<State>()) {
  if (capacity < 4096 || record_limit < 1024 || record_limit > capacity) throw std::invalid_argument("capture limits");
  state_->capacity = capacity; state_->limit = record_limit;
  state_->now = monotonic_clock ? std::move(monotonic_clock) : [] { return GetTickCount64(); };
}
Session::~Session() = default;
std::string Session::Call(const std::string& method, const std::string& arguments) {
  const auto args = Object(arguments);
  const bool start = method == "gbf_capture_start", read = method == "gbf_capture_read";
  if (!start && !read && method != "gbf_capture_status" && method != "gbf_capture_stop" && method != "gbf_capture_clear")
    throw std::runtime_error("unknown_tool");
  for (const auto& argument : args) {
    const auto name = argument.Key();
    if (!(start ? name == L"mode" || name == L"duration_seconds" || name == L"retention_seconds" :
        name == L"capture_id" || (read && (name == L"cursor" || name == L"limit"))))
      throw std::runtime_error("unknown_argument");
  }
  const auto integer = [&](const wchar_t* name, double fallback, double minimum, double maximum, const char* error) {
    if (!args.HasKey(name)) return fallback;
    const auto value = args.GetNamedValue(name);
    if (value.ValueType() != JsonValueType::Number) throw std::runtime_error(error);
    const auto number = value.GetNumber();
    if (!std::isfinite(number) || number < minimum || number > maximum || std::trunc(number) != number)
      throw std::runtime_error(error);
    return number;
  };
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto& state = *state_; state.Expire();
  if (start) {
    if (args.HasKey(L"mode") && args.GetNamedValue(L"mode").ValueType() != JsonValueType::String)
      throw std::runtime_error("mode_must_be_bounded_or_rolling");
    const auto mode = Text(args, L"mode", "bounded");
    if (mode != "bounded" && mode != "rolling") throw std::runtime_error("mode_must_be_bounded_or_rolling");
    if (mode == "rolling" && args.HasKey(L"duration_seconds")) throw std::runtime_error("rolling_rejects_duration_seconds");
    if (mode == "bounded" && args.HasKey(L"retention_seconds")) throw std::runtime_error("bounded_rejects_retention_seconds");
    const auto duration = mode == "bounded" ? integer(L"duration_seconds", 120, 1, 600, "duration_must_be_integer_1_to_600") : 0;
    const auto retention = mode == "rolling" ? integer(L"retention_seconds", 600, 600, 600, "retention_must_be_integer_600") : 0;
    if (state.status == "active") throw std::runtime_error("capture_already_active");
    state.Clear(); state.mode = mode; state.retention = static_cast<uint64_t>(retention);
    state.started = WallTime();
    if (mode == "bounded") {
      state.deadline = state.started + static_cast<uint64_t>(duration * 1000);
      state.monotonic_deadline = state.now() + static_cast<uint64_t>(duration * 1000);
    }
    state.id = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(state.started) + "-" + std::to_string(++state.serial);
    state.status = "active";
    auto result = state.Status(); Put(result, L"observer_state", "arming; wait for observer_ready records before triggering actions");
    return Encode(result);
  }
  if (!args.HasKey(L"capture_id") || args.GetNamedValue(L"capture_id").ValueType() != JsonValueType::String)
    throw std::runtime_error("capture_id_must_be_string");
  if (state.id.empty() || Text(args, L"capture_id") != state.id) throw std::runtime_error("unknown_capture_id");
  if (method == "gbf_capture_stop") { state.status = "stopped"; return Encode(state.Status()); }
  if (method == "gbf_capture_clear") { state.Clear(); return Encode(state.Status()); }
  if (method == "gbf_capture_status") return Encode(state.Status());
  const auto cursor_value = integer(L"cursor", 0, 0, static_cast<double>(state.next - 1), "invalid_pagination");
  const auto limit_value = integer(L"limit", 100, 1, 100, "invalid_pagination");
  auto cursor = static_cast<uint64_t>(cursor_value);
  auto result = state.Status(); JsonArray records; size_t bytes = 0; bool has_more = false;
  Flag(result, L"cursor_gap", cursor < state.evicted_through);
  for (const auto& record : state.records) {
    if (record.id <= cursor) continue;
    if (records.Size() >= static_cast<uint32_t>(limit_value) || (records.Size() && bytes + record.json.size() > 4 * 1024 * 1024)) {
      has_more = true; break;
    }
    records.Append(Object(record.json)); bytes += record.json.size(); cursor = record.id;
  }
  // Advance over an entirely evicted window or a discarded tail, rather than
  // returning an empty page with has_more forever. New records keep increasing IDs.
  if (!has_more) cursor = state.next - 1;
  result.SetNamedValue(L"records", records); Number(result, L"next_cursor", static_cast<double>(cursor));
  Flag(result, L"has_more", has_more);
  return Encode(result);
}
std::string Session::Active() {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire(); return state_->status == "active" ? state_->id : "";
}
std::wstring Session::Label() {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire();
  if (state_->status == "active") return state_->mode == "rolling" ?
      L" · 网络持续采集中（保留最近10分钟，菜单可停止）" : L" · 网络采集中（菜单可停止）";
  if (state_->status == "idle") return L"";
  return state_->status == "cleared" ? L" · 抓取已清除" : L" · 抓取已停止";
}
void Session::Tick() { std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire(); }
void Session::Stop() {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire();
  if (state_->status == "active") state_->status = "stopped";
}
void Session::Clear() {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Clear();
}
void Session::Loss(const std::string& capture_id) {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire();
  if (state_->id == capture_id && state_->status == "active") ++state_->dropped;
}
void Session::Filtered(const std::string& capture_id) {
  std::lock_guard<std::mutex> lock(state_->mutex); state_->Expire();
  if (state_->id == capture_id && state_->status == "active") ++state_->filtered;
}
void Session::Add(const std::string& capture_id, size_t view, const std::string& request_id,
    const std::string& kind, const std::string& json) {
  std::lock_guard<std::mutex> lock(state_->mutex); auto& state = *state_; state.Expire();
  if (capture_id != state.id || state.status != "active") return;
  auto record = Object(json);
  Put(record, L"capture_id", state.id); Put(record, L"request_id", request_id); Put(record, L"kind", kind);
  Number(record, L"record_id", static_cast<double>(state.next++)); Number(record, L"view_id", static_cast<double>(view));
  Number(record, L"observed_at_ms", static_cast<double>(WallTime()));
  auto encoded = Encode(record);
  if (encoded.size() + 128 > state.limit) {
    record = Metadata(record); Put(record, L"capture_id", state.id); Put(record, L"request_id", request_id);
    Put(record, L"kind", kind); Number(record, L"record_id", static_cast<double>(state.next - 1));
    Number(record, L"view_id", static_cast<double>(view)); Number(record, L"observed_at_ms", static_cast<double>(WallTime()));
    if (record.HasKey(L"headers")) record.Remove(L"headers");
    if (Text(record, L"url").size() > 4096) { Put(record, L"url", Text(record, L"url").substr(0, 4096)); }
    Flag(record, L"truncated"); Flag(record, L"body_unavailable"); Put(record, L"reason", "record_limit"); ++state.truncated;
    encoded = Encode(record);
  } else if (encoded.find("\"truncated\":true") != std::string::npos) ++state.truncated;
  const auto cost = encoded.size() + 128;
  if (cost > state.capacity || cost > state.limit) {
    state.evicted_through = state.next - 1; ++state.dropped; return;
  }
  while (!state.records.empty() && state.bytes + cost > state.capacity) { state.EvictFront(); ++state.dropped; }
  state.bytes += cost; state.records.push_back({state.next - 1, state.now(), std::move(encoded)});
}

struct Observer::State : std::enable_shared_from_this<Observer::State> {
  using Core = Microsoft::WRL::ComPtr<ICoreWebView2>;
  Core core;
  Session& session;
  size_t view;
  std::function<bool()> eligible;
  bool alive = true;
  bool listening = false, network = false, network_ready = false;
  uint64_t network_epoch = 0, request_sequence = 0;
  cooldown::State cooldown_state;
  std::function<void(const cooldown::Snapshot&)> cooldown_changed;
  std::function<bool(const std::string&)> cooldown_document;
  std::string capture_id;
  std::set<std::string> targets;
  struct Pending {
    std::string mime;
    double started = 0;
    bool socket = false;
    bool skill = false;
    bool expects_snapshot = false;
    uint64_t sequence = 0, epoch = 0;
  };
  std::map<std::string, Pending> pending;
  std::set<std::string> excluded;
  struct Subscription { Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> receiver; EventRegistrationToken token{}; };
  std::vector<Subscription> subscriptions;
  size_t body_pending = 0;
  size_t skill_pending = 0;
  State(ICoreWebView2* value, Session& buffer, size_t index, std::function<bool()> filter)
      : core(value), session(buffer), view(index), eligible(std::move(filter)) {}
  void Emit(const std::string& request, const std::string& kind, const JsonObject& object) {
    if (capture_id.empty() || session.Active() != capture_id) return;
    auto record = object;
    if (!request.empty()) {
      const auto delimiter = request.find(':');
      Put(record, L"target_session", delimiter == std::string::npos ? "" : request.substr(0, delimiter));
      Put(record, L"cdp_request_id", delimiter == std::string::npos ? request : request.substr(delimiter + 1));
    }
    session.Add(capture_id, view, request, kind, Encode(record));
  }
  void Method(const std::string& target, const wchar_t* method, const JsonObject& args,
      std::function<void(HRESULT, const JsonObject&)> done = {}) {
    if (!core) { if (done) done(E_POINTER, JsonObject{}); return; }
    const auto weak = weak_from_this();
    auto callback = Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
        [weak, done](HRESULT error, LPCWSTR json) -> HRESULT {
          const auto self = weak.lock(); if (!self || !self->alive) return S_OK;
          try { if (done) done(error, SUCCEEDED(error) && json ? JsonObject::Parse(json) : JsonObject{}); }
          catch (...) { self->session.Loss(self->capture_id); if (self->listening) self->ResetCooldown(); }
          return S_OK;
        });
    HRESULT error;
    if (target.empty()) error = core->CallDevToolsProtocolMethod(method, args.Stringify().c_str(), callback.Get());
    else {
      Microsoft::WRL::ComPtr<ICoreWebView2_11> sessions;
      error = core.As(&sessions);
      if (SUCCEEDED(error)) error = sessions->CallDevToolsProtocolMethodForSession(winrt::to_hstring(target).c_str(), method, args.Stringify().c_str(), callback.Get());
    }
    if (FAILED(error) && done) done(error, JsonObject{});
  }
  void Enable(const std::string& target) {
    const auto epoch = network_epoch; JsonObject options;
    Number(options, L"maxTotalBufferSize", 2 * 1024 * 1024); Number(options, L"maxResourceBufferSize", 1024 * 1024); Number(options, L"maxPostDataSize", 1024 * 1024);
    Method(target, L"Network.enable", options, [self = shared_from_this(), epoch, target](HRESULT error, const JsonObject&) {
      if (epoch != self->network_epoch || !self->eligible() || (!target.empty() && !self->targets.count(target))) return;
      if (target.empty()) self->network_ready = SUCCEEDED(error);
      JsonObject record; Put(record, L"target_session", target); Put(record, L"state", FAILED(error) ? "unavailable" : "ready");
      if (FAILED(error)) Put(record, L"reason", "Network.enable_failed");
      self->Emit("", FAILED(error) ? "observer_error" : "observer_ready", record);
      if (FAILED(error)) self->ResetCooldown();
    });
    JsonObject attach; Flag(attach, L"autoAttach"); Flag(attach, L"waitForDebuggerOnStart", false); Flag(attach, L"flatten");
    JsonArray filters;
    for (const auto* type : {"iframe", "worker"}) {
      JsonObject include; Put(include, L"type", type); Flag(include, L"exclude", false); filters.Append(include);
    }
    JsonObject exclude; Flag(exclude, L"exclude"); filters.Append(exclude); attach.SetNamedValue(L"filter", filters);
    Method(target, L"Target.setAutoAttach", attach, [self = shared_from_this(), epoch](HRESULT error, const JsonObject&) {
      if (FAILED(error) && epoch == self->network_epoch) { JsonObject record; Put(record, L"reason", "child_target_auto_attach_unavailable"); self->Emit("", "observer_error", record); }
    });
  }
  void Unavailable(const std::string& key, const std::string& reason) {
    JsonObject record; Flag(record, L"body_unavailable"); Put(record, L"reason", reason); Emit(key, "response_body", record);
  }
  void ResetCooldown() {
    cooldown_state.Reset();
    if (cooldown_changed) cooldown_changed(cooldown_state.snapshot());
  }
  void Deliver(const Pending& request, const cooldown::Snapshot& snapshot) {
    if (listening && eligible() && cooldown_state.Apply(request.epoch, request.sequence, snapshot) && cooldown_changed)
      cooldown_changed(cooldown_state.snapshot());
  }
  void CompleteBody(uint64_t epoch, const std::string& capture, const std::string& key,
      const Pending& request, bool skill, HRESULT error, const JsonObject& body) {
    if (epoch != network_epoch) return;
    auto& count = skill ? skill_pending : body_pending;
    if (count) --count;
    const auto delimiter = key.find(':');
    if (delimiter != std::string::npos && delimiter != 0 && !targets.count(key.substr(0, delimiter))) return;
    const bool available = SUCCEEDED(error) && body.HasKey(L"body");
    if (skill) {
      if (!available || body.GetNamedBoolean(L"base64Encoded", false)) Deliver(request, cooldown::Snapshot{});
      else {
        const auto text = Text(body, L"body");
        const auto snapshot = cooldown::Parse(text);
        if (snapshot) Deliver(request, *snapshot);
        else if (request.expects_snapshot || text.size() > 1024 * 1024) Deliver(request, cooldown::Snapshot{});
      }
    }
    if (capture != capture_id) return;
    if (!available) Unavailable(key, "runtime_body_unavailable");
    else Emit(key, "response_body", Body(Text(body, L"body"), request.mime, body.GetNamedBoolean(L"base64Encoded", false)));
  }
  void Event(const std::string& name, const std::string& target, const JsonObject& args) {
    if (!alive || (!listening && (capture_id.empty() || session.Active() != capture_id)) || !eligible()) return;
    if (!target.empty() && !targets.count(target)) return;
    if (name == "Target.attachedToTarget") {
      const auto info = args.GetNamedObject(L"targetInfo", JsonObject{});
      if (!IsCapturableChildTarget(info)) return;
      const auto attached = Text(args, L"sessionId");
      if (targets.size() >= 32) { session.Loss(capture_id); return; }
      targets.insert(attached); Enable(attached); return;
    }
    if (name == "Target.detachedFromTarget") {
      const auto detached = Text(args, L"sessionId");
      if (detached.empty() || !targets.erase(detached)) return;
      const auto prefix = detached + ":";
      for (auto entry = pending.begin(); entry != pending.end();) {
        if (entry->first.rfind(prefix, 0) == 0) entry = pending.erase(entry); else ++entry;
      }
      // Safe only after detach: the target guard rejects all late events. Never
      // age out excluded sources on an attached target into unknown-context capture.
      for (auto entry = excluded.begin(); entry != excluded.end();) {
        if (entry->rfind(prefix, 0) == 0) entry = excluded.erase(entry); else ++entry;
      }
      return;
    }
    const auto raw_id = Text(args, L"requestId");
    if (raw_id.empty() || raw_id.size() > 256 || target.size() > 256) return;
    const auto key = target + ":" + raw_id;
    if (name == "Network.requestWillBeSent" || name == "Network.webSocketCreated") {
      const auto initiator = args.GetNamedObject(L"initiator", JsonObject{});
      InitiatorSources sources; InspectInitiator(initiator, sources);
      const bool socket = name == "Network.webSocketCreated";
      const auto request = socket ? args : args.GetNamedObject(L"request", JsonObject{});
      const auto document_url = Text(args, L"documentURL");
      const bool game_document = IsGbfPageUrl(document_url) && !IsGbfLoginUrl(document_url);
      if (excluded.count(key) || IsExtensionUrl(Text(args, L"documentURL")) || IsExtensionUrl(Text(request, L"url")) ||
          (sources.extension && !sources.page && !sources.incomplete && !game_document)) {
        if (excluded.size() >= 4096) { JsonObject record; Put(record, L"reason", "source_filter_capacity"); Emit("", "observer_error", record); session.Stop(); }
        else if (excluded.insert(key).second) session.Filtered(capture_id);
        pending.erase(key);
        return;
      }
      if (pending.size() >= 4096 && !pending.count(key)) { session.Loss(capture_id); return; }
      if (args.HasKey(L"redirectResponse")) {
        auto redirect = Metadata(args.GetNamedObject(L"redirectResponse")); Put(redirect, L"terminal", "redirect");
        Flag(redirect, L"body_unavailable"); Put(redirect, L"reason", "redirect_body_not_exposed"); Emit(key, "response", redirect);
      }
      pending[key] = {"", args.GetNamedNumber(L"timestamp", 0), socket};
      auto& tracked = pending[key];
      tracked.skill = listening && !socket && game_document && (!cooldown_document || cooldown_document(document_url)) &&
          cooldown::BattleResponse(Text(request, L"url"));
      const auto url = Text(request, L"url");
      tracked.expects_snapshot = url.find("/start.json") != std::string::npos || url.find("/ability_result.json") != std::string::npos ||
          url.find("/normal_attack_result.json") != std::string::npos;
      tracked.sequence = ++request_sequence; tracked.epoch = cooldown_state.epoch();
      if (capture_id.empty()) return;
      auto record = Metadata(request); record.SetNamedValue(L"context", Metadata(args));
      Put(record, L"source_attribution", sources.incomplete ? "incomplete_initiator" :
          sources.extension && !sources.page && game_document ? "game_document_extension_initiator" :
          sources.extension ? "mixed_page_extension" : "game_view");
      Put(record, L"initiator_type", Text(initiator, L"type"));
      bool redacted = false; record.SetNamedValue(L"initiator", RedactValue(initiator, redacted));
      if (redacted) Flag(record, L"redacted");
      Put(record, L"terminal", "pending");
      if (request.HasKey(L"postData")) {
        const auto headers = request.GetNamedObject(L"headers", JsonObject{}); std::string mime;
        for (const auto& header : headers) if (Lower(winrt::to_string(header.Key())) == "content-type") mime = winrt::to_string(header.Value().GetString());
        record.SetNamedValue(L"body", Body(Text(request, L"postData"), mime));
      } else if (request.GetNamedBoolean(L"hasPostData", false)) {
        JsonObject body; Flag(body, L"body_unavailable"); Put(body, L"reason", "request_body_not_exposed_or_multipart"); record.SetNamedValue(L"body", body);
      }
      Emit(key, socket ? "websocket_created" : "request", record); return;
    }
    if (excluded.count(key)) {
      if (name == "Network.loadingFinished" || name == "Network.loadingFailed" || name == "Network.webSocketClosed") excluded.erase(key);
      return;
    }
    auto found = pending.find(key);
    if (found == pending.end()) {
      if (pending.size() >= 4096) { session.Loss(capture_id); return; }
      const bool socket = name.find("Network.webSocket") == 0;
      found = pending.emplace(key, Pending{"", args.GetNamedNumber(L"timestamp", 0), socket}).first;
      JsonObject record; Flag(record, L"request_context_unavailable"); Put(record, L"reason", "request_started_before_observation_or_prior_event_missing");
      Put(record, L"terminal", "pending"); Emit(key, socket ? "websocket_created" : "request", record);
    }
    if (name == "Network.responseReceived") {
      const auto response = args.GetNamedObject(L"response", JsonObject{});
      found->second.mime = Text(response, L"mimeType").substr(0, 256);
      auto record = Metadata(response); Number(record, L"elapsed_ms", (args.GetNamedNumber(L"timestamp", found->second.started) - found->second.started) * 1000);
      Emit(key, "response", record);
    } else if (name == "Network.loadingFailed") {
      auto record = Metadata(args); Put(record, L"terminal", args.GetNamedBoolean(L"canceled", false) ? "canceled" : "failed");
      auto error = Text(args, L"errorText");
      if (error.size() > 80 || error.find("net::ERR_") != 0 || error.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_:") != std::string::npos) error = "runtime_request_failed";
      Put(record, L"error", error); Flag(record, L"body_unavailable"); Put(record, L"reason", "request_failed");
      if (found->second.skill) Deliver(found->second, cooldown::Snapshot{});
      Emit(key, "request_end", record); pending.erase(found);
    } else if (name == "Network.loadingFinished") {
      auto record = Metadata(args); Put(record, L"terminal", "completed");
      Number(record, L"elapsed_ms", (args.GetNamedNumber(L"timestamp", found->second.started) - found->second.started) * 1000); Emit(key, "request_end", record);
      const auto request = found->second; pending.erase(found);
      const bool skill = request.skill && listening && request.epoch == cooldown_state.epoch();
      if (!skill && capture_id.empty()) return;
      if ((skill ? skill_pending >= 8 : body_pending >= 4) || args.GetNamedNumber(L"encodedDataLength", 0) > 1024 * 1024) {
        Unavailable(key, "body_budget"); if (skill) Deliver(request, cooldown::Snapshot{}); return;
      }
      if (skill) ++skill_pending; else ++body_pending;
      const auto epoch = network_epoch; const auto capture = capture_id;
      JsonObject parameters; Put(parameters, L"requestId", raw_id);
      Method(target, L"Network.getResponseBody", parameters, [self = shared_from_this(), epoch, capture, key, request, skill](HRESULT error, const JsonObject& body) {
        self->CompleteBody(epoch, capture, key, request, skill, error, body);
      });
    } else if (name == "Network.webSocketWillSendHandshakeRequest" || name == "Network.webSocketHandshakeResponseReceived") {
      auto record = Metadata(args.GetNamedObject(name == "Network.webSocketWillSendHandshakeRequest" ? L"request" : L"response", JsonObject{}));
      Emit(key, name == "Network.webSocketWillSendHandshakeRequest" ? "websocket_handshake_request" : "websocket_handshake_response", record);
    } else if (name == "Network.webSocketFrameSent" || name == "Network.webSocketFrameReceived") {
      const auto frame = args.GetNamedObject(L"response", JsonObject{}); const auto opcode = frame.GetNamedNumber(L"opcode", 0);
      const auto payload = Text(frame, L"payloadData");
      auto record = Body(payload, "websocket", opcode != 1);
      record.SetNamedValue(L"gbf_cd_extraction", GbfCooldownExtraction(payload, opcode == 1));
      Number(record, L"opcode", opcode); Number(record, L"timestamp", args.GetNamedNumber(L"timestamp", 0));
      Put(record, L"direction", name == "Network.webSocketFrameSent" ? "sent" : "received"); Emit(key, "websocket_frame", record);
    } else if (name == "Network.webSocketClosed" || name == "Network.webSocketFrameError") {
      auto record = Metadata(args); Put(record, L"terminal", name == "Network.webSocketClosed" ? "closed" : "failed");
      if (name == "Network.webSocketFrameError") Put(record, L"error", "runtime_websocket_error");
      Emit(key, "websocket_end", record); pending.erase(found);
    }
  }
  void Subscribe(const std::string& name) {
    Subscription entry;
    winrt::check_hresult(core->GetDevToolsProtocolEventReceiver(winrt::to_hstring(name).c_str(), &entry.receiver));
    const auto weak = weak_from_this();
    winrt::check_hresult(entry.receiver->add_DevToolsProtocolEventReceived(
        Microsoft::WRL::Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
        [weak, name](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args) -> HRESULT {
          const auto self = weak.lock(); if (!self || !self->alive || (!self->listening && self->capture_id.empty())) return S_OK;
          LPWSTR json = nullptr, session_id = nullptr;
          try {
            Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceivedEventArgs2> detail;
            if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&detail)))) winrt::check_hresult(detail->get_SessionId(&session_id));
            winrt::check_hresult(args->get_ParameterObjectAsJson(&json));
            if (json && wcslen(json) <= 4 * 1024 * 1024) self->Event(name, session_id ? winrt::to_string(session_id) : "", JsonObject::Parse(json));
            else self->session.Loss(self->capture_id);
          } catch (...) { self->session.Loss(self->capture_id); if (self->listening) self->ResetCooldown(); }
          CoTaskMemFree(json); CoTaskMemFree(session_id); return S_OK;
        }).Get(), &entry.token));
    subscriptions.push_back(std::move(entry));
  }
  void Disable() {
    ++network_epoch; network = network_ready = false; body_pending = skill_pending = 0;
    for (const auto& target : targets) Method(target, L"Network.disable", JsonObject{});
    Method("", L"Network.disable", JsonObject{});
    JsonObject args; Flag(args, L"autoAttach", false); Flag(args, L"waitForDebuggerOnStart", false); Flag(args, L"flatten");
    Method("", L"Target.setAutoAttach", args); targets.clear(); pending.clear(); excluded.clear(); capture_id.clear();
  }
  void Tick() {
    const auto next = eligible() ? session.Active() : "";
    const bool wanted = eligible() && (listening || !next.empty());
    const bool new_capture = next != capture_id && !next.empty();
    capture_id = next;
    if (!wanted && network) { Disable(); ResetCooldown(); }
    if (wanted && !network) { network = true; ++network_epoch; Enable(""); }
    else if (wanted && new_capture && network_ready) {
      JsonObject record; Put(record, L"state", "ready"); Put(record, L"target_session", ""); Emit("", "observer_ready", record);
    }
  }
};
Observer::Observer(ICoreWebView2* core, Session& session, size_t view, std::function<bool()> eligible)
    : state_(std::make_shared<State>(core, session, view, std::move(eligible))) {
  for (const auto* name : {"Target.attachedToTarget", "Target.detachedFromTarget", "Network.requestWillBeSent", "Network.responseReceived",
      "Network.loadingFinished", "Network.loadingFailed", "Network.webSocketCreated", "Network.webSocketWillSendHandshakeRequest",
      "Network.webSocketHandshakeResponseReceived", "Network.webSocketFrameSent", "Network.webSocketFrameReceived", "Network.webSocketClosed", "Network.webSocketFrameError"}) state_->Subscribe(name);
}
Observer::~Observer() {
  state_->alive = false;
  if (state_->network) state_->Disable();
  for (const auto& subscription : state_->subscriptions) subscription.receiver->remove_DevToolsProtocolEventReceived(subscription.token);
}
void Observer::Tick() {
  state_->Tick();
}
void Observer::Listen(bool enabled, std::function<void(const cooldown::Snapshot&)> changed,
    std::function<bool(const std::string&)> document) {
  state_->cooldown_changed = std::move(changed);
  state_->cooldown_document = std::move(document);
  if (enabled != state_->listening) { state_->listening = enabled; state_->ResetCooldown(); }
  Tick();
}
void Observer::ResetCooldown() { state_->ResetCooldown(); }
void CheckObserverEvents() {
  {
    uint64_t tick = 0;
    Session idle(8192, 2048, [&] { return tick; });
    const auto id = Text(Object(idle.Call("gbf_capture_start", R"({"mode":"rolling"})")), L"capture_id");
    idle.Add(id, 0, "idle", "request", "{}");
    tick = 599999; idle.Tick();
    if (idle.state_->records.size() != 1) throw std::runtime_error("idle sweep expired too early");
    tick = 600000; idle.Tick(); // No observer, status/read RPC or incoming event drives this cleanup.
    if (!idle.state_->records.empty() || idle.state_->bytes != 0 || idle.state_->expired != 1 || idle.state_->status != "active")
      throw std::runtime_error("idle rolling sweep failed");
    idle.Add(id, 0, "old-at-stop", "request", "{}");
    tick += 300000; idle.Add(id, 0, "live-at-stop", "request", "{}");
    tick += 300000; idle.Stop();
    if (idle.state_->records.size() != 1 || idle.state_->expired != 2) throw std::runtime_error("native stop skipped final sweep");
    tick += 86400000; idle.Tick();
    if (idle.state_->records.size() != 1 || idle.state_->expired != 2 || idle.state_->status != "stopped")
      throw std::runtime_error("native stop did not freeze snapshot");
    idle.Clear();
    if (idle.state_->expired || idle.state_->evicted_through || idle.state_->retention || idle.state_->mode != "bounded")
      throw std::runtime_error("native clear retained rolling state");
  }
  {
    Session buffer;
    const auto id = Text(Object(buffer.Call("gbf_capture_start", R"({"mode":"rolling"})")), L"capture_id");
    auto live = std::make_shared<Observer::State>(nullptr, buffer, 0, [] { return true; });
    live->capture_id = id; live->targets = {"child", "child-long"};
    live->pending["child:request"] = {}; live->pending["child-long:request"] = {}; live->pending[":root"] = {};
    live->excluded = {"child:excluded", "child-long:excluded", ":root-excluded"};
    live->Event("Target.detachedFromTarget", "", Object(R"({"sessionId":""})"));
    live->Event("Target.detachedFromTarget", "", Object(R"({"sessionId":"unknown"})"));
    if (live->pending.size() != 3 || live->excluded.size() != 3) throw std::runtime_error("invalid detach removed live sources");
    live->Event("Target.detachedFromTarget", "", Object(R"({"sessionId":"child"})"));
    if (live->targets.count("child") || live->pending.size() != 2 || live->excluded.size() != 2 ||
        live->pending.count("child:request") || live->excluded.count("child:excluded") || !live->targets.count("child-long"))
      throw std::runtime_error("target detach failed to release only its associations");
    const auto before = buffer.state_->next;
    live->Event("Network.responseReceived", "child", Object(R"({"requestId":"excluded","response":{"status":200}})"));
    live->Event("Network.webSocketFrameReceived", "child", Object(R"({"requestId":"excluded","response":{"opcode":1,"payloadData":"{}"}})"));
    Observer::State::Pending request;
    request.epoch = live->cooldown_state.epoch(); request.sequence = 1;
    live->listening = true; live->body_pending = live->skill_pending = 1;
    unsigned deliveries = 0; live->cooldown_changed = [&](const cooldown::Snapshot&) { ++deliveries; };
    const auto body = Object(R"({"body":"{\"ability\":{}}"})");
    live->CompleteBody(live->network_epoch, id, "child:request", request, false, S_OK, body);
    live->CompleteBody(live->network_epoch, id, "child:request", request, true, S_OK, body);
    if (buffer.state_->next != before || live->body_pending || live->skill_pending || deliveries ||
        live->pending.size() != 2 || live->excluded.size() != 2)
      throw std::runtime_error("detached target late event/body bypassed source guard");
    // Attached exclusions survive arbitrarily long captures; only terminal events/detach may release them.
    live->Event("Network.responseReceived", "child-long", Object(R"({"requestId":"excluded","response":{"status":200}})"));
    if (buffer.state_->next != before || !live->excluded.count("child-long:excluded"))
      throw std::runtime_error("attached excluded source was exposed");
  }
  Session session; bool eligible = true;
  const auto id = Text(Object(session.Call("gbf_capture_start", "{}")), L"capture_id");
  auto observer = std::make_shared<Observer::State>(nullptr, session, 0, [&eligible] { return eligible; });
  observer->capture_id = id;
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"1","timestamp":1,"documentURL":"https://game.granbluefantasy.jp/","request":{"url":"https://arbitrary-cdn.test/api","method":"POST","headers":{"Cookie":"hidden"},"postData":"{\"skill\":3,\"token\":\"hidden\"}"}})"));
  observer->Event("Network.responseReceived", "", Object(R"({"requestId":"1","timestamp":2,"response":{"status":200,"mimeType":"application/json","headers":{"Set-Cookie":"hidden"}}})"));
  observer->Event("Network.loadingFailed", "", Object(R"({"requestId":"1","timestamp":3,"canceled":true,"errorText":"net::ERR_ABORTED"})"));
  const auto before = session.Call("gbf_capture_status", "{\"capture_id\":\"" + id + "\"}");
  eligible = false;
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"ordinary","request":{"url":"https://other.test/"}})"));
  eligible = true;
  observer->Event("Network.requestWillBeSent", "worker-session", Object(R"({"requestId":"worker","request":{"url":"https://game.test/"}})"));
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"extension","initiator":{"url":"chrome-extension://example/background.js"},"request":{"url":"https://game.test/"}})"));
  const auto after_filter = Object(session.Call("gbf_capture_status", "{\"capture_id\":\"" + id + "\"}"));
  if (Object(before).GetNamedNumber(L"record_count") != after_filter.GetNamedNumber(L"record_count") ||
      after_filter.GetNamedNumber(L"filtered_requests") != 1) throw std::runtime_error("capture scope leaked or exclusions not counted");
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"wrapped","initiator":{"type":"script","stack":{"callFrames":[{"url":"chrome-extension://example/wrapper.js"},{"url":"https://game.test/battle.js"}]}},"request":{"url":"https://game.test/attack","method":"POST"}})"));
  observer->Event("Network.responseReceived", "", Object(R"({"requestId":"wrapped","response":{"status":200,"mimeType":"application/json"}})"));
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"url-text","initiator":{"url":"https://game.test/?text=chrome-extension://example"},"request":{"url":"https://game.test/api"}})"));
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"extension-stack","initiator":{"stack":{"callFrames":[{"url":"chrome-extension://example/script.js"}]}},"request":{"url":"https://game.test/"}})"));
  observer->Event("Network.responseReceived", "", Object(R"({"requestId":"extension-stack","response":{"status":200}})"));
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"extension-document","documentURL":"chrome-extension://example/page.html","request":{"url":"https://game.test/"}})"));
  observer->Event("Network.requestWillBeSent", "", Object(R"({"requestId":"extension-url","request":{"url":"chrome-extension://example/script.js"}})"));
  if (Object(session.Call("gbf_capture_status", "{\"capture_id\":\"" + id + "\"}")).GetNamedNumber(L"filtered_requests") != 4)
    throw std::runtime_error("capture extension filter diagnostics failed");
  unsigned document_index = 0;
  for (const auto* document : {"https://game.granbluefantasy.jp/#raid/example", "https://gbf.game.mbga.jp/#raid/example"}) {
    auto event = Object(R"({"initiator":{"type":"script","stack":{"callFrames":[{"url":"chrome-extension://example/wrapper.js"}]}},"request":{"url":"https://arbitrary-cdn.test/battle","method":"POST","headers":{"Cookie":"hidden"},"postData":"{\"skill_cd\":6,\"token\":\"hidden\"}"}})");
    Put(event, L"requestId", "document-wrapped-" + std::to_string(document_index++)); Put(event, L"documentURL", document);
    observer->Event("Network.requestWillBeSent", "", event);
    JsonObject response; Put(response, L"requestId", Text(event, L"requestId"));
    response.SetNamedValue(L"response", Object(R"({"status":200,"mimeType":"application/json"})"));
    observer->Event("Network.responseReceived", "", response);
  }
  unsigned expected_filtered = 4;
  for (const auto* document : {"https://other.test/", "https://game.granbluefantasy.jp.evil.test/",
      "https://game.granbluefantasy.jp@evil.test/", "https://connect.mobage.jp/", "chrome-extension://example/page.html"}) {
    auto event = Object(R"({"initiator":{"stack":{"callFrames":[{"url":"chrome-extension://example/wrapper.js"}]}},"request":{"url":"https://game.granbluefantasy.jp/battle","method":"POST"}})");
    Put(event, L"requestId", "rejected-document-" + std::to_string(expected_filtered++)); Put(event, L"documentURL", document);
    observer->Event("Network.requestWillBeSent", "", event);
  }
  auto extension_resource = Object(R"({"requestId":"rejected-extension-resource","documentURL":"https://game.granbluefantasy.jp/","request":{"url":"chrome-extension://example/script.js"}})");
  observer->Event("Network.requestWillBeSent", "", extension_resource); ++expected_filtered;
  const auto filtered_status = Object(session.Call("gbf_capture_status", "{\"capture_id\":\"" + id + "\"}"));
  if (filtered_status.GetNamedNumber(L"filtered_requests") != expected_filtered)
    throw std::runtime_error("capture game document exception widened source scope");
  auto unbound = Object(R"({"requestId":"rejected-target","documentURL":"https://game.granbluefantasy.jp/","request":{"url":"https://game.granbluefantasy.jp/battle"}})");
  observer->Event("Network.requestWillBeSent", "unattached-extension-target", unbound);
  if (!IsCapturableChildTarget(Object(R"({"type":"worker","url":"https://game.test/worker.js"})")) ||
      IsCapturableChildTarget(Object(R"({"type":"service_worker","url":"https://game.test/sw.js"})")) ||
      IsCapturableChildTarget(Object(R"({"type":"shared_worker","url":"https://game.test/shared.js"})")) ||
      IsCapturableChildTarget(Object(R"({"type":"background_page","url":"chrome-extension://example/background.html"})")) ||
      IsCapturableChildTarget(Object(R"({"type":"worker","url":"chrome-extension://example/worker.js"})")))
    throw std::runtime_error("capture child target scope failed");
  observer->targets.insert("game-worker");
  observer->Event("Network.requestWillBeSent", "game-worker", Object(R"({"requestId":"worker","timestamp":4,"request":{"url":"https://game.test/combat","method":"POST"}})"));
  observer->Event("Network.webSocketCreated", "", Object(R"({"requestId":"socket","url":"wss://different.test/"})"));
  observer->Event("Network.webSocketFrameReceived", "", Object(R"({"requestId":"socket","timestamp":4,"response":{"opcode":1,"payloadData":"{\"damage\":42,\"secret\":\"hidden\"}"}})"));
  observer->Event("Network.webSocketFrameSent", "", Object(R"({"requestId":"socket","timestamp":5,"response":{"opcode":2,"payloadData":"aGlkZGVu"}})"));
  observer->Event("Network.webSocketFrameReceived", "", Object(R"({"requestId":"preexisting","timestamp":6,"response":{"opcode":1,"payloadData":"{\"skill_cd\":2}"}})"));
  observer->Event("Network.webSocketFrameReceived", "", Object(R"({"requestId":"socket","timestamp":7,"response":{"opcode":1,"payloadData":"42[\"battle\",{\"party\":[{\"abilities\":[{\"cooldown\":4},{\"recast\":7}]}],\"token\":\"hidden\"}]"}})"));
  observer->Event("Network.webSocketFrameReceived", "", Object(R"({"requestId":"socket","timestamp":8,"response":{"opcode":1,"payloadData":"ability_cd=9&session=hidden"}})"));
  observer->targets.insert("iframe-first"); observer->targets.insert("iframe-second");
  for (const auto* target : {"iframe-first", "iframe-second"}) {
    observer->Event("Network.requestWillBeSent", target, Object(R"({"requestId":"shared-id","timestamp":7,"request":{"url":"https://other.test/api","method":"GET"}})"));
    observer->Event("Network.responseReceived", target, Object(R"({"requestId":"shared-id","timestamp":8,"response":{"status":200}})"));
  }
  const auto read = session.Call("gbf_capture_read", "{\"capture_id\":\"" + id + "\"}");
  if (read.find("hidden") != std::string::npos || read.find("arbitrary-cdn.test") == std::string::npos ||
      read.find("websocket_frame") == std::string::npos || read.find("canceled") == std::string::npos || read.find("game-worker") == std::string::npos ||
      read.find("skill_cd") == std::string::npos || read.find("request_context_unavailable") == std::string::npos) throw std::runtime_error("capture synthetic observer failed");
  bool parsed_frame = false, parsed_form = false, mixed_source = false, url_text = false;
  unsigned game_document_requests = 0, game_document_responses = 0;
  for (const auto& item : Object(read).GetNamedArray(L"records")) {
    const auto record = item.GetObject();
    const auto request_id = Text(record, L"cdp_request_id");
    if (request_id == "wrapped" && Text(record, L"source_attribution") == "mixed_page_extension") mixed_source = true;
    if (request_id == "url-text") url_text = true;
    if (request_id.rfind("document-wrapped-", 0) == 0) {
      if (Text(record, L"kind") == "request" && Text(record, L"source_attribution") == "game_document_extension_initiator") ++game_document_requests;
      if (Text(record, L"kind") == "response") ++game_document_responses;
    }
    if (request_id == "extension-stack" || request_id == "extension-document" || request_id == "extension-url" || request_id.rfind("rejected-", 0) == 0)
      throw std::runtime_error("capture extension source leaked");
    if (Text(record, L"kind") != "websocket_frame") continue;
    const auto extraction = record.GetNamedObject(L"gbf_cd_extraction", JsonObject{});
    const auto fields = extraction.GetNamedArray(L"fields", JsonArray{});
    if (Text(extraction, L"state") == "matched" && fields.Size() == 2 && record.GetNamedBoolean(L"body_unavailable")) {
      std::set<int> values;
      for (const auto& field : fields) values.insert(static_cast<int>(field.GetObject().GetNamedNumber(L"value")));
      if (values == std::set<int>{4, 7}) parsed_frame = true;
    }
    if (Text(extraction, L"state") == "matched" && fields.Size() == 1) {
      const auto field = fields.GetObjectAt(0);
      if (Text(field, L"field") == "ability_cd" && field.GetNamedNumber(L"value") == 9) parsed_form = true;
    }
  }
  if (!parsed_frame || !parsed_form) throw std::runtime_error("GBF websocket cooldown extraction failed");
  if (!mixed_source || !url_text) throw std::runtime_error("capture wrapped game request was filtered");
  if (game_document_requests != 2 || game_document_responses != 2)
    throw std::runtime_error("capture game document extension-only stack was filtered or unmarked");
  std::map<std::string, unsigned> linked;
  for (const auto& item : Object(read).GetNamedArray(L"records")) {
    const auto record = item.GetObject();
    if (Text(record, L"cdp_request_id") == "shared-id") {
      const auto target = Text(record, L"target_session");
      if (Text(record, L"request_id") != target + ":shared-id") throw std::runtime_error("capture target identity missing");
      ++linked[target];
    }
  }
  if (linked.size() != 2 || linked["iframe-first"] != 2 || linked["iframe-second"] != 2) throw std::runtime_error("capture cross-target association collided");
  session.Loss(id); session.state_->truncated = 1;
  session.Clear();
  const auto cleared = session.state_->Status();
  if (!Text(cleared, L"capture_id").empty() || Text(cleared, L"state") != "cleared" || session.state_->monotonic_deadline != 0)
    throw std::runtime_error("capture native clear retained identity");
  for (const auto* field : {L"started_at_ms", L"deadline_ms", L"retention_seconds", L"expired_records", L"buffer_bytes", L"record_count", L"dropped_records", L"truncated_records", L"filtered_requests", L"next_cursor"})
    if (cleared.GetNamedNumber(field) != 0) throw std::runtime_error("capture native clear retained state");
  const auto another = Text(Object(session.Call("gbf_capture_start", "{}")), L"capture_id");
  if (session.Call("gbf_capture_clear", "{\"capture_id\":\"" + another + "\"}") != Encode(cleared)) throw std::runtime_error("capture clear paths diverged");
  {
    uint64_t tick = 0;
    Session buffer(8192, 2048, [&] { return tick; });
    bool game = true;
    auto live = std::make_shared<Observer::State>(nullptr, buffer, 4, [&] { return game; });
    live->listening = live->network = true;
    cooldown::Snapshot shown;
    live->cooldown_changed = [&](const cooldown::Snapshot& snapshot) { shown = snapshot; };
    const auto request = Object(R"({"requestId":"skill","documentURL":"https://game.granbluefantasy.jp/#raid_multi/example","initiator":{"url":"chrome-extension://example/wrapper.js"},"request":{"url":"https://game.granbluefantasy.jp/rest/multiraid/ability_result.json","method":"POST"}})");
    live->Event("Network.requestWillBeSent", "", request);
    const auto ticket = live->pending.at(":skill");
    if (!ticket.skill || !ticket.expects_snapshot) throw std::runtime_error("CD listener did not track wrapped game request without MCP");
    JsonObject body;
    Put(body, L"body", R"({"status":{"ability":{"4":{"pos":3,"list":{"3":[{"ability-recast":8}]}}}}})");
    live->CompleteBody(live->network_epoch, "", ":skill", ticket, true, S_OK, body);
    if (shown.cells[3][2] != 8 || buffer.state_->bytes != 0) throw std::runtime_error("CD-only listener failed or retained capture records");
    const auto capture = Text(Object(buffer.Call("gbf_capture_start", R"({"mode":"rolling"})")), L"capture_id");
    live->capture_id = capture;
    live->Event("Network.requestWillBeSent", "", request);
    const auto epoch = live->network_epoch;
    tick = 86400000; buffer.Tick(); live->Tick();
    if (live->capture_id != capture || live->network_epoch != epoch || !live->network || !buffer.state_->records.empty())
      throw std::runtime_error("rolling window reset the continuous observer");
    live->Event("Network.responseReceived", "", Object(R"({"requestId":"skill","response":{"status":200,"mimeType":"application/json"}})"));
    if (buffer.state_->records.size() != 1 || live->pending.count(":skill") != 1)
      throw std::runtime_error("rolling eviction broke in-flight request correlation");
    buffer.Stop(); live->Tick();
    if (!live->network || !live->capture_id.empty()) throw std::runtime_error("MCP stop disabled continuous CD");
    buffer.Clear(); live->Tick();
    if (!live->network || shown.cells[3][2] != 8) throw std::runtime_error("MCP clear reset continuous CD");
    live->ResetCooldown();
    live->CompleteBody(live->network_epoch, "", ":skill", ticket, true, S_OK, body);
    if (shown.cells[3][2] != cooldown::kUnknown) throw std::runtime_error("CD old document callback accepted");
    live->Event("Network.requestWillBeSent", "", request);
    const auto current = live->pending.at(":skill");
    live->CompleteBody(live->network_epoch, "", ":skill", current, true, S_OK, body);
    if (shown.cells[3][2] != 8) throw std::runtime_error("CD post-reset snapshot missing");
    live->CompleteBody(live->network_epoch, "", ":skill", current, true, E_FAIL, JsonObject{});
    if (shown.cells[3][2] != cooldown::kUnknown) throw std::runtime_error("CD unavailable body left stale value");
    live->cooldown_document = [](const std::string&) { return false; };
    live->Event("Network.requestWillBeSent", "", request);
    if (live->pending.at(":skill").skill) throw std::runtime_error("CD request after leaving battle was tracked");
    const auto next_capture = Text(Object(buffer.Call("gbf_capture_start", "{}")), L"capture_id");
    live->capture_id = next_capture; live->listening = false; live->Tick();
    if (!live->network) throw std::runtime_error("CD close stopped MCP capture");
    buffer.Stop(); live->Tick();
    if (live->network) throw std::runtime_error("unused network observer left enabled");
    live->listening = live->network = true; game = false; live->Tick();
    if (live->network) throw std::runtime_error("CD observed non-game navigation");
  }
}
}
