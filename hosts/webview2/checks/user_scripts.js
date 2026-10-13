'use strict';
// Exercise newly added persistence/network/cancellation boundaries in a VM.
// No browser, real profile, credentials or network is used by these checks.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const directory = path.join(__dirname, '../src/scripts');
const source = name => fs.readFileSync(path.join(directory, name), 'utf8');
const config = JSON.parse(fs.readFileSync(path.join(__dirname,
  '../../../app/src/bundled_assets/GBF-Bookmark-Config.json'), 'utf8'));
let passed = 0;
async function check(name, fn) { await fn(); ++passed; console.log('PASS ' + name); }
function event() { const callbacks = []; return {addListener: fn => callbacks.push(fn),
  fire: (...args) => callbacks.map(fn => fn(...args))}; }
const extensionId = 'abcdefghijklmnopabcdefghijklmnop';
const sender = () => ({id: extensionId, frameId: 0, tab: {id: 31}, url: 'https://game.granbluefantasy.jp/'});
const jwt = () => ({url: 'https://api.interpreter.caiyunai.com/v1/user/jwt/generate', method: 'POST',
  headers: {'Content-Type': 'application/json', 'X-Authorization': 'synthetic',
    Origin: 'https://fanyi.caiyunapp.com', Referer: 'https://fanyi.caiyunapp.com/', 'Device-Id': 'synthetic'},
  responseType: 'json', data: JSON.stringify({browser_id: 'synthetic'})});
const ai = () => ({url: 'https://api.openai.com/v1/chat/completions', method: 'POST',
  headers: {'Content-Type': 'application/json', Authorization: 'Bearer synthetic',
    'X-Title': 'BLHXFY', 'HTTP-Referer': 'https://github.com/biuuu/BLHXFY'}, responseType: 'json',
  data: JSON.stringify({model: 'synthetic', messages: [{role: 'system', content: 'Translate'},
    {role: 'user', content: 'synthetic'}], response_format: {type: 'json_object'}})});
