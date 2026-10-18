'use strict';
// New bridge boundaries only, in an isolated JS VM; no browser, profile or
// network. These assertions do not re-run the already accepted CEF UI manual.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = name => fs.readFileSync(path.join(__dirname, '../src', name), 'utf8');
const id = 'abcdefghijklmnopabcdefghijklmnop';
const expected = `chrome-extension://${id}/options.html`;
function context() {
  let clock = 0;
  const timers = new Map();
  const state = {window: {}, location: {href: expected}, chrome: {runtime: {id}},
    setTimeout: fn => { timers.set(++clock, fn); return clock; }, clearTimeout: n => timers.delete(n)};
  vm.createContext(state);
  return {state, timers, run: script => vm.runInContext(script, state)};
}
let passed = 0;
function check(name, work) { work(); ++passed; console.log('PASS ' + name); }
check('wrong extension document cannot install the bridge', () => {
  const c = context(); c.state.location.href = 'https://game.granbluefantasy.jp/';
  assert.throws(() => c.run(source('proxy_bridge.js') + `(1,${JSON.stringify(expected)});`));
});
check('proxy operation mismatch does not enter the queue', () => {
  const c = context(); c.run(source('proxy_bridge.js') + `(3,${JSON.stringify(expected)});`);
  let rejected = 0;
  c.state.window.gbrowserProxyQuery({request: '2:apply', onFailure: () => ++rejected});
  assert.equal(rejected, 1); assert.equal(c.state.window.__gbrowserProxyTake(), null);
});
check('denied or stale document reply cannot authorize a write', () => {
  const c = context(); c.run(source('proxy_bridge.js') + `(3,${JSON.stringify(expected)});`);
  let rejected = 0, authorized = 0;
  const send = () => c.state.window.gbrowserProxyQuery({request: '3:apply',
    onFailure: () => ++rejected, onSuccess: () => ++authorized});
  send(); assert.equal(c.state.window.__gbrowserProxyTake(), '1|3:apply');
  c.state.window.__gbrowserProxyReply(1, 'denied');
  send(); c.state.location.href = expected.replace('options.html', 'popup.html');
  c.state.window.__gbrowserProxyReply(2, 'authorized');
  assert.equal(rejected, 2); assert.equal(authorized, 0);
});
check('timed out reply is not replayed', () => {
  const c = context(); c.run(source('proxy_bridge.js') + `(3,${JSON.stringify(expected)});`);
  let rejected = 0, authorized = 0;
  c.state.window.gbrowserProxyQuery({request: '3:preflight',
    onFailure: () => ++rejected, onSuccess: () => ++authorized});
  const timer = c.timers.values().next().value; timer();
  c.state.window.__gbrowserProxyReply(1, 'authorized');
  assert.equal(rejected, 1); assert.equal(authorized, 0);
});
function targets() {
  const c = context(), callbacks = [];
  c.state.chrome.tabs = {query: (filter, callback) => {
    assert.equal(Object.keys(filter).length, 0); callbacks.push(callback);
  }};
  c.bind = (n, known = 0) => c.run(source('target_binding.js') + `(${n},${JSON.stringify(expected)},${known});`);
  c.callbacks = callbacks;
  return c;
}
const gameTab = id => ({id, url: 'https://game.granbluefantasy.jp/#mypage'});
check('target requires one positive signed-32-bit game tab', () => {
  for (const [tabs, reason, sample] of [[[], 3, 0], [[gameTab(1), gameTab(2)], 3, 0],
       [[gameTab(-1)], 4, -1], [[gameTab(0)], 4, 0], [[gameTab(2147483648)], 4, 0]]) {
    const c = targets(); c.bind(1); c.callbacks[0](tabs);
    assert.equal(c.state.window.__gbrowserGameTarget, `1:0:${reason}:${sample}`);
  }
  for (const id of [47, 1000000000, 2147483647]) {
    const c = targets(); c.bind(2); c.callbacks[0]([gameTab(id)]);
    assert.equal(c.state.window.__gbrowserGameTarget, `2:${id}:0:${id}`);
  }
});
check('local exact game origin rejects unrelated/spoofed tabs and malformed results', () => {
  for (const url of ['https://game.granbluefantasy.jp.evil/', 'https://game.granbluefantasy.jp@evil/',
       'http://game.granbluefantasy.jp/', 'https://game.granbluefantasy.jp:999/', 'about:blank']) {
    const c = targets(); c.bind(1); c.callbacks[0]([{id: 99, url}, gameTab(47)]);
    assert.equal(c.state.window.__gbrowserGameTarget, '1:47:0:47');
  }
  for (const tabs of [undefined, {}, Array(33).fill(gameTab(47))]) {
    const c = targets(); c.bind(1); c.callbacks[0](tabs);
    assert.equal(c.state.window.__gbrowserGameTarget, '1:0:2:0');
  }
  const c = targets(); c.bind(1); c.callbacks[0]([null, {}, gameTab(47)]);
  assert.equal(c.state.window.__gbrowserGameTarget, '1:47:0:47');
});
check('late generation and switched document cannot bind the target', () => {
  const c = targets(); c.bind(1); c.bind(2); c.callbacks[0]([gameTab(1)]);
  assert.equal(c.state.window.__gbrowserGameTarget, null);
  c.state.location.href = expected.replace('options.html', 'popup.html');
  c.callbacks[1]([gameTab(2)]); assert.equal(c.state.window.__gbrowserGameTarget, null);
});
check('runtime query error does not produce a target', () => {
  const c = targets(); c.bind(1); c.state.chrome.runtime.lastError = {message: 'test'};
  c.callbacks[0]([gameTab(5)]); assert.equal(c.state.window.__gbrowserGameTarget, '1:0:1:0');
});
check('additional game tabs cannot replace the verified permanent game ID', () => {
  const c = targets(); c.bind(1, 47); c.callbacks[0]([gameTab(96), gameTab(47)]);
  assert.equal(c.state.window.__gbrowserGameTarget, '1:47:0:47');
  c.bind(2, 47); c.callbacks[1]([gameTab(96)]);
  assert.equal(c.state.window.__gbrowserGameTarget, '2:0:3:0');
});
check('missing, duplicate or invalid remembered identity never falls back to another tab', () => {
  for (const tabs of [[], [gameTab(48)], [gameTab(47), gameTab(47)], [{id: 47, url: 'about:blank'}]]) {
    const c = targets(); c.bind(1, 47); c.callbacks[0](tabs);
    assert.equal(c.state.window.__gbrowserGameTarget, '1:0:3:0');
  }
});
function gameUi() {
  const messages = [], events = new Map(), options = [];
  let inset = 64, node = {dataset: {showMenubarWidth: '64'}, getBoundingClientRect: () => ({left: inset})};
  const window = {innerWidth: 900, chrome: {webview: {postMessage: value => messages.push(value)}}, addEventListener: () => {}};
  window.top = window;
  let editor = null, menu = null, zoom = 2, writes = 0;
  const styles = new Map([['left', '20px'], ['width', '280px']]);
  const document = {documentElement: {}, getElementById: name => name === 'mobage-game-container' ? node :
      name === 'gbf-bookmark-setting' ? editor : name === 'submenu' ? menu : null,
    addEventListener: (name, callback) => events.set(name, callback)};
  let observer;
  const state = {window, document, getComputedStyle: () => ({left: styles.get('left'), width: styles.get('width'), boxSizing: 'content-box'}),
    location: {origin: 'https://game.granbluefantasy.jp'},
    MutationObserver: class {constructor(callback) {observer = callback;} observe(_, value) {options.push(value);}}};
  vm.createContext(state);
  return {messages, events, options, state, run: () => vm.runInContext(source('game_ui.js') + '();' + source('pane_pointer.js') + '();', state),
    resize: value => {inset = value; observer();}, node: () => node, remove: () => {node = null;},
    editor: (width, ratio) => {
      window.innerWidth = width; zoom = ratio;
      editor = {offsetWidth: 280, style: {getPropertyValue: name => styles.get(name),
        setProperty: (name, value) => {styles.set(name, value); ++writes;}},
        getBoundingClientRect: () => ({left: parseFloat(styles.get('left')) * zoom,
          width: parseFloat(styles.get('width')) * zoom})};
      observer(); return editor.getBoundingClientRect();
    }, writes: () => writes, menu: (left, width, recognized = true) => {
      menu = {querySelector: () => recognized ? {} : null, getBoundingClientRect: () => ({left, width})}; observer();
    }};
}
check('secondary menu measurement uses verified public submenu geometry and rejects missing/spurious regions', () => {
  const c = gameUi(); c.run(); c.messages.length = 0;
  c.menu(750, 150); assert.deepEqual(c.messages, ['gbrowser:submenu:150']);
  c.menu(750, 150); c.menu(100, 800); c.menu(800, 0); c.menu(700, 200, false);
  assert.equal(c.messages.length, 1);
  c.menu(780, 120); assert.equal(c.messages[1], 'gbrowser:submenu:120');
  c.run(); assert.equal(c.messages[2], 'gbrowser:submenu:120'); // Restore remeasures even at unchanged CSS width.
});
check('bookmark editor stays inside each game viewport after Moba crop and late CSS zoom without mutation loops', () => {
  const c = gameUi(); c.run();
  for (const width of [320, 517, 850]) for (const zoom of [1, 1.25, 2.7, 3]) {
    const rect = c.editor(width, zoom);
    assert.ok(rect.left >= 71.99 && rect.left + rect.width <= width - 7.99);
    const writes = c.writes(); c.editor(width, zoom); c.editor(width, zoom);
    assert.equal(c.writes(), writes, 'self-authored styles caused repeated observer writes');
  }
  c.resize(90);
  assert.ok(c.editor(600, 2).left >= 97.99, 'late Moba geometry did not reposition editor');
});
check('game adapter reports only bounded public inset and watches late Moba attributes', () => {
  const c = gameUi(); c.run();
  assert.deepEqual(c.messages, ['gbrowser:moba:64']);
  assert.ok(c.options[0].attributeFilter.includes('data-show-menubar-width'));
  c.resize(64); c.resize(999); c.resize(-1);
  assert.equal(c.messages.length, 1);
  c.resize(70); assert.equal(c.messages[1], 'gbrowser:moba:70');
  c.remove(); c.resize(80); assert.equal(c.messages.length, 2);
});
check('side button adapter consumes one trusted release and no game click', () => {
  const c = gameUi(); c.run(); c.messages.length = 0;
  let cancelled = 0;
  const event = (button, isTrusted = true) => ({button, isTrusted, preventDefault: () => ++cancelled});
  for (const button of [0, 1, 2]) {c.events.get('mousedown')(event(button)); c.events.get('mouseup')(event(button));}
  assert.deepEqual(c.messages, ['gbrowser:active-game']); c.messages.length = 0;
  c.events.get('mouseup')(event(3, false)); assert.equal(c.messages.length, 0);
  c.events.get('mousedown')(event(3)); assert.equal(c.messages.length, 0);
  c.events.get('mouseup')(event(3)); c.events.get('mouseup')(event(4));
  assert.deepEqual(c.messages, ['gbrowser:back', 'gbrowser:forward']); assert.equal(cancelled, 3);
  c.events.get('auxclick')(event(3)); assert.equal(c.messages.length, 2);
});
check('fixed helper side buttons share routing without a second native input path', () => {
  const c = gameUi(), expected = `chrome-extension://${id}/src/views/sidePanel/main.html?47`;
  c.state.location.href = expected; c.state.location.origin = `chrome-extension://${id}`;
  vm.runInContext(source('pane_pointer.js') + `(${JSON.stringify(expected)});`, c.state);
  const event = {button: 3, isTrusted: true, preventDefault: () => {}};
  c.events.get('mousedown')({...event, button: 0}); assert.deepEqual(c.messages, []);
  c.events.get('mouseup')(event); assert.deepEqual(c.messages, ['gbrowser:back']);
  c.state.location.href = 'about:blank'; c.events.get('mouseup')(event); assert.equal(c.messages.length, 1);
});
check('CD view selection needs a trusted game click, not script or other origin', () => {
  const c = gameUi(); c.run(); c.messages.length = 0;
  const click = {button: 0, isTrusted: false, preventDefault: () => assert.fail('game click consumed')};
  c.events.get('mousedown')(click); assert.deepEqual(c.messages, []);
  c.events.get('mousedown')({...click, isTrusted: true}); assert.deepEqual(c.messages, ['gbrowser:active-game']);
  c.state.location.origin = 'https://example.test';
  c.events.get('mousedown')({...click, isTrusted: true}); assert.equal(c.messages.length, 1);
});
check('game display adapter refuses child and unrelated page contexts', () => {
  const child = gameUi(); child.state.window.top = {}; child.run(); assert.equal(child.events.size, 0);
  const other = gameUi(); other.state.location.origin = 'https://connect.mobage.jp'; other.run(); assert.equal(other.messages.length, 0);
});
function gameRole(origin = 'https://game.granbluefantasy.jp') {
  const nodes = new Map(), window = {}; window.top = window;
  const state = {window, location: {origin}, document: {
    getElementById: id => nodes.get(id), createElement: tag => {assert.equal(tag, 'style');
      const node = {remove: () => nodes.delete(node.id)}; return node;},
    documentElement: {appendChild: node => nodes.set(node.id, node)}}};
  Object.defineProperty(state, 'localStorage', {get() {throw new Error('role must not modify shared profile storage');}});
  vm.createContext(state);
  return {nodes, state, run: (secondary, hidden = false) => vm.runInContext(source('game_role.js') + `(${secondary},${hidden});`, state)};
}
check('secondary menu hiding is reversible document-only styling and never applied to the primary', () => {
  const c = gameRole(); c.run(true, true); c.run(true, true); assert.equal(c.nodes.size, 2);
  assert.equal(c.nodes.get('gbrowser-secondary-menu-style').textContent,
    '#submenu{visibility:hidden!important;pointer-events:none!important}');
  c.run(true, false); assert.equal(c.nodes.size, 1);
  c.run(false, true); assert.equal(c.nodes.size, 0);
});
check('only secondary hides the precise fixed bookmark UI without shared configuration writes', () => {
  const c = gameRole(); c.run(true); c.run(true);
  assert.equal(c.nodes.size, 1);
  assert.equal(c.nodes.get('gbrowser-secondary-bookmark-style').textContent,
    '#gbf-bookmark-lacia,#show-setting-bookmark,#gbf-bookmark-setting{display:none!important}');
  c.run(false); assert.equal(c.nodes.size, 0);
});
check('bookmark view-role adapter refuses unrelated and child documents', () => {
  const other = gameRole('https://connect.mobage.jp'); other.run(true); assert.equal(other.nodes.size, 0);
  const child = gameRole(); child.state.window.top = {}; child.run(true); assert.equal(child.nodes.size, 0);
});
async function arcarumChecks() {
  const expected = `chrome-extension://${id}/src/panel/index.html`;
  const run = async (url, status, changeDocument = false, interrupt = '') => {
    const calls = [], location = {href: expected};
    let capture, storageChanged;
    const stop = {contains: node => node === child}, child = {};
    const document = {getElementById: name => name === 'btn-stop' ? stop : null,
      addEventListener: (name, callback, phase) => {assert.equal(name, 'click'); assert.equal(phase, true); capture = callback;}};
    const click = () => capture({isTrusted: true, target: child});
    const state = {window: {}, location, document, chrome: {tabs: {get: async target => {assert.equal(target, 47);
      if (interrupt === 'get') click();
      if (changeDocument) location.href = 'about:blank'; return {id: target, url};}},
      runtime: {sendMessage: async value => {
        calls.push(value);
        if (interrupt === 'late-enabled' && value.type === 'LISTEN_STATUS' && calls.length === 1) {
          const earlier = {ok: true, ...status}; status = {listening: true, tabId: 99};
          storageChanged({listenEnabled: {newValue: true}}, 'local'); return earlier;
        }
        if (interrupt === 'unknown-read' && value.type === 'LISTEN_STATUS') {
          storageChanged({listenEnabled: {newValue: true}}, 'local'); return undefined;
        }
        if (interrupt === 'read-storm' && value.type === 'LISTEN_STATUS')
          storageChanged({listenEnabled: {newValue: true}}, 'local');
        if (interrupt === value.type) click();
        if (interrupt === 'untrusted' && value.type === 'STOP_NETWORK_LISTEN') capture({isTrusted: false, target: stop});
        if (interrupt === 'unknown-start' && value.type === 'START_NETWORK_LISTEN') {click(); return undefined;}
        if (interrupt === 'failed-stop' && value.type === 'STOP_NETWORK_LISTEN') return {ok: false};
        return value.type === 'LISTEN_STATUS' ? {ok: true, ...status} : {ok: true};
      }}, storage: {onChanged: {addListener: callback => {storageChanged = callback;}}}}};
    await vm.runInNewContext(source('arcarum_binding.js') + `(47,${JSON.stringify(expected)});`, state);
    // Drain only the fake immediate API promises, including an event-coalesced read.
    for (let i = 0; i < 20; ++i) await Promise.resolve();
    return calls;
  };
  let calls = await run('https://game.granbluefantasy.jp/#arcarum3/dungeon', {listening: true, tabId: 99});
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'STOP_NETWORK_LISTEN', 'START_NETWORK_LISTEN']);
  assert.equal(calls[2].tabId, 47); ++passed; console.log('PASS original Arcarum explicit right target, no query ordering or RECHECK/map reset');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 47});
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS']); ++passed; console.log('PASS already matching listener is not restarted');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: false, tabId: null});
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS']); ++passed; console.log('PASS original manual listener STOP remains stopped');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: false, tabId: 99}, false, 'late-enabled');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'LISTEN_STATUS', 'STOP_NETWORK_LISTEN', 'START_NETWORK_LISTEN']);
  assert.equal(calls[3].tabId, 47);
  ++passed; console.log('PASS original separate tabId/enabled storage events retain right-game correction');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 47}, false, 'read-storm');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'LISTEN_STATUS']);
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, 'unknown-read');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS']);
  ++passed; console.log('PASS storage read coalescing is once-only and never follows an unknown result');
  for (const phase of ['get', 'LISTEN_STATUS', 'STOP_NETWORK_LISTEN']) {
    calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, phase);
    assert.ok(!calls.some(x => x.type === 'START_NETWORK_LISTEN'));
  }
  ++passed; console.log('PASS trusted manual STOP cancels pending target correction before START');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, 'START_NETWORK_LISTEN');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'STOP_NETWORK_LISTEN', 'START_NETWORK_LISTEN', 'STOP_NETWORK_LISTEN']);
  ++passed; console.log('PASS manual STOP during successful in-flight START has final priority once');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, 'unknown-start');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'STOP_NETWORK_LISTEN', 'START_NETWORK_LISTEN']);
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, 'failed-stop');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'STOP_NETWORK_LISTEN']);
  ++passed; console.log('PASS unknown START and failed STOP do not trigger another write');
  calls = await run('https://game.granbluefantasy.jp/#arcarum3', {listening: true, tabId: 99}, false, 'untrusted');
  assert.deepEqual(calls.map(x => x.type), ['LISTEN_STATUS', 'STOP_NETWORK_LISTEN', 'START_NETWORK_LISTEN']);
  ++passed; console.log('PASS synthetic STOP click cannot change trusted cancellation state');
  for (const url of ['https://example.test/#arcarum3', 'https://game.granbluefantasy.jp/#mypage'])
    assert.equal((await run(url, {listening: false})).length, 0);
  assert.equal((await run('https://game.granbluefantasy.jp/#arcarum3', {}, true)).length, 0);
  ++passed; console.log('PASS changed document/unrelated game cannot submit a listener switch');
  console.log(`${passed} synthetic boundary checks; browser runtime NOT_EVALUATED`);
}
arcarumChecks().catch(error => {console.error(error); process.exitCode = 1;});
