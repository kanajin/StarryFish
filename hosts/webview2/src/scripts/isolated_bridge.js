(() => {
  'use strict';
  if (window !== window.top || location.protocol !== 'https:' ||
      !['game.granbluefantasy.jp', 'gbf.game.mbga.jp'].includes(location.hostname)) return;
  const channel = 'gbrowser-translation-v1', ports = new Map();
  // Same-page code can send messages here; this authenticates the GBF frame,
  // not an individual script. The worker therefore accepts only the original
  // translation's bounded POST shapes/endpoints, never arbitrary page URLs.
  const reply = (id, value) => window.postMessage({channel, kind: 'response', id, ...value}, location.origin);
  window.addEventListener('message', event => {
    const value = event.data;
    if (event.source !== window || event.origin !== location.origin || !value || value.channel !== channel ||
        typeof value.id !== 'string' || !/^[0-9a-f-]{36}$/.test(value.id)) return;
    if (value.kind === 'cancel') {
      const port = ports.get(value.id); if (port) { ports.delete(value.id); port.disconnect(); }
      return;
    }
    if (value.kind !== 'request' || ports.has(value.id)) return;
    try {
      if (!value.request || typeof value.request !== 'object' || ports.size >= 8 ||
          JSON.stringify(value.request).length > 600000) {
        reply(value.id, {error: 'Translation request budget exceeded'}); return;
      }
      const port = chrome.runtime.connect({name: channel}); ports.set(value.id, port);
      port.onMessage.addListener(result => {
        if (ports.get(value.id) !== port) return;
        ports.delete(value.id); reply(value.id, result); port.disconnect();
      });
      port.onDisconnect.addListener(() => {
        // Consume runtime.lastError without copying sensitive upstream strings.
        void chrome.runtime.lastError;
        if (ports.get(value.id) !== port) return;
        ports.delete(value.id); reply(value.id, {error: 'Translation service disconnected'});
      });
      port.postMessage(value.request);
    } catch (_) { ports.delete(value.id); reply(value.id, {error: 'Translation service unavailable'}); }
  });
  window.addEventListener('pagehide', () => {
    const active = [...ports.values()]; ports.clear(); for (const port of active) port.disconnect();
  });
})();
