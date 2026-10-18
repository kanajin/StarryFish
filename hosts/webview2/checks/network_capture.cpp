#include "network_capture.h"
#include "capture_json.h"
#include <iostream>
#include <stdexcept>

namespace gbrowser::webview::capture {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
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
    Session session(8192, 2048);
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
    Require(Object(session.Call("gbf_capture_status", Arguments(id))).GetNamedNumber(L"record_count") == count, "stopped capture still recorded");
    for (const char* duration : {"0", "601", "1.5"}) {
      rejected = false; try { session.Call("gbf_capture_start", std::string("{\"duration_seconds\":") + duration + "}"); } catch (...) { rejected = true; }
      Require(rejected, "invalid capture duration accepted");
    }
    const auto next = Text(Object(session.Call("gbf_capture_start", "{\"duration_seconds\":600}")), L"capture_id");
    for (int index = 0; index < 50; ++index) { JsonObject item; Put(item, L"padding", std::string(500, 'x')); session.Add(next, 4, "bulk", "request", Encode(item)); }
    auto status = Object(session.Call("gbf_capture_status", Arguments(next)));
    Require(status.GetNamedNumber(L"dropped_records") > 0 && status.GetNamedNumber(L"buffer_bytes") <= 8192, "capture memory limit failed");
    Require(Object(session.Call("gbf_capture_read", Arguments(next))).GetNamedBoolean(L"cursor_gap"), "capture loss was silent");
    JsonObject large; Put(large, L"padding", std::string(4000, 'x')); session.Add(next, 4, "large", "response", Encode(large));
    Require(Object(session.Call("gbf_capture_status", Arguments(next))).GetNamedNumber(L"truncated_records") == 1, "record limit not reported");
    session.Call("gbf_capture_clear", Arguments(next)); Require(session.Active().empty(), "capture clear remained active");
    rejected = false; try { session.Call("gbf_capture_read", Arguments(next)); } catch (...) { rejected = true; }
    Require(rejected, "cleared records readable");
    const auto timed = Text(Object(session.Call("gbf_capture_start", "{\"duration_seconds\":1}")), L"capture_id");
    Sleep(1100); Require(session.Active().empty(), "capture did not expire");
    Require(Text(Object(session.Call("gbf_capture_status", Arguments(timed))), L"state") == "expired", "expiry state missing");
  }
  CheckObserverEvents();
  CheckCaptureTransport();
  std::cout << "PASS capture synthetic events: scope/association/redaction/pagination/time/capacity/clear; browser NOT_STARTED\n";
  winrt::uninit_apartment();
}
}