function policy() {
  const state = {URL, TextEncoder}; vm.createContext(state); vm.runInContext(source('request_policy.js'), state);
  return state.GbrowserScriptPolicy;
}
function seed(data = new Map(), write) {
  const window = {}; window.top = window;
  const state = {window, location: {protocol: 'https:', hostname: 'game.granbluefantasy.jp'},
    localStorage: {getItem: key => data.has(key) ? data.get(key) : null,
      setItem: (key, value) => { if (write) write(key, value); data.set(key, value); },
      removeItem: key => data.delete(key)}, console: {warn() {}}};
  vm.createContext(state); vm.runInContext(source('bookmark_seed.js') + `(${JSON.stringify(config)});`, state);
  return data;
}
function bookmarkContext(parent = false) {
  const handlers = new Map(), opened = []; let serial = 0;
  const location = {href: 'https://game.granbluefantasy.jp/#mypage'};
  class Element {
    constructor(href, inside = true) { this.value = href; this.inside = inside; }
    get href() { return new URL(this.value, location.href).href; }
    closest(selector) {
      assert.equal(selector, '#gbf-bookmark-lacia a.bookmark-item-lacia[href]');
      return this.inside && this.value !== undefined ? this : null;
    }
  }
  const window = {open: (...args) => opened.push(args)}; window.top = parent ? {} : window;
  const state = {window, Element, location, URL, crypto: {randomUUID: () =>
    `00000000-0000-4000-8000-${String(++serial).padStart(12, '0')}`},
    document: {addEventListener: (name, handler, capture) => {
      assert.equal(capture, true); handlers.set(name, handler);
    }}};
  vm.createContext(state); vm.runInContext(source('bookmark_context.js'), state);
  return {handlers, opened, Element, fire(value, options = {}) {
    let prevented = 0;
    handlers.get('auxclick')({isTrusted: true, button: 1, target: new Element(value), ...options,
      preventDefault: () => ++prevented});
    return prevented;
  }};
}
function worker(options = {}) {
  const ports = [], timers = new Map(), requests = [], rules = [], connect = event(); let timerId = 0;
  const state = {URL, TextEncoder, TextDecoder, AbortController,
    setTimeout: (fn, delay) => { timers.set(++timerId, {fn, delay}); return timerId; },
    clearTimeout: id => timers.delete(id),
    chrome: {runtime: {id: extensionId, onConnect: connect}, declarativeNetRequest: {
      updateSessionRules: value => { rules.push(value); return options.rules ? options.rules(value) : Promise.resolve(); }}},
    fetch: async (url, init) => {
      requests.push({url, init}); if (options.fetch) return options.fetch(url, init);
      let read = false; return {status: 200, statusText: 'OK', body: {getReader: () => ({
        read: async () => read ? {done: true} : (read = true, {done: false, value: new TextEncoder().encode('{"ok":true}')}),
        cancel: async () => {}})}};
    }};
  vm.createContext(state);
  state.importScripts = () => vm.runInContext(source('request_policy.js'), state);
  vm.runInContext(source('worker.js'), state);
  function port(value = sender()) {
    const result = {name: 'gbrowser-translation-v1', sender: value, onMessage: event(), onDisconnect: event(),
      replies: [], disconnected: false, postMessage: v => result.replies.push(v), disconnect: () => {
        if (!result.disconnected) { result.disconnected = true; result.onDisconnect.fire(); }} };
    ports.push(result); connect.fire(result); return result;
  }
  return {port, ports, timers, requests, rules};
}
(async () => {
  await check('bookmark right click scopes the menu and preserves editable controls and original toggle', () => {
    const handlers = new Map(); let toggled = 0;
    class Element {
      constructor(kind) { this.kind = kind; }
      closest(selector) {
        if (selector.startsWith('input')) return this.kind === 'input' ? this : null;
        if (selector === '#show-setting-bookmark') return this.kind === 'hotspot' ? this : null;
        return ['bookmark', 'settings', 'hotspot'].includes(this.kind) ? this : null;
      }
      click() { ++toggled; }
    }
    const window = {}; window.top = window;
    const state = {window, Element, document: {addEventListener: (name, handler, capture) => {
      assert.equal(capture, true); handlers.set(name, handler);
    }}};
    vm.createContext(state); vm.runInContext(source('bookmark_context.js'), state);
    for (const kind of ['bookmark', 'settings', 'hotspot', 'input', 'game']) {
      let cancelled = 0;
      handlers.get('contextmenu')({target: new Element(kind), preventDefault: () => ++cancelled});
      assert.equal(cancelled, ['bookmark', 'settings', 'hotspot'].includes(kind) ? 1 : 0);
    }
    handlers.get('mouseup')({target: new Element('bookmark'), button: 2});
    handlers.get('mouseup')({target: new Element('hotspot'), button: 0});
    assert.equal(toggled, 0); // Neither duplicate the bookmark handler nor left click.
    handlers.get('mouseup')({target: new Element('hotspot'), button: 2});
    assert.equal(toggled, 1);
  });
  await check('trusted middle bookmark opens resolved route once with fresh window classification', () => {
    const c = bookmarkContext();
    assert.equal(c.fire('#quest'), 1); assert.equal(c.fire('#quest'), 1);
    assert.equal(c.opened.length, 2);
    for (const [url, name] of c.opened) {
      assert.equal(url, 'https://game.granbluefantasy.jp/#quest');
      assert.match(name, /^gbrowser-bookmark-middle-[0-9a-f-]{36}$/);
    }
    assert.notEqual(c.opened[0][1], c.opened[1][1]);
    assert.equal(c.fire('/#arcarum2'), 1);
    assert.equal(c.opened[2][0], 'https://game.granbluefantasy.jp/#arcarum2');
    assert.equal(c.fire('https://example.test/path?x=1#here'), 1);
    assert.equal(c.opened[3][0], 'https://example.test/path?x=1#here');
  });
  await check('middle adapter preserves other buttons, action items, settings and non-bookmark links', () => {
    const c = bookmarkContext();
    for (const options of [{button: 0}, {button: 2}, {isTrusted: false}, {target: {}},
      {target: new c.Element('#quest', false)}]) assert.equal(c.fire('#quest', options), 0);
    assert.equal(c.fire(undefined), 0); // Action items have onclick and no href.
    assert.equal(c.opened.length, 0);
    assert.equal(bookmarkContext(true).handlers.size, 0);
  });
  await check('middle adapter rejects privileged schemes, credentials and excessive URLs', () => {
    const c = bookmarkContext();
    for (const url of ['javascript:location.reload()', 'file:///C:/test', 'data:text/html,test',
      'chrome-extension://test/popup.html', 'https://user:secret@example.test/',
      'https://example.test/' + 'x'.repeat(8192)]) assert.equal(c.fire(url), 0);
    assert.equal(c.opened.length, 0);
  });
  await check('fresh bookmark configuration uses the fixed product data', () => {
    const data = seed(); assert.deepEqual(JSON.parse(data.get('gbf-bookmark:data')), config.data);
    assert.deepEqual(JSON.parse(data.get('gbf-bookmark:config')), config.config);
    assert.equal(config.data.length, 35);
  });
  await check('existing or partial bookmark configuration is never overwritten', () => {
    for (const key of ['gbf-bookmark:data', 'gbf-bookmark:config']) {
      const data = new Map([[key, 'user edited bytes']]); seed(data);
      assert.deepEqual([...data], [[key, 'user edited bytes']]);
    }
  });
  await check('failed second bookmark write rolls back only own first bytes', () => {
    const data = new Map(); seed(data, key => { if (key.endsWith(':config')) throw new Error('quota'); });
    assert.equal(data.size, 0);
    const concurrent = new Map(); seed(concurrent, key => {
      if (key.endsWith(':data')) concurrent.set('gbf-bookmark:config', 'user replacement');
    });
    assert.deepEqual([...concurrent], [['gbf-bookmark:config', 'user replacement']]);
  });
  await check('frame identity rejects unrelated, child and forged extension senders', () => {
    const p = policy(); p.sender(sender(), extensionId);
    for (const value of [undefined, {...sender(), id: 'wrong'}, {...sender(), frameId: 1},
      {...sender(), url: 'https://game.granbluefantasy.jp.evil.test/'}, {...sender(), url: 'http://game.granbluefantasy.jp/'},
      {...sender(), url: 'https://game.granbluefantasy.jp:444/'}, {...sender(), tab: {id: -1}}])
      assert.throws(() => p.sender(value, extensionId));
  });
  await check('original Caiyun and OpenAI-compatible request forms remain accepted', () => {
    const p = policy(), result = p.request(jwt());
    assert.equal(result.headers.origin, undefined); assert.equal(result.headers.referer, undefined);
    const second = p.request(ai()); assert.equal(second.headers['x-title'], 'BLHXFY');
    assert.equal(second.headers['http-referer'], 'https://github.com/biuuu/BLHXFY');
    for (const url of ['https://api.deepseek.com/chat/completions', 'https://openrouter.ai/api/v1/chat/completions',
      'https://generativelanguage.googleapis.com/v1beta/openai/chat/completions']) p.request({...ai(), url});
  });
  await check('bridge rejects arbitrary methods, paths, headers, body and excess bytes', () => {
    const p = policy();
    for (const value of [{...ai(), method: 'GET'}, {...ai(), url: 'https://api.openai.com/v1/files'},
      {...ai(), url: 'http://api.openai.com/v1/chat/completions'},
      {...ai(), url: 'https://api.openai.com/v1/chat/completions?unexpected=1'},
      {...ai(), headers: {Cookie: 'private'}}, {...ai(), headers: {'X-Title': 'other'}},
      {...ai(), headers: {Authorization: 'one', authorization: 'two'}},
      {...ai(), headers: {Authorization: 'bad\r\nheader'}}, {...jwt(), headers: {Authorization: 'other'}},
      {...ai(), data: '{}'}, {...ai(), data: '[]'}, {...ai(), data: 'x'.repeat(524289)},
      {...jwt(), data: '{"browser_id":"x","extra":true}'},
      {...ai(), url: 'https://api.anthropic.com/v1/messages'}]) assert.throws(() => p.request(value));
  });
  await check('worker sends a bounded credential-free nonredirecting request once', async () => {
    const w = worker(), port = w.port(); await Promise.all(port.onMessage.fire(jwt()));
    assert.equal(w.requests.length, 1); const init = w.requests[0].init;
    assert.equal(init.credentials, 'omit'); assert.equal(init.redirect, 'error');
    assert.equal(port.replies.length, 1); assert.equal(port.replies[0].result.responseText, '{"ok":true}');
    assert.equal(port.disconnected, true); assert.equal(w.timers.size, 0);
    const rule = w.rules[0].addRules[0];
    assert.deepEqual(Array.from(rule.condition.initiatorDomains), [extensionId]);
    assert.deepEqual(Array.from(rule.condition.requestMethods), ['post']);
  });
  await check('cancel while preparing Caiyun prevents a later network request', async () => {
    let resolve; const w = worker({rules: () => new Promise(fn => { resolve = fn; })}), port = w.port();
    const pending = port.onMessage.fire(jwt()); port.disconnect(); resolve(); await Promise.all(pending);
    assert.equal(w.requests.length, 0); assert.equal(port.replies.length, 0); assert.equal(w.timers.size, 0);
  });
  await check('duplicate inflight request aborts without replay or late result', async () => {
    let resolve; const w = worker({rules: () => new Promise(fn => { resolve = fn; })}), port = w.port();
    const pending = port.onMessage.fire(jwt()); await Promise.all(port.onMessage.fire(jwt()));
    resolve(); await Promise.all(pending); assert.equal(w.requests.length, 0);
    assert.equal(port.replies.length, 1); assert.equal(port.replies[0].error, 'Duplicate request rejected');
  });
  await check('worker connection budget releases canceled requests', () => {
    const w = worker(); for (let i = 0; i < 8; ++i) assert.equal(w.port().disconnected, false);
    assert.equal(w.port().disconnected, true); w.ports[0].disconnect(); assert.equal(w.port().disconnected, false);
    for (const port of w.ports) port.disconnect(); assert.equal(w.timers.size, 0);
  });
  await check('oversized upstream response becomes a static failure without content', async () => {
    const w = worker({fetch: async () => ({status: 200, statusText: 'OK', body: {getReader: () => ({
      read: async () => ({done: false, value: new Uint8Array(2097153)}), cancel: async () => {}})}})});
    const port = w.port(); await Promise.all(port.onMessage.fire(ai()));
    assert.equal(port.replies.length, 1); assert.equal(port.replies[0].result, undefined);
    assert.equal(port.replies[0].error, 'Translation request failed or was not allowed');
  });
  // Build output is part of the installer; this catches a missing/inert seed or
  // source copied under a name absent from the actual extension manifest.
  await check('generated extension includes referenced code and an invoked seed', () => {
    const root = path.join(__dirname, '../.build/native/Release/Scripts');
    const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json'), 'utf8'));
    for (const name of [manifest.background.service_worker, 'request_policy.js',
      ...manifest.content_scripts.flatMap(group => group.js)]) assert.equal(fs.statSync(path.join(root, name)).isFile(), true);
    const window = {}; window.top = window; const data = new Map();
    vm.runInNewContext(fs.readFileSync(path.join(root, 'bookmark_seed.js'), 'utf8'), {window,
      location: {protocol: 'https:', hostname: 'game.granbluefantasy.jp'}, console,
      localStorage: {getItem: key => data.has(key) ? data.get(key) : null, setItem: (key, value) => data.set(key, value), removeItem: key => data.delete(key)}});
    assert.deepEqual(JSON.parse(data.get('gbf-bookmark:data')), config.data);
  });
  console.log(`${passed} fixed-script checks; browser runtime NOT_EVALUATED`);
})().catch(error => { console.error(error); process.exitCode = 1; });
