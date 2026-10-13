(() => {
  'use strict';
  if (window !== window.top || location.protocol !== 'https:' ||
      !['game.granbluefantasy.jp', 'gbf.game.mbga.jp'].includes(location.hostname)) return;
  // MAIN world is required by the unmodified translation's Game/require/
  // jQuery hooks. No runtime or host objects are exposed to page scripts.
  if (typeof window.GM_xmlhttpRequest === 'function') return;
  const pending = new Map();
  const channel = 'gbrowser-translation-v1';
  const finish = (id, kind, result) => {
    const item = pending.get(id); if (!item) return;
    pending.delete(id); clearTimeout(item.timer);
    const callback = kind === 'load' ? item.options.onload :
      kind === 'abort' ? item.options.onabort : item.options.onerror;
    try { if (typeof callback === 'function') callback(result); }
    catch (_) { console.warn('StarryFish：汉化请求回调未完成。'); }
  };
  window.addEventListener('message', event => {
    const value = event.data;
    if (event.source !== window || event.origin !== location.origin || !value ||
        value.channel !== channel || value.kind !== 'response' || typeof value.id !== 'string') return;
    if (value.error) finish(value.id, 'error', {error: String(value.error).slice(0, 160)});
    else if (value.result && typeof value.result.responseText === 'string') finish(value.id, 'load', value.result);
  });
  window.GM_xmlhttpRequest = options => {
    if (!options || typeof options !== 'object' || pending.size >= 8)
      throw new Error('StarryFish translation request limit');
    const id = crypto.randomUUID();
    const request = {url: options.url, method: options.method || 'GET', headers: options.headers || {},
      data: options.data, responseType: options.responseType || 'text'};
    // Bound serialization before crossing worlds; worker independently validates
    // the actual sender, endpoints, methods, headers and body again.
    if (JSON.stringify(request).length > 600000) throw new Error('StarryFish translation request too large');
    const timeout = new URL(request.url).hostname === 'api.interpreter.caiyunai.com' ? 30000 : 180000;
    const item = {options, timer: setTimeout(() => {
      window.postMessage({channel, kind: 'cancel', id}, location.origin);
      finish(id, 'error', {error: 'Translation request timed out'});
    }, timeout)};
    pending.set(id, item);
    window.postMessage({channel, kind: 'request', id, request}, location.origin);
    return {abort() {
      window.postMessage({channel, kind: 'cancel', id}, location.origin);
      finish(id, 'abort', {error: 'Translation request aborted'});
    }};
  };
  window.addEventListener('pagehide', () => {
    for (const id of pending.keys()) {
      window.postMessage({channel, kind: 'cancel', id}, location.origin);
      clearTimeout(pending.get(id).timer);
    }
    pending.clear();
  });
})();
