#include "network_capture.h"
#include "capture_json.h"
#include <iostream>
#include <stdexcept>

namespace gbrowser::webview::capture {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string Identity(const std::string& id) { JsonObject args; Put(args, L"capture_id", id); return Encode(args); }
bool Rejects(Session& session, const std::string& method, const std::string& args) {
  try { session.Call(method, args); } catch (...) { return true; }
  return false;
}
std::string Arguments(const std::string& id, uint64_t cursor = 0) {
  JsonObject args; Put(args, L"capture_id", id); Number(args, L"cursor", static_cast<double>(cursor)); Number(args, L"limit", 1); return Encode(args);
}
}
void RunCaptureChecks() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  {
    using namespace cooldown;
    const auto start = Parse(R"({"ability":{"4":{"pos":3,"list":{"3":[{"ability-recast":"8","ability-character-num":"3"}],"1":[{"ability-recast":"7"}],"2":[{"ability-recast":10}]}},"3":{"pos":"2","list":{"1":[{"ability-recast":"6","recaset-default":"9999"}],"2":[{"ability-recast":0}],"3":[{"ability-recast":"0"}]}}}})");
    Require(start && start->cells[2] == std::array<int, 4>{6, 0, 0, kMissing} &&
        start->cells[3] == std::array<int, 4>{7, 10, 8, kMissing} && start->cells[0][0] == kUnknown,
        "CD initialization positional/string/number/missing decoding failed");
    const auto attack = Parse(R"({"status":{"ability":{"arbitrary":{"pos":2,"list":{"1":[{"ability-recast":4}]}}}}})");
    Require(attack && attack->cells[2][0] == 4, "CD attack nested snapshot failed");
    const auto reset = Parse(R"({"status":{"ability":{"2":{"pos":1,"list":{"1":[{"ability-recast":0}],"2":[{"ability-recast":"0"}],"3":[{"ability-recast":0}]}}}}})");
    Require(reset && reset->cells[1] == std::array<int, 4>{0, 0, 0, kMissing}, "CD reset was not authoritative");
    const auto invalid = Parse(R"({"ability":{"1":{"pos":0,"list":{"1":[{"ability-recast":-1}],"2":[{"ability-recast":1.5}],"3":[{"ability-recast":"6x"}],"4":[{"ability-recast":true}]}},"2":{"pos":1,"list":{"1":[{"ability-recast":0,"ability-character-num":3}]}}}})");
    Require(invalid && invalid->cells[0] == Snapshot{}.cells[0] && invalid->cells[1][0] == kUnknown,
        "CD invalid values or foreign character falsely became ready");
    Require(!Parse("opaque") && !Parse(R"({"player":{"recast":100},"summon":{"recast":5}})") &&
        !Parse(std::string(1024 * 1024 + 1, 'x')), "CD parser accepted unrelated or oversized data");
    const auto malformed = Parse(R"({"status":{"ability":[]}})");
    Require(malformed && malformed->cells == Snapshot{}.cells, "CD malformed snapshot retained stale data");
    const auto duplicate = Parse(R"({"ability":{"1":{"pos":0,"list":{"1":[{"ability-recast":6}]}},"2":{"pos":0,"list":{"1":[{"ability-recast":2}]}}}})");
    Require(duplicate && duplicate->cells[0] == Snapshot{}.cells[0], "CD duplicate position accepted");
    State main, secondary;
    const auto epoch = main.epoch();
    Require(main.Apply(epoch, 3, *attack) && !main.Apply(epoch, 2, *start) && main.snapshot().cells[2][0] == 4,
        "CD late callback rolled back state");
    Require(secondary.Apply(secondary.epoch(), 1, *start) && secondary.snapshot().cells[2][0] == 6,
        "CD view isolation failed");
    main.Reset();
    Require(!main.Apply(epoch, 4, *start) && main.snapshot().cells[2][0] == kUnknown,
        "CD old navigation resurrected after reset");
    Require(BattleResponse("https://game.granbluefantasy.jp/rest/multiraid/ability_result.json?x=1") &&
        BattleResponse("https://gbf.game.mbga.jp/rest/raid/summon_result.json") &&
        !BattleResponse("https://game.granbluefantasy.jp.evil.test/rest/raid/start.json") &&
        !BattleResponse("https://game.granbluefantasy.jp/assets/start.json") &&
        BattleRoute("https://game.granbluefantasy.jp/#raid_multi/123") == "raid_multi/123" &&
        BattleRoute("https://game.granbluefantasy.jp/#mypage").empty(), "CD URL scope failed");
    std::cout << "PASS skill CD parser: snapshots/types/positions/resets/unknown/order/view isolation\n";
  }
  {
    bool redacted = false;
    const auto url = RedactUrl("https://name:secret@example.test/api?x=1&access_token=hidden&%73ession=hidden#route", redacted);
    Require(redacted && url.find("hidden") == std::string::npos && url.find("name:secret") == std::string::npos, "capture URL credentials leaked");
    redacted = false;
    Require(RedactUrl("data:text/plain;base64,aGlkZGVu", redacted) == "data:[REDACTED]" && redacted,
        "inline URL payload bypassed body redaction");
    Require(Encode(Body(R"({"image":"DATA:text/plain,hidden"})", "application/json")).find("hidden") == std::string::npos,
        "inline JSON URL payload bypassed body redaction");
    const auto body = Body(R"({"damage":42,"nested":[{"refresh_token":"hidden","sessionId":"hidden"}],"url":"https://example.test/?password=hidden"})", "application/json");
    Require(Encode(body).find("hidden") == std::string::npos && body.GetNamedBoolean(L"redacted"), "capture JSON credentials leaked");
    const auto form = Body("turn=3&password=hidden&signature=hidden", "application/x-www-form-urlencoded");
    Require(Encode(form).find("hidden") == std::string::npos && Text(form, L"text").find("turn=3") != std::string::npos, "capture form credentials leaked");
    const auto cd_frame = GbfCooldownExtraction(R"(42["battle",{"party":[{"abilities":[{"cooldown":4},{"recast":"7"}]}],"token":"hidden"}])");
    Require(Text(cd_frame, L"state") == "matched" && cd_frame.GetNamedArray(L"fields").Size() == 2 &&
        Encode(cd_frame).find("hidden") == std::string::npos && Encode(cd_frame).find("battle") == std::string::npos,
        "GBF WebSocket CD extractor exposed non-CD data");
    const auto cd_form = GbfCooldownExtraction("skill_cd=6&password=hidden");
    Require(Text(cd_form, L"state") == "matched" && cd_form.GetNamedArray(L"fields").Size() == 1 &&
        Encode(cd_form).find("hidden") == std::string::npos, "GBF WebSocket form CD extraction failed");
    const auto no_cd = GbfCooldownExtraction(R"({"damage":42,"session":"hidden"})");
    Require(Text(no_cd, L"state") == "no_cd_fields" && Encode(no_cd).find("hidden") == std::string::npos,
        "GBF WebSocket no-match result leaked content");
    Require(Text(GbfCooldownExtraction("opaque", false), L"state") == "binary_skipped",
        "GBF WebSocket binary frame was parsed");
    const auto sensitive_cd = GbfCooldownExtraction(R"({"secret":{"skill_cd":42},"%2573ession":{"abilities":[{"cd":43}]},"cooldown":{"token":{"value":44}},"abilities":[{"cd":5}]})");
    const auto safe_fields = sensitive_cd.GetNamedArray(L"fields");
    Require(safe_fields.Size() == 1 && safe_fields.GetObjectAt(0).GetNamedNumber(L"value") == 5,
        "CD extractor traversed a sensitive subtree or missed abilities context");
    const auto duplicate_cd = GbfCooldownExtraction(R"({"first":{"cooldown":5},"second":{"cooldown":5}})");
    const auto duplicates = duplicate_cd.GetNamedArray(L"fields");
    Require(duplicates.Size() == 2 && Encode(duplicates.GetObjectAt(0).GetNamedArray(L"location")) !=
        Encode(duplicates.GetObjectAt(1).GetNamedArray(L"location")), "distinct CD candidates were merged");
    const auto duplicate_form = GbfCooldownExtraction("ability_cd=5&ability_cd=5&session[skill_cd]=42");
    Require(duplicate_form.GetNamedArray(L"fields").Size() == 2, "form candidates were merged or credentials extracted");
    const auto numeric_cd = GbfCooldownExtraction(R"({"skill_cd":[0,99,"7",-1,100,1.5,"7x",true,null]})");
    Require(numeric_cd.GetNamedArray(L"fields").Size() == 3, "CD integer bounds failed");
    Require(Text(GbfCooldownExtraction(R"(~m~12~m~{"skill_cd":6})"), L"state") == "unrecognized_frame" &&
        Text(GbfCooldownExtraction(R"(42["event",{"skill_cd":6}]trailing)"), L"state") == "unrecognized_frame",
        "unvalidated framing accepted");
    Require(Text(GbfCooldownExtraction(std::string(1024 * 1024 + 1, 'x')), L"state") == "body_limit",
        "CD body budget failed");
    JsonArray many;
    for (unsigned index = 0; index < 65; ++index) many.Append(JsonValue::CreateNumberValue(5));
    JsonObject many_cd; many_cd.SetNamedValue(L"skill_cd", many);
    const auto capped = GbfCooldownExtraction(Encode(many_cd));
    Require(capped.GetNamedArray(L"fields").Size() == 64 && capped.GetNamedBoolean(L"truncated"), "CD field cap was silent");
    JsonArray large_array;
    for (unsigned index = 0; index < 4100; ++index) large_array.Append(JsonValue::CreateNumberValue(0));
    Require(GbfCooldownExtraction(Encode(large_array)).GetNamedBoolean(L"truncated"), "CD traversal cap was silent");
    std::string deep = "{\"skill_cd\":5}";
    for (unsigned index = 0; index < 34; ++index) deep = "[" + deep + "]";
    Require(GbfCooldownExtraction(deep).GetNamedBoolean(L"truncated"), "CD depth cap was silent");
    Require(Body("aGlkZGVu", "image/png", true).GetNamedBoolean(L"body_unavailable"), "opaque binary was exposed");
    Require(Body("password is hidden", "text/plain").GetNamedBoolean(L"body_unavailable"), "opaque text was exposed");
    const auto headers = Headers(Object(R"({"Cookie":"hidden","Set-Cookie":"hidden","AUTHORIZATION":"hidden","Proxy-Authorization":"hidden","X-Api-Key":"hidden","Content-Type":"application/json"})"), redacted);
    Require(Encode(headers).find("hidden") == std::string::npos, "capture authentication headers leaked");
  }
  {
    uint64_t tick = 1000;
    Session session(8192, 2048, [&] { return tick; });
    const auto start = Object(session.Call("gbf_capture_start", "{}")); const auto id = Text(start, L"capture_id");
    Require(start.GetNamedNumber(L"deadline_ms") - start.GetNamedNumber(L"started_at_ms") == 120000, "default capture duration changed");
    bool rejected = false; try { session.Call("gbf_capture_start", "{}"); } catch (...) { rejected = true; }
    Require(rejected, "parallel capture allowed");
    session.Add(id, 0, "request-1", "request", R"({"url":"https://other.test/api","method":"POST"})");
    session.Add(id, 0, "request-1", "response", R"({"status":200,"json":{"skills":[{"cd":3}]}})");
    auto read = Object(session.Call("gbf_capture_read", Arguments(id)));
    Require(read.GetNamedArray(L"records").Size() == 1 && read.GetNamedBoolean(L"has_more"), "capture pagination failed");
    read = Object(session.Call("gbf_capture_read", Arguments(id, 1)));
    Require(Text(read.GetNamedArray(L"records").GetObjectAt(0), L"request_id") == "request-1", "capture association lost");
    const auto count = read.GetNamedNumber(L"record_count");
    session.Stop(); session.Add(id, 0, "ignored", "request", "{}");
    Require(Object(session.Call("gbf_capture_status", Identity(id))).GetNamedNumber(L"record_count") == count, "stopped capture still recorded");
    for (const char* duration : {"0", "601", "1.5"}) {
      rejected = false; try { session.Call("gbf_capture_start", std::string("{\"duration_seconds\":") + duration + "}"); } catch (...) { rejected = true; }
      Require(rejected, "invalid capture duration accepted");
    }
    const auto next = Text(Object(session.Call("gbf_capture_start", "{\"duration_seconds\":600}")), L"capture_id");
    for (int index = 0; index < 50; ++index) { JsonObject item; Put(item, L"padding", std::string(500, 'x')); session.Add(next, 4, "bulk", "request", Encode(item)); }
    auto status = Object(session.Call("gbf_capture_status", Identity(next)));
    Require(status.GetNamedNumber(L"dropped_records") > 0 && status.GetNamedNumber(L"buffer_bytes") <= 8192, "capture memory limit failed");
    Require(Object(session.Call("gbf_capture_read", Arguments(next))).GetNamedBoolean(L"cursor_gap"), "capture loss was silent");
    JsonObject large; Put(large, L"padding", std::string(4000, 'x')); session.Add(next, 4, "large", "response", Encode(large));
    Require(Object(session.Call("gbf_capture_status", Identity(next))).GetNamedNumber(L"truncated_records") == 1, "record limit not reported");
    session.Call("gbf_capture_clear", Identity(next)); Require(session.Active().empty(), "capture clear remained active");
    rejected = false; try { session.Call("gbf_capture_read", Arguments(next)); } catch (...) { rejected = true; }
    Require(rejected, "cleared records readable");
    const auto timed = Text(Object(session.Call("gbf_capture_start", "{\"duration_seconds\":1}")), L"capture_id");
    tick += 1000; Require(session.Active().empty(), "capture did not expire at the monotonic deadline");
    Require(Text(Object(session.Call("gbf_capture_status", Identity(timed))), L"state") == "expired", "expiry state missing");
  }
  {
    uint64_t tick = 0;
    Session session(8192, 2048, [&] { return tick; });
    auto start = Object(session.Call("gbf_capture_start", "{}"));
    auto id = Text(start, L"capture_id");
    session.Add(id, 0, "snapshot", "request", "{}"); session.Stop();
    const auto snapshot = session.Call("gbf_capture_status", Identity(id));
    for (const auto* invalid : {R"({"mode":null})", R"({"mode":false})", R"({"mode":1})", R"({"mode":{}})",
        R"({"mode":[]})", R"({"mode":"ROLLING"})", R"({"mode":""})", R"({"unknown":1})",
        R"({"duration_seconds":null})", R"({"duration_seconds":true})", R"({"duration_seconds":"120"})",
        R"({"duration_seconds":{}})", R"({"duration_seconds":[]})", R"({"duration_seconds":1e100})",
        R"({"retention_seconds":600})", R"({"mode":"bounded","retention_seconds":600})",
        R"({"mode":"rolling","duration_seconds":600})", R"({"mode":"rolling","duration_seconds":null})",
        R"({"mode":"rolling","retention_seconds":600,"duration_seconds":1})",
        R"({"mode":"rolling","retention_seconds":null})", R"({"mode":"rolling","retention_seconds":true})",
        R"({"mode":"rolling","retention_seconds":"600"})", R"({"mode":"rolling","retention_seconds":[]})",
        R"({"mode":"rolling","retention_seconds":{}})", R"({"mode":"rolling","retention_seconds":599})",
        R"({"mode":"rolling","retention_seconds":601})", R"({"mode":"rolling","retention_seconds":600.5})",
        R"({"mode":"rolling","retention_seconds":1e100})", R"({"mode":"rolling","extra":true})"}) {
      Require(Rejects(session, "gbf_capture_start", invalid), "invalid capture start accepted");
      Require(session.Call("gbf_capture_status", Identity(id)) == snapshot, "invalid start replaced the stopped snapshot");
    }
    for (const auto* field : {L"cursor", L"limit"}) for (const auto* invalid : {"null", "true", R"("1")", "[]", "{}", "-1", "0.5", "1e100"}) {
      auto args = Object(Identity(id)); args.SetNamedValue(field, JsonValue::Parse(winrt::to_hstring(invalid)));
      Require(Rejects(session, "gbf_capture_read", Encode(args)), "invalid pagination type/range accepted");
    }
    Require(Rejects(session, "gbf_capture_status", Arguments(id)) &&
        Rejects(session, "gbf_capture_stop", R"({"capture_id":null})") &&
        Rejects(session, "gbf_capture_clear", "{}"), "invalid identity/unknown fields accepted");
    start = Object(session.Call("gbf_capture_start", R"({"mode":"bounded","duration_seconds":600})"));
    id = Text(start, L"capture_id");
    Require(Text(start, L"mode") == "bounded" && start.GetNamedNumber(L"retention_seconds") == 0 &&
        start.GetNamedNumber(L"expired_records") == 0 && start.GetNamedNumber(L"deadline_ms") - start.GetNamedNumber(L"started_at_ms") == 600000,
        "explicit bounded contract changed");
    session.Add(id, 0, "bounded", "request", "{}"); tick = 600000; session.Tick();
    auto state = Object(session.Call("gbf_capture_status", Identity(id)));
    Require(Text(state, L"state") == "expired" && state.GetNamedNumber(L"record_count") == 1 &&
        state.GetNamedNumber(L"expired_records") == 0, "bounded expiry removed its snapshot");
    tick += 86400000; session.Tick();
    Require(session.Call("gbf_capture_status", Identity(id)) == Encode(state), "bounded snapshot acquired rolling TTL");
  }
  {
    uint64_t tick = 1000;
    Session session(8192, 2048, [&] { return tick; });
    const auto start = Object(session.Call("gbf_capture_start", R"({"mode":"rolling","retention_seconds":600})"));
    const auto id = Text(start, L"capture_id");
    Require(Text(start, L"mode") == "rolling" && Text(start, L"state") == "active" &&
        start.GetNamedNumber(L"retention_seconds") == 600 && start.GetNamedNumber(L"deadline_ms") == 0 &&
        session.Label().find(L"持续") != std::wstring::npos, "rolling contract/label missing");
    Require(Rejects(session, "gbf_capture_start", R"({"mode":"rolling"})"), "parallel rolling capture allowed");
    session.Add(id, 0, "old", "request", R"({"timestamp":1e20,"wallTime":1e20,"observed_at_ms":1e20})");
    tick += 300000; session.Add(id, 0, "recent", "response", R"({"timestamp":-1e20,"wallTime":-1e20})");
    tick = 600999;
    Require(Object(session.Call("gbf_capture_status", Identity(id))).GetNamedNumber(L"record_count") == 2, "rolling record expired early");
    tick = 601000; session.Tick();
    auto page = Object(session.Call("gbf_capture_read", Arguments(id)));
    Require(session.Active() == id && page.GetNamedNumber(L"expired_records") == 1 && page.GetNamedNumber(L"dropped_records") == 0 &&
        page.GetNamedNumber(L"record_count") == 1 && page.GetNamedBoolean(L"cursor_gap") && !page.GetNamedBoolean(L"has_more") &&
        page.GetNamedNumber(L"next_cursor") == 2 && Text(page.GetNamedArray(L"records").GetObjectAt(0), L"request_id") == "recent",
        "rolling exact boundary/partial eviction failed");
    Require(!Object(session.Call("gbf_capture_read", Arguments(id, 1))).GetNamedBoolean(L"cursor_gap"), "caught-up cursor reported old loss");
    tick = 901000;
    page = Object(session.Call("gbf_capture_read", Arguments(id)));
    Require(page.GetNamedNumber(L"expired_records") == 2 && page.GetNamedNumber(L"buffer_bytes") == 0 &&
        page.GetNamedArray(L"records").Size() == 0 && page.GetNamedBoolean(L"cursor_gap") &&
        page.GetNamedNumber(L"next_cursor") == 2 && !page.GetNamedBoolean(L"has_more"), "empty rolling window stranded cursor");
    page = Object(session.Call("gbf_capture_read", Arguments(id, 2)));
    Require(!page.GetNamedBoolean(L"cursor_gap") && !page.GetNamedBoolean(L"has_more"), "empty current cursor did not settle");
    tick += 86400000; session.Tick(); session.Add(id, 4, "after-a-day", "request", "{}");
    page = Object(session.Call("gbf_capture_read", Arguments(id, 2)));
    Require(session.Active() == id && page.GetNamedArray(L"records").Size() == 1 &&
        page.GetNamedNumber(L"next_cursor") == 3 && !page.GetNamedBoolean(L"cursor_gap"), "rolling stopped or restarted at ten minutes");
    tick += 599999;
    const auto stopped = session.Call("gbf_capture_stop", Identity(id));
    tick += 86400000; session.Tick(); session.Add(id, 0, "late", "request", "{}"); session.Loss(id); session.Filtered(id);
    Require(session.Call("gbf_capture_status", Identity(id)) == stopped && session.Active().empty() &&
        session.Call("gbf_capture_stop", Identity(id)) == stopped, "stop failed to freeze rolling snapshot");
    const auto restarted = Object(session.Call("gbf_capture_start", R"({"mode":"rolling"})"));
    const auto next_id = Text(restarted, L"capture_id");
    Require(next_id != id && restarted.GetNamedNumber(L"retention_seconds") == 600 &&
        restarted.GetNamedNumber(L"expired_records") == 0 && restarted.GetNamedNumber(L"record_count") == 0 &&
        restarted.GetNamedNumber(L"next_cursor") == 0, "rolling restart failed to reset state");
    session.Add(id, 0, "stale-generation", "request", "{}"); session.Loss(id); session.Filtered(id);
    const auto current = Object(session.Call("gbf_capture_status", Identity(next_id)));
    Require(current.GetNamedNumber(L"record_count") == 0 && current.GetNamedNumber(L"dropped_records") == 0 &&
        current.GetNamedNumber(L"filtered_requests") == 0, "old generation modified rolling session");
    session.Add(next_id, 0, "expires-on-stop", "request", "{}"); tick += 600000;
    const auto empty_stop = Object(session.Call("gbf_capture_stop", Identity(next_id)));
    Require(empty_stop.GetNamedNumber(L"record_count") == 0 && empty_stop.GetNamedNumber(L"expired_records") == 1,
        "stop preserved already expired rolling records");
    const auto cleared = Object(session.Call("gbf_capture_clear", Identity(next_id)));
    for (const auto* field : {L"retention_seconds", L"expired_records", L"dropped_records", L"truncated_records", L"filtered_requests",
        L"record_count", L"buffer_bytes", L"started_at_ms", L"deadline_ms", L"next_cursor"})
      Require(cleared.GetNamedNumber(field) == 0, "rolling clear retained counters or identity");
    Require(Text(cleared, L"mode") == "bounded" && Text(cleared, L"state") == "cleared" && Text(cleared, L"capture_id").empty() &&
        Rejects(session, "gbf_capture_read", Identity(next_id)), "rolling clear retained session");
  }
  {
    uint64_t tick = 0;
    Session session(4096, 1024, [&] { return tick; });
    const auto id = Text(Object(session.Call("gbf_capture_start", R"({"mode":"rolling"})")), L"capture_id");
    JsonObject item; Put(item, L"padding", std::string(300, 'x'));
    for (unsigned index = 0; index < 40; ++index) session.Add(id, 0, "capacity", "request", Encode(item));
    JsonObject large; Put(large, L"padding", std::string(2000, 'x')); session.Add(id, 0, "truncated", "response", Encode(large));
    auto status = Object(session.Call("gbf_capture_status", Identity(id)));
    Require(status.GetNamedNumber(L"dropped_records") > 0 && status.GetNamedNumber(L"expired_records") == 0 &&
        status.GetNamedNumber(L"truncated_records") == 1 && status.GetNamedNumber(L"buffer_bytes") <= 4096, "rolling bypassed memory caps");
    const auto count = status.GetNamedNumber(L"record_count");
    const auto dropped = status.GetNamedNumber(L"dropped_records");
    // An oversized metadata-only record is dropped without evicting valid records.
    session.Add(id, 0, std::string(5000, 'x'), "request", "{}");
    status = Object(session.Call("gbf_capture_status", Identity(id)));
    Require(status.GetNamedNumber(L"record_count") == count && status.GetNamedNumber(L"dropped_records") == dropped + 1,
        "oversized record discarded unrelated data");
    const auto tail = Object(session.Call("gbf_capture_read", Arguments(id, 41)));
    Require(tail.GetNamedBoolean(L"cursor_gap") && !tail.GetNamedBoolean(L"has_more") && tail.GetNamedNumber(L"next_cursor") == 42,
        "discarded tail stranded cursor");
    tick = 600000; session.Tick();
    status = Object(session.Call("gbf_capture_status", Identity(id)));
    Require(status.GetNamedNumber(L"expired_records") == count && status.GetNamedNumber(L"dropped_records") == dropped + 1 &&
        status.GetNamedNumber(L"buffer_bytes") == 0 && session.Active() == id, "TTL and capacity loss were conflated");
    const auto empty = Object(session.Call("gbf_capture_read", Arguments(id)));
    Require(empty.GetNamedBoolean(L"cursor_gap") && !empty.GetNamedBoolean(L"has_more") && empty.GetNamedNumber(L"next_cursor") == 42,
        "combined TTL/capacity loss stranded cursor");
  }
  CheckObserverEvents();
  CheckCaptureTransport();
  std::cout << "PASS capture synthetic events: scope/association/redaction/bounded/rolling/strict-arguments/TTL/pagination/capacity/stop/clear/IPC; browser NOT_STARTED\n";
  winrt::uninit_apartment();
}
}
