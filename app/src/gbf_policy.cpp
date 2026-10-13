#include "gbf_policy.h"
#include "core.h"
#include "extension_page.h"
#include <algorithm>
#include <string_view>

namespace gbrowser {
namespace {
std::string_view HttpsHost(const std::string& url) {
  // No credentials, alternate ports, IP destinations or parser ambiguity.
  // CEF supplies canonical URLs; an origin supplied as initiator has no slash.
  if (url.size() > 8192 || url.compare(0, 8, "https://") != 0 ||
      !std::all_of(url.begin(), url.end(), [](unsigned char c) {
        return c >= 0x21 && c <= 0x7e && c != '\\';
      })) return {};
  const auto end = url.find_first_of("/?#", 8);
  auto host = std::string_view(url).substr(8, end == std::string::npos ? end : end - 8);
  if (host.size() > 4 && host.substr(host.size() - 4) == ":443") host.remove_suffix(4);
  if (host.empty() || !std::all_of(host.begin(), host.end(), [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    })) return {};
  return host;
}
bool Game(std::string_view host) {
  return host == "game.granbluefantasy.jp" || host == "gbf.game.mbga.jp";
}
bool Login(std::string_view host) {
  return host == "connect.mobage.jp" || host == "ssl.sp.mbga.jp";
}
bool Asset(std::string_view host) {
  // Observed in the anonymous official homepage, 2026-10-11. Do not grant
  // every Akamai/Mobage tenant or arbitrary HTTPS requests.
  return host == "prd-game-a-granbluefantasy.akamaized.net" ||
      host == "cdn-connect.mobage.jp" || host == "aimg-link.gree.net" ||
      host == "fonts.fontplus.dev";
}
std::string_view UrlPath(const std::string& url) {
  const auto start = url.find_first_of("/?#", 8);
  if (start == std::string::npos || url[start] != '/') return {};
  const auto end = url.find_first_of("?#", start);
  return std::string_view(url).substr(start, end == std::string::npos ? end : end - start);
}
bool NamespacePath(std::string_view path, std::string_view prefix) {
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix ||
      path.find("//") != path.npos || path.find("/./") != path.npos || path.find("/../") != path.npos ||
      path.substr(path.size() - 2) == "/." || path.substr(path.size() - 3) == "/..") return false;
  return std::all_of(path.begin(), path.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '/' || c == '.' || c == '_' || c == '-';
  });
}
bool CaptchaHost(std::string_view host) {
  return host == "www.google.com" || host == "recaptcha.google.com";
}
bool CaptchaFrame(const std::string& url) {
  if (!CaptchaHost(HttpsHost(url))) return false;
  const auto path = UrlPath(url);
  return path == "/recaptcha/api2/anchor" || path == "/recaptcha/api2/bframe" ||
      path == "/recaptcha/api2/aframe";
}
bool CaptchaScope(const GbfFrameScope& scope) {
  if (!scope.complete || !scope.count || scope.count > scope.urls.size() ||
      scope.subframe != (scope.count > 1) ||
      !IsGbfPageUrl(scope.urls[scope.count - 1])) return false;
  bool mobage = false;
  for (size_t i = 0; i < scope.count; ++i) {
    const auto& url = scope.urls[i];
    const auto host = HttpsHost(url);
    if (host == "connect.mobage.jp") mobage = true;
    else if (!Game(host) && !Login(host) && !CaptchaFrame(url) && url != "about:blank") return false;
  }
  return mobage;
}
bool CaptchaOrigin(const std::string& origin) {
  const auto host = HttpsHost(origin);
  const auto tail = origin.find_first_of("/?#", 8);
  return (host == "connect.mobage.jp" || CaptchaHost(host)) &&
      (tail == std::string::npos || origin.substr(tail) == "/");
}
bool CaptchaSource(const std::string& origin, bool navigation, const GbfFrameScope& scope) {
  if (!CaptchaOrigin(origin)) return false;
  const auto host = HttpsHost(origin);
  // A newly created about:blank child inherits its parent's origin. Other
  // resource initiators must match the current frame, not an unrelated sibling.
  size_t current = 0;
  while (current + 1 < scope.count && scope.urls[current] == "about:blank") ++current;
  return host == HttpsHost(scope.urls[current]) ||
      (navigation && scope.subframe && host == HttpsHost(scope.urls[1]));
}
bool BareHttpsOrigin(const std::string& origin) {
  return !HttpsHost(origin).empty() && origin.find_first_of("/?#", 8) == std::string::npos;
}
bool LoginSupportDocument(const std::string& url) {
  if (HttpsHost(url) != "app.mobage.jp" || UrlPath(url) != "/static/proxy2.3.html" ||
      url.find('?') != std::string::npos) return false;
  const auto hash = url.find('#');
  if (hash == std::string::npos) return true;  // Network requests may omit the fragment.
  const auto fragment = std::string_view(url).substr(hash + 1);
  constexpr std::string_view prefix = "parent_origin=";
  if (fragment.substr(0, prefix.size()) != prefix) return false;
  const std::string origin(fragment.substr(prefix.size()));
  return BareHttpsOrigin(origin) && (Game(HttpsHost(origin)) || Login(HttpsHost(origin)));
}
bool LoginSupportScript(const std::string& url) {
  return HttpsHost(url) == "cdn-connect.mobage.jp" &&
      UrlPath(url) == "/jssdk/mobage-jssdk-xdframe.2.3.min.js" &&
      url.find_first_of("?#") == std::string::npos;
}
bool LoginSupportScope(const GbfFrameScope& scope) {
  if (!scope.complete || !scope.subframe || scope.count < 2 || scope.count > scope.urls.size() ||
      !IsGbfPageUrl(scope.urls[scope.count - 1])) return false;
  for (size_t i = 0; i < scope.count; ++i) {
    const auto& url = scope.urls[i];
    // Only the new current child may still have an inherited/empty URL.
    // Every parent must already be a known first-party or the exact SDK document.
    if (i == 0 && (url.empty() || url == "about:blank")) continue;
    if (IsGbfPageUrl(url)) continue;
    if (!LoginSupportDocument(url) || i + 1 >= scope.count) return false;
    const auto hash = url.find('#');
    if (hash != std::string::npos &&
        HttpsHost(url.substr(hash + 1 + std::string_view("parent_origin=").size())) != HttpsHost(scope.urls[i + 1]))
      return false;
  }
  return true;
}
bool FormValue(std::string_view value) {
  // URLSearchParams in the original build/detail call encodes the key. Keep
  // separators out of the raw value without interpreting or logging user data.
  if (value.empty() || value.size() > 4096) return false;
  const auto hex = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
  };
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '%') {
      if (i + 2 >= value.size() || !hex(value[i + 1]) || !hex(value[i + 2])) return false;
      i += 2;
    } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_' || c == '+')) return false;
  }
  return true;
}
std::string DecodeFormValue(std::string_view value) {
  // Call only after FormValue: decode exactly once, never normalize a callback.
  std::string decoded;
  const auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%') {
      decoded.push_back(static_cast<char>(hex(value[i + 1]) * 16 + hex(value[i + 2])));
      i += 2;
    } else decoded.push_back(value[i] == '+' ? ' ' : value[i]);
  }
  return decoded;
}
template <typename Accept> bool LoginFormQuery(const std::string& url, unsigned limit, Accept&& accept) {
  if (url.find('#') != std::string::npos) return false;
  const auto query = url.find('?');
  if (query == std::string::npos) return false;
  auto args = std::string_view(url).substr(query + 1);
  if (args.empty()) return false;
  for (unsigned count = 0; !args.empty(); ++count) {
    if (count == limit) return false;
    const auto end = args.find('&');
    const auto pair = args.substr(0, end);
    const auto equal = pair.find('=');
    if (equal == pair.npos) return false;
    const auto key = pair.substr(0, equal), value = pair.substr(equal + 1);
    if (!FormValue(value) || !accept(key, value)) return false;
    if (end == args.npos) break;
    args.remove_prefix(end + 1);
    if (args.empty()) return false;
  }
  return true;
}
bool LoginBridgeQuery(const std::string& url) {
  bool state = false, callback = false;
  const bool valid = LoginFormQuery(url, 2, [&](std::string_view key, std::string_view value) {
    if (key == "state" && !state) state = true;  // Bounded opaque server value.
    else if (key == "redirect_uri" && !callback) {
      if (DecodeFormValue(value) != "https://connect.mobage.jp/federated/login/mobage/callback") return false;
      callback = true;
    } else return false;  // Unknown, duplicated or encoded keys are not accepted.
    return true;
  });
  return valid && state && callback;
}
bool LoginStartQuery(const std::string& url) {
  // Anonymous public GBF props, its lib/mobage-jssdk wrapper, and official
  // SDK 3.10.1 agree on these fields. The game adds grbl/1; absent optional
  // route/nickname fields are omitted by SDK encodeQuery, not guessed here.
  // State is opaque and may change on the server; never compare or log it.
  constexpr std::array<std::string_view, 13> keys = {"client_id", "response_type", "scope", "redirect_uri", "state",
      "prompt", "theme", "custom_theme", "appearance_version", "display", "client_origin_uri", "sdk_name", "sdk_version"};
  constexpr std::array<std::string_view, 13> values = {"12016007-2", "code token", "openid common_api",
      "https://game.granbluefantasy.jp/authentication", "", "consent", "default", "grbl", "1", "page",
      "https://game.granbluefantasy.jp", "mobage-jssdk", "3.10.1"};
  std::array<bool, keys.size()> seen{};
  const bool valid = LoginFormQuery(url, static_cast<unsigned>(keys.size()), [&](std::string_view key, std::string_view value) {
    const auto found = std::find(keys.begin(), keys.end(), key);
    if (found == keys.end()) return false;
    const auto index = static_cast<size_t>(found - keys.begin());
    if (seen[index] || (!values[index].empty() && DecodeFormValue(value) != values[index])) return false;
    seen[index] = true;
    return true;
  });
  return valid && std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}
