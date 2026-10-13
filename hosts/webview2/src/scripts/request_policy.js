'use strict';
// The fixed script uses only JSON POST for GM requests. This deliberately is not a
// general GM implementation, file reader, cookie proxy or arbitrary endpoint.
globalThis.GbrowserScriptPolicy = (() => {
  const paths = new Map([
    ['api.interpreter.caiyunai.com', ['/v1/user/jwt/generate', '/v1/translator']],
    ['api.openai.com', ['/v1/chat/completions']],
    ['api.deepseek.com', ['/chat/completions', '/v1/chat/completions']],
    ['openrouter.ai', ['/api/v1/chat/completions']],
    ['generativelanguage.googleapis.com', ['/v1beta/openai/chat/completions']]
  ]);
  const caiyunHeaders = new Set(['accept', 'content-type', 'x-authorization', 't-authorization', 'device-id']);
  const aiHeaders = new Set(['accept', 'content-type', 'authorization']);
  function need(value) { if (!value) throw new Error('Translation endpoint or request rejected'); }
  function sender(value, id) {
    need(value && value.id === id && value.frameId === 0 && value.tab && Number.isInteger(value.tab.id) && value.tab.id >= 0);
    const url = new URL(value.url);
    need(url.protocol === 'https:' && !url.username && !url.password && !url.port &&
      ['game.granbluefantasy.jp', 'gbf.game.mbga.jp'].includes(url.hostname));
  }
  function request(value) {
    need(value && typeof value === 'object' && typeof value.url === 'string' && value.url.length <= 4096);
    const url = new URL(value.url);
    need(url.protocol === 'https:' && !url.username && !url.password && !url.port && !url.hash);
    const known = paths.get(url.hostname);
    // The unmodified script emits OpenAI-compatible chat bodies. Native
    // Anthropic/Gemini payloads are not implemented by it and are not accepted.
    need(known && known.includes(url.pathname) && !url.search);
    need(value.method === 'POST');
    need(value.responseType === 'text' || value.responseType === 'json' || !value.responseType);
    need(typeof value.data === 'string' && new TextEncoder().encode(value.data).length <= 512 * 1024);
    const body = JSON.parse(value.data);
    need(body && typeof body === 'object' && !Array.isArray(body));
    if (url.hostname === 'api.interpreter.caiyunai.com') {
      if (url.pathname === '/v1/user/jwt/generate')
        need(Object.keys(body).length === 1 && typeof body.browser_id === 'string' && body.browser_id.length <= 128);
      else need(Array.isArray(body.source) && body.source.length <= 4096 &&
        body.source.every(line => typeof line === 'string') && ['ja2zh', 'en2zh'].includes(body.trans_type));
    } else need(Array.isArray(body.messages) && body.messages.length <= 64 &&
      body.messages.every(message => message && ['system', 'user', 'assistant'].includes(message.role) &&
        typeof message.content === 'string') && typeof body.model === 'string' && body.model.length <= 128);
    const input = value.headers || {}; need(input && typeof input === 'object' && !Array.isArray(input));
    const entries = Object.entries(input); need(entries.length <= 16);
    const output = {}, seen = new Set();
    for (const [key, text] of entries) {
      need(typeof text === 'string' && text.length <= 4096 && !/[\r\n\0]/.test(text));
      const name = key.toLowerCase();
      need(!seen.has(name)); seen.add(name);
      if (name === 'origin' || name === 'referer') {
        // Fetch cannot set these forbidden headers; the narrow session rule
        // supplies the original Caiyun values for our extension only.
        need(url.hostname === 'api.interpreter.caiyunai.com' &&
          text === (name === 'origin' ? 'https://fanyi.caiyunapp.com' : 'https://fanyi.caiyunapp.com/'));
      } else if (name === 'x-title' || name === 'http-referer') {
        need(url.hostname !== 'api.interpreter.caiyunai.com' &&
          text === (name === 'x-title' ? 'BLHXFY' : 'https://github.com/biuuu/BLHXFY'));
        output[name] = text;
      } else {
        need((url.hostname === 'api.interpreter.caiyunai.com' ? caiyunHeaders : aiHeaders).has(name));
        if (name === 'content-type') need(text === 'application/json');
        output[name] = text;
      }
    }
    return {url: url.href, method: value.method, headers: output, body: value.data,
      caiyun: url.hostname === 'api.interpreter.caiyunai.com'};
  }
  return {sender, request};
})();
