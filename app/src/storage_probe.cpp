#include "storage_probe.h"
#include "storage_probe_assets.h"
#include <stdexcept>

namespace gbrowser {
bool IsStorageProbeRequest(const std::string& url, const std::string& method,
                           const std::string& initiator, bool navigation, bool download) {
  if (method != "GET" || download) return false;
  return (url == kStorageProbePage && navigation) ||
         (url == kStorageProbeScript && !navigation && initiator == kStorageProbeOrigin);
}
const char* StorageProbeCsp() {
  return "default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; "
         "connect-src 'none'; worker-src 'none'; child-src 'none'; "
         "frame-ancestors 'none'; form-action 'none'; base-uri 'none'; object-src 'none'";
}
StorageProbeResource MakeStorageProbeResource(const std::string& url,
                                             const std::string& profile) {
  if (!IsProfileId(profile)) throw std::runtime_error("Invalid storage probe profile");
  if (url == kStorageProbeScript) return {"application/javascript", kStorageProbeJavaScript};
  if (url != kStorageProbePage) throw std::runtime_error("Unknown storage probe resource");
  // The only variable is an already validated, opaque ASCII profile ID.
  return {"text/html", std::string(R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>本地存储检查</title><style>
body{font:16px "Segoe UI",sans-serif;margin:32px;max-width:720px;background:#f3f4f5;color:#20272c}
h1{font-size:26px}code{overflow-wrap:anywhere}button{font:inherit;padding:9px 15px;margin:8px 12px 8px 0}
pre{white-space:pre-wrap;padding:16px;background:white;border:1px solid #ccd2d6;min-height:70px}
</style><h1>本地存储检查</h1><p>仅用于离线合成标记，不代表真实登录态或扩展数据已验收。</p>
<p>当前资料 ID：<code id="profile" data-profile=")HTML") + profile + R"HTML(">)HTML" + profile + R"HTML(</code></p>
<p>读取仅检查本页保留的 localStorage 键和 IndexedDB 数据库，不创建数据库。
写入仅在两处均未观察到标记时创建；已有匹配标记保持原样。
发现外来、损坏或不完整数据会停止，不覆盖、不删除。</p>
<button id="read" disabled>读取标记</button><button id="write" disabled>写入合成标记</button>
<pre id="status" role="status">脚本尚未就绪；若此状态不变，请停止测试。</pre>
<p>两种存储无法一起原子写入。失败可能留下部分标记，请保留现场，不反复写入。
关闭或切换资料时仍在进行的事务可能已提交，须重新读取核对。</p>
<h2>通知权限拒绝检查</h2>
<p>仅显式请求本保留域名的通知权限，不创建通知、不读写存储标记。
可能记录该域名的拒绝状态；已有状态不重置。若出现允许提示、已授权或超时，请停止。</p>
<button id="permission-check" disabled>检查通知权限拒绝（一次）</button>
<pre id="permission-status" role="status">尚未请求通知权限。</pre>
<h2>取消关闭检查</h2>
<p>仅在本页内存中启用关闭确认，不读写存储标记。点击启用后关闭本页并选择“否”，
应保留此标签；测试后先解除再正常关闭。若取消后页面或资料意外改变，请停止。</p>
<button id="unload-arm" disabled>启用本页关闭确认（一次）</button>
<button id="unload-disarm" disabled>解除本页关闭确认</button>
<pre id="unload-status" role="status">尚未启用关闭确认。</pre>
<script src="/storage-probe/probe.js" defer></script></html>)HTML"};
}
}  // namespace gbrowser