bool TarouCloudApi(const std::string& url, const std::string& method) {
  // User requested the original Tarou cloud features. These are the exact
  // methods/paths in fixed 3.7.1 useUser; no host wildcard, browsing capability,
  // copied body/headers, CORS exception or manifest permission is introduced.
  if (HttpsHost(url) != "www.itsuki.icu" || url.find('#') != std::string::npos) return false;
  const auto path = UrlPath(url);
  const auto query = url.find('?');
  const bool preflight = method == "OPTIONS";
  if (path == "/api/ext/build/detail") {
    if ((!preflight && method != "GET") || query == std::string::npos) return false;
    const auto args = std::string_view(url).substr(query + 1);
    if (args.substr(0, 4) != "key=") return false;
    const auto split = args.find('&');
    if (!FormValue(args.substr(4, split == args.npos ? split : split - 4))) return false;
    if (split == args.npos) return true;
    const auto stamp = args.substr(split + 1);
    return stamp.size() > 2 && stamp.size() <= 22 && stamp.substr(0, 2) == "t=" &&
        std::all_of(stamp.begin() + 2, stamp.end(), [](char c) { return c >= '0' && c <= '9'; });
  }
  if (query != std::string::npos) return false;
  if (path == "/api/ext/quest") return preflight || method == "GET";
  constexpr std::array<std::string_view, 8> posts = {
      "/api/ext/code", "/api/ext/multiDrop", "/api/ext/startJson", "/api/ext/stat",
      "/api/ext/build/upsert", "/api/ext/build/list", "/api/ext/config/set", "/api/ext/config/get"};
  return (preflight || method == "POST") && std::find(posts.begin(), posts.end(), path) != posts.end();
}
bool TarouIconData(const std::string& url, const std::string& method) {
  // Fixed 3.7.1 uses Iconify's original fetch loader for these 29 literal icons.
  // Its two built-in fallback providers use the same JSON query. Keep the
  // payload/permissions intact; this grants no SVG, search, arbitrary icon,
  // script, upload or browsing endpoint. Chromium still checks CORS/CSP/TLS.
  const auto host = HttpsHost(url);
  if (method != "GET" || url.size() > 1024 || url.find('#') != url.npos ||
      (host != "api.iconify.design" && host != "api.simplesvg.com" && host != "api.unisvg.com")) return false;
  const auto path = UrlPath(url);
  constexpr std::string_view suffix = ".json";
  if (path.size() <= suffix.size() + 1 || path.substr(path.size() - suffix.size()) != suffix) return false;
  const auto prefix = path.substr(1, path.size() - suffix.size() - 1);
  const auto query = url.find('?');
  if (query == url.npos || url.compare(query + 1, 6, "icons=") != 0) return false;
  auto names = std::string_view(url).substr(query + 7);
  if (names.empty()) return false;
  constexpr std::array<std::string_view, 29> icons = {
      "carbon:document-download", "carbon:information-filled", "carbon:reset", "carbon:search",
      "carbon:settings", "carbon:switcher", "carbon:update-now",
      "game-icons:battle-axe", "game-icons:crossed-swords", "game-icons:custodian-helmet",
      "game-icons:dungeon-gate", "game-icons:glowing-artifact", "game-icons:gold-stack",
      "game-icons:medal", "game-icons:mimic-chest", "game-icons:scroll-unfurled",
      "material-symbols:arrow-back-ios", "material-symbols:check-box", "material-symbols:check-box-outline-blank",
      "material-symbols:compare", "material-symbols:content-copy", "material-symbols:dashboard",
      "material-symbols:document-search", "material-symbols:patient-list", "material-symbols:star-outline-rounded",
      "material-symbols:star-rounded", "mdi:information-outline", "ri:team-fill", "streamline-flex:screenshot"};
  std::array<bool, icons.size()> seen{};
  for (;;) {
    const auto split = names.find_first_of(",%");
    const auto name = names.substr(0, split);
    const auto found = std::find_if(icons.begin(), icons.end(), [&](std::string_view icon) {
      return icon.size() == prefix.size() + 1 + name.size() && icon.substr(0, prefix.size()) == prefix &&
          icon[prefix.size()] == ':' && icon.substr(prefix.size() + 1) == name;
    });
    if (found == icons.end()) return false;
    const auto index = static_cast<size_t>(found - icons.begin());
    if (seen[index]) return false;
    seen[index] = true;
    if (split == names.npos) return true;
    // URLSearchParams in the original loader encodes batch commas as %2C.
    // Accept literal commas too, but no other escapes or query parameters.
    const size_t separator = names[split] == ',' ? 1 : names.substr(split, 3) == "%2C" ? 3 : 0;
    if (!separator || split + separator == names.size()) return false;
    names.remove_prefix(split + separator);
  }
}
bool DiagnosticWebRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, const GbfFrameScope& scope) {
  if (!IsBrowserNetworkUrl(url) || method.empty() || method.size() > 32 ||
      !std::all_of(method.begin(), method.end(), [](char c) { return c >= 'A' && c <= 'Z'; }) ||
      initiator.compare(0, 19, "chrome-extension://") == 0) return false;
  // Workers without a CefFrame need an ordinary web initiator. An opaque
  // request gains no capability without a live complete browser-frame chain.
  if (!scope.count) return IsBrowserNetworkUrl(initiator);
  if (!scope.complete || scope.count > scope.urls.size() || scope.subframe != (scope.count > 1)) return false;
  for (size_t i = 0; i < scope.count; ++i) {
    const auto& current = scope.urls[i];
    if (IsBrowserNetworkUrl(current)) continue;
    // A fresh normal page/child may not yet have its target URL. This cannot
    // make a Chrome/extension/probe parent or an unbound resource ordinary.
    if (i == 0 && navigation && method == "GET" &&
        (current.empty() || current == "about:blank" || current == GbfPreparationUrl() || current == DevelopmentHomeUrl())) continue;
    return false;
  }
  return true;
}
}
const char* RunModeSwitch(RunMode mode) {
  switch (mode) {
    case RunMode::Offline: return "validation-offline";
    case RunMode::Gbf: return "validation-gbf";
    case RunMode::GbfOpenNetwork: return "validation-gbf-open-network";
  }
  throw std::runtime_error("Unknown browser run mode");
}
bool IsGbfMode(RunMode mode) { return mode == RunMode::Gbf || mode == RunMode::GbfOpenNetwork; }
bool IsBrowserNetworkUrl(const std::string& url) {
  const size_t start = url.compare(0, 8, "https://") == 0 ? 8 : url.compare(0, 7, "http://") == 0 ? 7 : 0;
  if (!start || url.size() > 8192 || !std::all_of(url.begin(), url.end(), [](unsigned char c) {
        return c >= 0x21 && c <= 0x7e && c != '\\';
      })) return false;
  const auto end = url.find_first_of("/?#", start);
  const auto authority = std::string_view(url).substr(start, end == std::string::npos ? end : end - start);
  auto host = std::string(authority.substr(0, authority.find(':')));
  std::transform(host.begin(), host.end(), host.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
  });
  if (host == "gbrowser.invalid") return false;  // Synthetic storage stays isolated.
  // CEF canonicalizes actual requests. Keep malformed/credential-bearing
  // address drafts out of the shell; do not interpret or log account values.
  return !authority.empty() && authority.front() != ':' &&
      std::all_of(authority.begin(), authority.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '-' || c == ':' || c == '[' || c == ']';
      });
}
bool IsNetworkPageUrl(const std::string& url, RunMode mode) {
  return IsGbfMode(mode) && (IsGbfPageUrl(url) || (mode == RunMode::GbfOpenNetwork && IsBrowserNetworkUrl(url)));
}
const std::string& GbfHomeUrl() {
  static const std::string url = "https://game.granbluefantasy.jp/#mypage";
  return url;
}
const std::string& GbfPreparationUrl() {
  static const std::string url = "data:text/html,%3Cmeta%20charset%3Dutf-8%3E%3Ctitle%3EGBF%20%E5%87%86%E5%A4%87%3C%2Ftitle%3E%3Cstyle%3Ebody%7Bfont%3A18px%20Segoe%20UI%3Bbackground%3A%23f2f4f5%3Bcolor%3A%232a353e%3Bmargin%3A56px%3Bline-height%3A1.6%7D%3C%2Fstyle%3E%3Ch1%3EGbrowser%3C%2Fh1%3E%3Cp%3E%E5%87%86%E5%A4%87%E8%BF%9B%E5%85%A5%20GBF%3C%2Fp%3E%3Cp%3E%E8%AF%B7%E5%85%88%E5%90%AF%E5%8A%A8%20acgpower%EF%BC%8C%E7%84%B6%E5%90%8E%E5%9C%A8%E5%8F%B3%E4%BE%A7%E2%80%9C%E4%BB%A3%E7%90%86%E2%80%9D%E9%80%89%E6%8B%A9%20acgpower%E3%80%82%E7%A1%AE%E8%AE%A4%E5%8C%B9%E9%85%8D%E5%90%8E%E5%B0%86%E8%BF%9B%E5%85%A5%20GBF%E3%80%82%3C%2Fp%3E%3Cp%3E%E8%8B%A5%E5%B7%B2%E5%88%87%E6%8D%A2%E5%88%B0%E5%85%B6%E4%BB%96%E6%A0%87%E7%AD%BE%EF%BC%8C%E5%8F%AF%E4%BB%8E%E4%B8%89%E7%82%B9%E8%8F%9C%E5%8D%95%E6%89%93%E5%BC%80%20GBF%20%E4%B8%BB%E9%A1%B5%E3%80%82%E7%99%BB%E5%BD%95%E4%B8%8E%E6%B8%B8%E6%88%8F%E6%93%8D%E4%BD%9C%E7%94%B1%E4%BD%A0%E5%AE%8C%E6%88%90%E3%80%82%3C%2Fp%3E%3Cp%3E%E9%81%87%E5%88%B0%E8%AF%81%E4%B9%A6%E6%88%96%E6%B2%99%E7%AE%B1%E9%94%99%E8%AF%AF%EF%BC%8C%E8%AF%B7%E5%81%9C%E6%AD%A2%E5%B9%B6%E6%8A%A5%E5%91%8A%E3%80%82%3C%2Fp%3E";
  return url;
}
bool IsGbfPageUrl(const std::string& url) { return Game(HttpsHost(url)) || Login(HttpsHost(url)); }
bool IsGbfLoginUrl(const std::string& url) { return Login(HttpsHost(url)); }
bool IsGbfLoginPopup(RunMode mode, const std::string& source, const std::string& target, bool gesture) {
  return IsGbfMode(mode) && gesture && IsGbfPageUrl(source) && IsGbfLoginUrl(target);
}
bool IsGbfIdentityRequest(const std::string& current_url, const std::string& requesting_origin) {
  if (!IsGbfPageUrl(current_url) || !IsGbfPageUrl(requesting_origin)) return false;
  const auto tail = requesting_origin.find_first_of("/?#", 8);
  return tail == std::string::npos || requesting_origin.substr(tail) == "/";
}
bool IsGbfResourceUrl(const std::string& url, const std::string& method,
                      const std::string& initiator, bool navigation, bool download) {
  if (download || (method != "GET" && method != "POST" && method != "HEAD" && method != "OPTIONS")) return false;
  if (navigation) return IsGbfPageUrl(url);
  const auto source = HttpsHost(initiator), destination = HttpsHost(url);
  // API POST and preflight OPTIONS remain subject to Chromium CORS/CSP. No
  // opaque/local/extension initiator obtains network access through this rule.
  return (Game(source) || Login(source) || Asset(source)) &&
      (Game(destination) || Login(destination) || Asset(destination));
}
bool IsGbfCaptchaUrl(const std::string& url) {
  const auto host = HttpsHost(url), path = UrlPath(url);
  // Anonymous Mobage /login references api.js. Google's documented CSP lists
  // these namespaces; no general Google navigation, login or tenant wildcard.
  if (host == "www.google.com" && path == "/recaptcha/api.js") return true;
  if (CaptchaHost(host)) return NamespacePath(path, "/recaptcha/api2/");
  return host == "www.gstatic.com" && NamespacePath(path, "/recaptcha/releases/");
}
bool IsGbfCaptchaFrameNavigation(const std::string& url, const GbfFrameScope& scope) {
  return scope.subframe && CaptchaScope(scope) && CaptchaFrame(url);
}
bool IsGbfCaptchaRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download, const GbfFrameScope& scope) {
  if (download || !IsGbfCaptchaUrl(url) || !CaptchaScope(scope) || !CaptchaSource(initiator, navigation, scope)) return false;
  if (navigation) return method == "GET" && IsGbfCaptchaFrameNavigation(url, scope);
  if (HttpsHost(url) == "www.gstatic.com" || UrlPath(url) == "/recaptcha/api.js")
    return method == "GET" || method == "HEAD";
  return method == "GET" || method == "HEAD" || method == "POST" || method == "OPTIONS";
}
bool IsGbfLoginSupportUrl(const std::string& url) {
  return LoginSupportDocument(url) || LoginSupportScript(url);
}
bool IsGbfLoginBridgeStart(const std::string& source, const std::string& initial) {
  return HttpsHost(source) == "game.granbluefantasy.jp" && HttpsHost(initial) == "connect.mobage.jp" &&
      UrlPath(initial) == "/connect/1.0/services/authorize" && LoginStartQuery(initial);
}
bool IsGbfLoginBridgeUrl(const std::string& url) {
  // Official anonymous SDK chain observed 2026-10-12; this GET only checks
  // Mobage cookies before returning to the fixed federated callback. It grants
  // no browsing, POST, script, identity or captcha authority on www.mbga.jp.
  return HttpsHost(url) == "www.mbga.jp" && UrlPath(url) == "/_connect_immediate_login" && LoginBridgeQuery(url);
}
bool IsGbfLoginSupportFrameNavigation(const std::string& url, const GbfFrameScope& scope) {
  if (!LoginSupportDocument(url) || !LoginSupportScope(scope) || !IsGbfPageUrl(scope.urls[1])) return false;
  const auto hash = url.find('#');
  if (hash == std::string::npos) return true;
  const auto parent = url.substr(hash + 1 + std::string_view("parent_origin=").size());
  return HttpsHost(parent) == HttpsHost(scope.urls[1]);
}
bool IsGbfLoginSupportRequest(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download, const GbfFrameScope& scope) {
  if (download || !LoginSupportScope(scope)) return false;
  if (navigation) {
    if (method != "GET" || !IsGbfLoginSupportFrameNavigation(url, scope)) return false;
    // Locked CEF serializes a missing navigation initiator as "null". Only
    // this exact first load can use its live, complete parent chain instead;
    // a loaded frame, worker or ordinary resource obtains no opaque capability.
    if (initiator == "null") return scope.urls[0].empty() || scope.urls[0] == "about:blank";
    return BareHttpsOrigin(initiator) && HttpsHost(initiator) == HttpsHost(scope.urls[1]);
  }
  return BareHttpsOrigin(initiator) && (method == "GET" || method == "HEAD") && LoginSupportScript(url) &&
      LoginSupportDocument(scope.urls[0]) && HttpsHost(initiator) == "app.mobage.jp";
}
std::string GbfDiagnosticHost(const std::string& host) {
  // Retain public DNS facts needed to identify a rejected official redirect.
  // Never print IPs, local names, credentials or unbounded/unparsed strings.
  if (host.empty()) return "MISSING";
  if (host.size() > 253 || host.front() == '.' || host.back() == '.' || host.find('.') == std::string::npos ||
      !std::all_of(host.begin(), host.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
      }) || !std::any_of(host.begin(), host.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
    return "REDACTED";
  for (const auto* suffix : {".local", ".localhost", ".internal", ".lan", ".test", ".invalid"}) {
    const std::string_view tail(suffix);
    if (host.size() >= tail.size() && std::string_view(host).substr(host.size() - tail.size()) == tail)
      return "REDACTED";
  }
  return host;
}
const char* GbfDiagnosticRoute(const std::string& path) {
  // Fixed labels prevent arbitrary path segments from exposing auth material.
  // These categories do not assert that the route or its host is approved.
  if (path == "/") return "ROOT";
  if (path == "/connect/1.0/services/authorize") return "SDK_AUTHORIZE";
  if (path == "/federated/login/mobage") return "MOBAGE_FEDERATED";
  if (path == "/_connect_immediate_login") return "MOBAGE_IMMEDIATE_LOGIN";
  if (path == "/federated/login/mobage/callback") return "MOBAGE_CALLBACK";
  if (path == "/login") return "LOGIN";
  if (path == "/login/") return "LOGIN_SLASH";
  if (path == "/authorize") return "AUTHORIZE";
  if (path == "/_login") return "MOBAGE_LOGIN";
  if (path == "/_oauth_authorize") return "MOBAGE_AUTHORIZE";
  if (path == "/_oauth_prompt") return "MOBAGE_PROMPT";
  if (path == "/connect/1.0/error") return "SDK_ERROR";
  if (path == "/error") return "ERROR";
  return "OTHER";
}
namespace {
enum class HelperRole { None, Tarou, Arcarum };
HelperRole FixedHelperRole(const ExtensionPage& page) {
  const auto& url = page.url();
  const auto offset = url.find('/', 19);
  if (page.target() || offset == std::string::npos) return HelperRole::None;
  const auto entry = std::string_view(url).substr(offset);
  if (entry == "/src/views/sidePanel/main.html") return HelperRole::Tarou;
  if (entry == "/src/panel/index.html") return HelperRole::Arcarum;
  return HelperRole::None;
}
}
ContextResourcePolicy::ContextResourcePolicy(RunMode mode) : mode_(mode) {}
void ContextResourcePolicy::Publish(std::vector<std::shared_ptr<const ExtensionPage>> checked) {
  std::lock_guard<std::mutex> held(mutex_);
  components_ = std::move(checked); published_ = true; ready_ = true; acgpower_confirmed_ = false;
  login_popups_.clear();
}
bool ContextResourcePolicy::IncludeCheckedHelper(std::shared_ptr<const ExtensionPage> checked,
    const std::string& profile, const std::filesystem::path& cache) {
  if (!checked || !checked->MatchesProfile(profile, cache)) return false;
  const auto role = FixedHelperRole(*checked);
  if (role == HelperRole::None) return false;
  std::lock_guard<std::mutex> held(mutex_);
  if (!IsGbfMode(mode_) || !published_ || !ready_ || !acgpower_confirmed_) return false;
  for (const auto& existing : components_) {
    if (!existing->MatchesProfile(profile, cache)) return false;
    if (existing->url() == checked->url()) return true;
  }
  // Assemble before committing: allocation failure keeps the previous policy.
  // The UI rechecked unique enabled source selection; retaining an old role's
  // origin here would let a stale source outlive that explicit user choice.
  auto next = components_;
  next.erase(std::remove_if(next.begin(), next.end(), [&](const auto& page) {
    return FixedHelperRole(*page) == role;
  }), next.end());
  if (next.size() >= 8) return false;
  next.push_back(std::move(checked));
  components_.swap(next);
  return true;
}
void ContextResourcePolicy::Suspend() {
  std::lock_guard<std::mutex> held(mutex_); ready_ = false; login_popups_.clear();
}
void ContextResourcePolicy::Resume() {
  std::lock_guard<std::mutex> held(mutex_); ready_ = published_;
}
void ContextResourcePolicy::AcgpowerConfirmed(bool confirmed) {
  std::lock_guard<std::mutex> held(mutex_); acgpower_confirmed_ = confirmed && ready_ && published_;
  if (!acgpower_confirmed_) login_popups_.clear();
}
bool ContextResourcePolicy::NetworkReady() const {
  std::lock_guard<std::mutex> held(mutex_);
  return IsGbfMode(mode_) && ready_ && acgpower_confirmed_;
}
bool ContextResourcePolicy::RegisterLoginPopup(int child, int opener, const std::string& source, const std::string& initial) {
  std::lock_guard<std::mutex> held(mutex_);
  if (!IsGbfMode(mode_) || !ready_ || !acgpower_confirmed_ || child <= 0 || opener <= 0 || child == opener ||
      !IsGbfLoginBridgeStart(source, initial) || login_popups_.size() >= 64) return false;
  return login_popups_.emplace(child, LoginPopup{opener, initial, {}, false,
      std::chrono::steady_clock::now() + std::chrono::seconds(60)}).second;
}
bool ContextResourcePolicy::BeginLoginBridge(int child, const std::string& source, const std::string& target,
    const std::string& method, int status, bool navigation, bool main, bool download) {
  std::lock_guard<std::mutex> held(mutex_);
  const auto found = login_popups_.find(child);
  if (!IsGbfMode(mode_) || !ready_ || !acgpower_confirmed_ || found == login_popups_.end() ||
      found->second.used || std::chrono::steady_clock::now() >= found->second.deadline ||
      method != "GET" || status != 302 || !navigation || !main || download ||
      HttpsHost(source) != "connect.mobage.jp" || UrlPath(source) != "/federated/login/mobage" ||
      source.find('#') != std::string::npos || !IsGbfLoginBridgeUrl(target)) return false;
  found->second.target = target;  // Copy first: allocation failure cannot grant authority.
  found->second.used = true;
  return true;
}
bool ContextResourcePolicy::AllowsLoginBridge(int child, const std::string& target, const std::string& method,
    bool navigation, bool main, bool download) const {
  std::lock_guard<std::mutex> held(mutex_);
  const auto found = login_popups_.find(child);
  return IsGbfMode(mode_) && ready_ && acgpower_confirmed_ && found != login_popups_.end() &&
      found->second.used && !found->second.target.empty() && found->second.target == target &&
      std::chrono::steady_clock::now() < found->second.deadline && method == "GET" && navigation && main && !download;
}
void ContextResourcePolicy::CompleteLoginBridge(int child, const std::string& source) {
  std::lock_guard<std::mutex> held(mutex_);
  const auto found = login_popups_.find(child);
  if (found != login_popups_.end() && found->second.used && found->second.target == source) login_popups_.erase(found);
}
void ContextResourcePolicy::LoginNavigation(int browser, const std::string& url, bool redirect) {
  std::lock_guard<std::mutex> held(mutex_);
  for (auto it = login_popups_.begin(); it != login_popups_.end();) {
    if (it->second.opener == browser || (it->first == browser && !redirect && it->second.initial != url))
      it = login_popups_.erase(it);
    else ++it;
  }
}
void ContextResourcePolicy::ReleaseLoginBrowser(int browser) {
  std::lock_guard<std::mutex> held(mutex_);
  for (auto it = login_popups_.begin(); it != login_popups_.end();) {
    if (it->first == browser || it->second.opener == browser) it = login_popups_.erase(it);
    else ++it;
  }
}
bool ContextResourcePolicy::Allows(const std::string& url, const std::string& method,
    const std::string& initiator, bool navigation, bool download, const GbfFrameScope& scope) const {
  if (IsOfflineResourceUrl(url, initiator, navigation, download)) return true;
  if (!IsGbfMode(mode_) || download) return false;
  if (url == GbfPreparationUrl()) return navigation && method == "GET";
  std::lock_guard<std::mutex> held(mutex_);
  if (!ready_) return false;
  // User-requested, explicit opt-in diagnostic capability. The normal fixed
  // mode still uses the narrow rules below. Never grant download/protocol,
  // extension-document or synthetic-probe access through this web branch.
  if (OpenNetworkDiagnostic() && acgpower_confirmed_ &&
      DiagnosticWebRequest(url, method, initiator, navigation, scope)) return true;
  // The fixed SDK resources own a narrower frame capability than the general
  // asset rule. A rejected SDK request must not fall back to that broader rule.
  if (IsGbfLoginSupportUrl(url)) return acgpower_confirmed_ &&
      IsGbfLoginSupportRequest(url, method, initiator, navigation, download, scope);
  if (acgpower_confirmed_ && IsGbfResourceUrl(url, method, initiator, navigation, download)) return true;
  if (acgpower_confirmed_ && IsGbfCaptchaRequest(url, method, initiator, navigation, download, scope)) return true;
  if (navigation) return false;  // Controller pages need their own exact capability.
  for (const auto& component : components_) {
    const auto origin = component->url().substr(0, component->url().find('/', 19));
    const auto role = FixedHelperRole(*component);
    const bool tarou = role == HelperRole::Tarou;
    const bool arcarum = role == HelperRole::Arcarum;
    // The original manifests expose precisely these two injection scripts to
    // game pages. Other extension assets stay private to their own origin;
    // Chromium still enforces registration, host permissions, WAR and CSP.
    const auto injection = tarou ? origin + "/inject.js" : origin + "/src/content/inject.js";
    // Both original content scripts append their own exact extensionId. Do
    // not accept arbitrary query parameters or another component's identity.
    const bool game_injection = (tarou || arcarum) && Game(HttpsHost(initiator)) &&
        (url == injection || url == injection + "?extensionId=" + origin.substr(19));
    if (component->AllowsAsset(url) && method == "GET" && (initiator == origin || game_injection)) return true;
    // Neither game helper needs to write login endpoints. ZeroOmega has no
    // game network capability through the application request policy.
    const auto destination = HttpsHost(url);
    if (acgpower_confirmed_ && (tarou || arcarum) && initiator == origin &&
        (method == "GET" || method == "HEAD") &&
        (Game(destination) || destination == "prd-game-a-granbluefantasy.akamaized.net")) return true;
    if (acgpower_confirmed_ && tarou && initiator == origin && TarouCloudApi(url, method)) return true;
    if (acgpower_confirmed_ && tarou && initiator == origin && TarouIconData(url, method)) return true;
    // Only the pinned original ZeroOmega options origin may fetch this PAC.
    // This is not general loopback access and does not parse/execute PAC here.
    if (initiator == origin && component->url() == origin + "/options.html" &&
        method == "GET" && url == "http://127.0.0.1:8123/proxy.pac") return true;
  }
  return false;
}
}
