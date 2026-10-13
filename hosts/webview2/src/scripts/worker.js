'use strict';
importScripts('request_policy.js');
let active = 0, caiyunRules;
function prepareCaiyun() {
  if (!caiyunRules) caiyunRules = chrome.declarativeNetRequest.updateSessionRules({removeRuleIds: [1001], addRules: [{
    id: 1001, priority: 1,
    action: {type: 'modifyHeaders', requestHeaders: [
      {header: 'origin', operation: 'set', value: 'https://fanyi.caiyunapp.com'},
      {header: 'referer', operation: 'set', value: 'https://fanyi.caiyunapp.com/'}]},
    condition: {regexFilter: '^https://api\\.interpreter\\.caiyunai\\.com/v1/(user/jwt/generate|translator)(\\?.*)?$',
      requestDomains: ['api.interpreter.caiyunai.com'], initiatorDomains: [chrome.runtime.id],
      resourceTypes: ['xmlhttprequest'], requestMethods: ['post']}
  }]}).catch(error => { caiyunRules = null; throw error; });
  return caiyunRules;
}
async function readText(response, signal) {
  if (!response.body) return '';
  const reader = response.body.getReader(), decoder = new TextDecoder('utf-8', {fatal: true});
  let text = '', bytes = 0;
  try {
    for (;;) {
      const item = await reader.read(); if (signal.aborted) throw new Error('Aborted');
      if (item.done) break;
      bytes += item.value.byteLength;
      if (bytes > 2 * 1024 * 1024) throw new Error('Response too large');
      text += decoder.decode(item.value, {stream: true});
    }
    return text + decoder.decode();
  } finally { await reader.cancel().catch(() => {}); }
}
chrome.runtime.onConnect.addListener(port => {
  try {
    if (port.name !== 'gbrowser-translation-v1' || active >= 8) throw new Error('Request rejected');
    GbrowserScriptPolicy.sender(port.sender, chrome.runtime.id);
  } catch (_) { port.disconnect(); return; }
  ++active;
  const controller = new AbortController(); let finished = false, started = false;
  let timer = setTimeout(expire, 30000);
  function expire() { controller.abort(); close({error: 'Translation request timed out'}); }
  function close(message) {
    if (finished) return;
    finished = true; --active; clearTimeout(timer);
    if (message) { try { port.postMessage(message); } catch (_) {} }
    port.disconnect();
  }
  port.onDisconnect.addListener(() => { void chrome.runtime.lastError; controller.abort(); close(); });
  port.onMessage.addListener(async value => {
    if (started || finished) { controller.abort(); close({error: 'Duplicate request rejected'}); return; }
    started = true;
    try {
      GbrowserScriptPolicy.sender(port.sender, chrome.runtime.id);
      const query = GbrowserScriptPolicy.request(value);
      clearTimeout(timer);
      timer = setTimeout(expire, query.caiyun ? 30000 : 180000);
      if (query.caiyun) await prepareCaiyun();
      if (finished) return;
      // Runs in the browser extension context with explicit host permissions;
      // normal TLS and the selected browser proxy remain in effect. Never
      // attach browser cookies, follow redirects or run a downloaded script.
      const response = await fetch(query.url, {method: query.method, headers: query.headers, body: query.body,
        credentials: 'omit', redirect: 'error', referrerPolicy: 'no-referrer', signal: controller.signal});
      const text = await readText(response, controller.signal);
      close({result: {status: response.status, statusText: response.statusText, responseText: text}});
    } catch (_) { controller.abort(); close({error: 'Translation request failed or was not allowed'}); }
  });
});
