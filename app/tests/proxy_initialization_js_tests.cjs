// Run with the existing Node runtime. Synthetic APIs only; no browser/network,
// real profile state. The pinned package supplies the actual Options reset
// logic and PAC converter; browser APIs and local-PAC replies are simulated.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/proxy_initialization.js'), 'utf8');
const url = 'chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/options.html';
const config = JSON.parse(fs.readFileSync(path.join(__dirname, '../src/proxy_presets.config'), 'utf8'));
const clone = x => JSON.parse(JSON.stringify(x));
const cachedPac = 'function FindProxyForURL(url, host) { /* 本地 */ return "PROXY 127.0.0.1:8123"; }\n';
const originalPac = require('../../hosts/webview2/.deps/zeroomega/js/omega_pac.min.js');
const cachedAscii = originalPac.PacGenerator.ascii(cachedPac);
// The service worker and options document have separate module-global minifiers.
delete require.cache[require.resolve('../../hosts/webview2/.deps/zeroomega/js/omega_pac.min.js')];
const workerPac = require('../../hosts/webview2/.deps/zeroomega/js/omega_pac.min.js');
function derivedRule(profile) {
  const temp = originalPac.Profiles.create('__tempZeroRuleListPac', profile.profileType);
  Object.assign(temp, {defaultProfileName: profile.defaultProfileName, format: profile.format,
    matchProfileName: profile.matchProfileName, ruleList: profile.ruleList, isTempPacProfile: true});
  return originalPac.PacGenerator.ascii(originalPac.PacGenerator.script({['+' + temp.name]: temp}, temp.name,
    {profileNotFound: () => 'ignore'}).print_to_string({beautify: true, comments: true}));
}
function islandSettings(options) {
  const profile = options['+岛风go'];
  const script = workerPac.PacGenerator.ascii(workerPac.PacGenerator.compress(
    workerPac.PacGenerator.script(options, profile)).print_to_string());
  const name = workerPac.PacGenerator.ascii(JSON.stringify(profile.name)).replace(/\*/g, '\\u002a').replace(/\\/g, '\\u002f');
  return '/*OmegaProfile*' + name + '*' + profile.revision + '*/' + script;
}
// Exercise reset/init/_watch/getAll from the fixed original bundle using its
// in-memory Storage. Only actual browser proxy application is stubbed; no user
// extension storage, network, browser, credentials or package edits are used.
async function originalResetOptions(input) {
  const context = {setTimeout, clearTimeout, setImmediate,
    console: {log() {}, warn() {}, error(error) { throw error; }}, OmegaPac: originalPac,
    require(name) {
      if (name === '../package.json') return {version: '3.5.2'};
      if (name === './formatters') return {}; // Unused diagnostic formatter.
      throw new Error('unexpected original dependency: ' + name);
    }};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,
    '../../hosts/webview2/.deps/zeroomega/js/omega_target.min.js'), 'utf8'), context);
  const target = context.OmegaTarget;
  target.Log.method = () => {};
  const options = new target.Options(new target.Storage(), new target.Storage(),
    {method() {}, error(error) { throw error; }});
  options.applyProfile = () => target.Promise.resolve();
  await options.reset(clone(input));
  return clone(options.getAll());
}
let originalResetReadback, originalStorageReadbacks;
async function run(mode, action = 'initialize', readOnly = false) {
  const events = [], timers = new Map(); let sequence = 0, mutated = false, now = 0, workerOptions;
  const observers = new Set(), intervals = new Map();
  const context = {location: {href: url + '#general'}, window: {},
    AbortController, TextDecoder, OmegaPac: originalPac,
    Date: class extends Date { static now() { return now; } },
    setTimeout: (cb, delay) => { const id = ++sequence; timers.set(id, {cb, due: now + delay}); return id; },
    clearTimeout: id => timers.delete(id), chrome: {runtime: {id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'}}};
  context.setInterval = cb => { const id = ++sequence; intervals.set(id, cb); return id; };
  context.clearInterval = id => intervals.delete(id);
  context.fetch = async (address, options) => {
    events.push('pacFetch');
    assert.equal(address, config['+acgpower'].pacUrl);
    assert.equal(options.redirect, 'error'); assert.equal(options.credentials, 'omit');
    assert.equal(options.cache, 'no-store');
    if (mode === 'pac-fetch-timeout') return new Promise((resolve, reject) => {
      options.signal.addEventListener('abort', () => reject(new Error('synthetic abort')), {once: true});
    });
    let read = false;
    return {ok: mode !== 'pac-fetch-http-error',
      url: mode === 'pac-fetch-redirect' ? 'http://127.0.0.1:9999/proxy.pac' : address,
      body: {getReader: () => ({read: async () => {
        if (read) return {done: true}; read = true;
        return {done: false, value: mode === 'pac-fetch-large' ? new Uint8Array(2 * 1024 * 1024 + 1) :
          mode === 'pac-fetch-utf8' ? new Uint8Array([0xff]) : Buffer.from(mode === 'pac-cache-stale' ? 'different' : cachedPac)};
      }, cancel: async () => {}})}};
  };
  if (mode === 'proxy-replace') {
    const previous = () => {}; observers.add(previous); intervals.set(-1, previous);
    context.window.__gbrowserProxyWatch = {stop: () => {
      events.push('stopPrevious'); observers.delete(previous); intervals.delete(-1);
    }};
  }
  context.location.reload = () => events.push('reload');
  context.chrome.proxy = {settings: {
    onChange: {addListener: fn => observers.add(fn), removeListener: fn => observers.delete(fn)},
    get: (details, reply) => {
      events.push('proxyGet'); assert.deepEqual(clone(details), {incognito: false});
      if (mode === 'proxy-timeout') return;
      if (mode === 'proxy-throw') throw new Error('synthetic');
      if (mode === 'proxy-last-error') context.chrome.runtime.lastError = {message: 'synthetic'};
      const effective = {levelOfControl: mode === 'proxy-control' ? 'controlled_by_other_extensions' : 'controlled_by_this_extension',
        value: {mode: mode === 'proxy-mode' ? 'direct' : 'pac_script',
          pacScript: {mandatory: mode !== 'proxy-optional',
            url: mode === 'proxy-url' ? 'http://127.0.0.1:9999/proxy.pac' : config['+acgpower'].pacUrl}}};
      if (action === 'direct') effective.value = {mode: mode === 'proxy-mode' ? 'system' : 'direct'};
      if (action === '岛风go') effective.value = {mode: 'pac_script', pacScript: {mandatory: mode !== 'proxy-optional', data: mode === 'proxy-url' ? 'wrong' : islandSettings(clone(workerOptions))}};
      if (action === '岛风go' && mode.startsWith('island-tamper-')) {
        const pac = effective.value.pacScript;
        if (mode === 'island-tamper-port') pac.data = pac.data.replace('localhost:8099', 'localhost:9999');
        if (mode === 'island-tamper-rule') pac.data = pac.data.replace('granbluefantasy', 'not-the-game');
        if (mode === 'island-tamper-factory') pac.data = pac.data.replace('43===', '44===');
        if (mode === 'island-tamper-global') pac.data = pac.data.replace('FindProxyForURL=', 'OtherGlobal=');
      }
      if (mode.startsWith('pac-cache') && mode !== 'pac-cache-url') {
        effective.value.pacScript = {mandatory: mode !== 'pac-cache-optional',
          data: mode === 'pac-cache-data-wrong' ? 'different' : cachedAscii};
        if (mode === 'pac-cache-both') effective.value.pacScript.url = config['+acgpower'].pacUrl;
      }
      if (mode === 'pac-empty-data') effective.value.pacScript = {mandatory: true, data: cachedAscii};
      reply(effective);
      delete context.chrome.runtime.lastError;
      if (mode === 'proxy-changed-during-read') for (const fn of [...observers]) fn({});
      if (mode === 'proxy-document-changed') context.location.href = 'about:blank';
    }
  }};
  if (mode === 'proxy-missing') delete context.chrome.proxy;
  if (mode === 'wrong-page') context.location.href = url.replace('options', 'popup');
  if (mode === 'wrong-id') context.chrome.runtime.id = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
  context.window.gbrowserProxyQuery = query => {
    const verb = query.request.split(':')[1]; events.push(verb);
    if (verb === 'begin' || verb === 'apply') {
      assert.equal(mutated, false, 'reset preceded host authorization');
      if (mode === 'deny') return query.onFailure(1, 'denied');
      if (verb === 'apply' && mode === 'deny-apply') return query.onFailure(1, 'denied');
      if (verb === 'apply' && mode === 'wrong-apply-ack') return query.onSuccess('received');
      if (mode === 'wrong-ack') return query.onSuccess('received');
      if (mode === 'changed-after-begin') context.location.href = 'about:blank';
      if (verb === 'apply' && mode === 'changed-after-apply') context.location.href = 'about:blank';
      if (mode === 'document-used') context.window.__gbrowserProxyInitializationStarted = true;
      return query.onSuccess('authorized');
    }
    if (verb === 'matched' && mode === 'deny-result') return query.onFailure(1, 'denied');
    query.onSuccess('received');
  };
  context.chrome.runtime.sendMessage = (request, reply) => {
    events.push(request.method);
    assert.equal(request.noReply, undefined, 'noReply hid a worker failure');
    if (request.method === 'reset') {
      assert.equal(mutated, false, 'reset replayed'); mutated = true;
      assert.equal(request.args.length, 1);
      assert.deepEqual(clone(request.args[0]), config, 'submitted options differ from bundled bytes');
      if (mode === 'throw') throw new Error('synthetic');
      if (mode === 'timeout') return;
      if (mode === 'worker-error') return reply({error: {reason: 'synthetic'}});
      if (mode === 'missing-reply') return reply(undefined);
      if (mode === 'last-error') {
        context.chrome.runtime.lastError = {message: 'synthetic'};
        reply({}); delete context.chrome.runtime.lastError; return;
      }
      reply({});
      if (mode === 'duplicate-reply') reply({});
      if (mode === 'changed-after-reset') context.location.href = 'about:blank';
    } else if (request.method === 'getAll') {
      assert.equal(request.args.length, 0);
      const readIndex = events.filter(event => event === 'getAll').length - 1;
      const actual = clone(mode === 'original-storage' ?
        originalStorageReadbacks[Math.min(readIndex, originalStorageReadbacks.length - 1)] :
        mode === 'original-reset' ? originalResetReadback : config);
      if (mode === 'runtime-fields' || mode.startsWith('pac-cache') || mode.startsWith('pac-fetch')) {
        actual['+acgpower'].revision = '199dcaee321';
        actual['+acgpower'].lastUpdate = '2026-10-11T13:00:00.000Z';
        actual['+acgpower'].sha256 = 'a'.repeat(64);
      }
      if (mode.startsWith('pac-cache') || mode.startsWith('pac-fetch')) actual['+acgpower'].pacScript = cachedPac;
      if (mode.startsWith('pac-empty')) actual['+acgpower'].pacScript = '';
      if (mode === 'revision-invalid') actual['+acgpower'].revision = {};
      if (mode === 'timestamp-invalid') actual['+acgpower'].lastUpdate = 'invalid';
      if (mode === 'digest-invalid') actual['+acgpower'].sha256 = 'invalid';
      if (mode === 'cache-invalid') actual['+acgpower'].pacScript = {};
      if (mode === 'acg-extra') actual['+acgpower'].extra = true;
      const sample = events.filter(event => event === 'getAll').length;
      if (mode === 'settling' && sample === 1) delete actual['-enableQuickSwitch'];
      if (mode === 'flapping' && sample % 2) delete actual['-enableQuickSwitch'];
      if (mode === 'global-different') actual['-enableQuickSwitch'] = true;
      if (mode === 'different') actual['+acgpower'].pacUrl = 'http://127.0.0.1:9999/proxy.pac';
      if (mode === 'extra') actual.extra = true;
      const ruleList = Object.values(actual).find(profile => profile.profileType === 'RuleListProfile');
      if (mode === 'derived' || mode.startsWith('island-tamper-')) ruleList.pacScript = action === 'initialize' ? 'synthetic derived script' : derivedRule(ruleList);
      if (mode === 'bad-derived') ruleList.pacScript = 'tampered';
      if (mode === 'invalid-derived') ruleList.pacScript = {};
      if (mode === 'rule-changed') ruleList.ruleList += '\nchanged';
      if (mode === 'rule-extra') ruleList.extra = true;
      if (mode === 'builtin-override') actual['+direct'] = {profileType: 'FixedProfile'};
      if (mode === 'profile-override') actual['-builtinProfiles'] = {};
      if (mode === 'missing-profile') delete actual['+acgpower'];
      if (mode === 'unrelated') actual.unrelated = true;
      if (mode === 'read-error') return reply({error: {reason: 'synthetic'}});
      if (mode === 'read-timeout') return;
      workerOptions = actual;
      reply({result: mode === 'reordered' ? Object.fromEntries(Object.entries(actual).reverse()) : actual});
      if (mode === 'changed-after-preflight') context.location.href = 'about:blank';
    } else if (request.method === 'applyProfile') {
      assert.equal(events.includes('apply'), true, 'applyProfile preceded the second host authorization');
      assert.equal(mutated, false); mutated = true;
      assert.deepEqual(clone(request.args), [action, {update: false}], 'unexpected name or background updates');
      if (mode === 'apply-error') return reply({error: {reason: 'synthetic'}});
      if (mode === 'apply-timeout') return;
      reply({});
      if (mode === 'duplicate-reply') reply({});
    } else {
      assert.equal(request.method, 'getState');
      assert.deepEqual(clone(request.args), [['currentProfileName', 'isSystemProfile']]);
      if (mode === 'state-error') return reply({error: {reason: 'synthetic'}});
      reply({result: {currentProfileName: mode === 'state-different' ? 'other' : action,
        isSystemProfile: mode === 'system'}});
    }
  };
  if (mode === 'runtime-missing') delete context.chrome.runtime.sendMessage;
  if (mode === 'host-delayed' || mode === 'host-missing') {
    const binding = context.window.gbrowserProxyQuery;
    delete context.window.gbrowserProxyQuery;
    if (mode === 'host-delayed') context.setTimeout(() => { context.window.gbrowserProxyQuery = binding; }, 300);
  }
  const result = vm.runInNewContext('(' + source + ")('1'," + JSON.stringify(url) + ',' + JSON.stringify(config) + ',' + JSON.stringify(action) + ',' + JSON.stringify(readOnly) + ')', context);
  // Exercise real async binding waits and API deadlines with a bounded virtual
  // clock. No wall-clock sleeps, network, original worker or real profiles.
  let finished = false;
  const completed = result.then(() => { finished = true; });
  for (let round = 0; round < 100 && !finished; ++round) {
    for (let i = 0; i < 30; ++i) await Promise.resolve();
    if (finished) break;
    const next = [...timers].sort((a, b) => a[1].due - b[1].due)[0];
    assert.ok(next, 'embedded action stalled without a bounded timer');
    timers.delete(next[0]); now = next[1].due; next[1].cb();
  }
  assert.equal(finished, true, 'embedded action exceeded its synthetic wait budget');
  await completed;
  if (mode === 'proxy-replace') {
    assert.equal(observers.size, 1, 'replacement retained an old listener');
    assert.equal(intervals.size, 1, 'replacement retained an old timer');
    const watch = context.window.__gbrowserProxyWatch;
    watch.stop(); watch.stop();
    assert.equal(observers.size, 0); assert.equal(intervals.size, 0);
    assert.equal(context.window.__gbrowserProxyWatch, undefined);
  }
  if (mode === 'proxy-heartbeat' || mode === 'pac-cache-heartbeat') {
    for (const fn of intervals.values()) await fn();
    assert.equal(intervals.size, 1, 'valid observer lost its bounded lease timer');
  }
  if (mode === 'proxy-changed-later' || mode === 'pac-cache-changed-later') {
    for (const fn of [...observers]) fn({});
    for (let i = 0; i < 5; ++i) await Promise.resolve();
    assert.equal(observers.size, 0, 'revoked observer retained its listener');
    assert.equal(intervals.size, 0, 'revoked observer retained its lease timer');
  }
  if (action === 'initialize' && ['different', 'extra', 'global-different', 'flapping'].includes(mode))
    assert.equal(context.window.__gbrowserProxyInitDiagnostic,
      {different: 4, extra: 2, 'global-different': 3, flapping: 5}[mode]);
  assert.equal(events.filter(x => x === (action === 'initialize' ? 'reset' : 'applyProfile')).length, mutated ? 1 : 0);
  return events;
}
(async () => {
  originalStorageReadbacks = await require('./proxy_storage_fixture.cjs')();
  assert.ok(originalStorageReadbacks.some(sample => Object.keys(sample).length < Object.keys(config).length),
    'original delayed clear event did not reproduce the partial readback');
  const settled = await run('original-storage');
  assert.deepEqual(settled.slice(-2), ['matched', 'reload']);
  assert.ok(settled.filter(event => event === 'getAll').length > 2, 'transient readback was not observed');
  const oldPreset = clone(config); delete oldPreset['-monitorWebRequests'];
  const oldReadback = await originalResetOptions(oldPreset);
  assert.deepEqual(Object.keys(oldReadback).filter(key => !(key in oldPreset)), ['-monitorWebRequests'],
    'original default-insertion regression not reproduced');
  assert.equal(oldReadback['-monitorWebRequests'], true);
  originalResetReadback = await originalResetOptions(config);
  assert.deepEqual(originalResetReadback, config, 'complete preset differs after original reset/init');
  assert.deepEqual(await run('original-reset'), ['begin', 'reset', 'getAll', 'getAll', 'matched', 'reload'],
    'original first reset did not reach success/reload for install-choice continuation');
  for (const mode of ['normal', 'reordered', 'duplicate-reply'])
    assert.deepEqual(await run(mode), ['begin', 'reset', 'getAll', 'getAll', 'matched', 'reload'], mode);
  assert.deepEqual(await run('settling'), ['begin', 'reset', 'getAll', 'getAll', 'getAll', 'matched', 'reload']);
  for (const mode of ['different', 'extra', 'global-different', 'flapping'])
    assert.deepEqual(await run(mode), ['begin', 'reset', ...Array(6).fill('getAll'), 'different'], mode);
  for (const mode of ['throw', 'timeout', 'worker-error', 'missing-reply', 'last-error', 'changed-after-reset'])
    assert.deepEqual(await run(mode), ['begin', 'reset', 'unconfirmed'], mode);
  for (const mode of ['read-error', 'read-timeout'])
    assert.deepEqual(await run(mode), ['begin', 'reset', 'getAll', 'unconfirmed'], mode);
  for (const mode of ['wrong-page', 'wrong-id', 'runtime-missing']) assert.deepEqual(await run(mode), ['unconfirmed'], mode);
  for (const mode of ['wrong-ack', 'changed-after-begin']) assert.deepEqual(await run(mode), ['begin', 'unconfirmed'], mode);
  for (const mode of ['deny', 'document-used']) assert.deepEqual(await run(mode), ['begin', 'unconfirmed'], mode);
  for (const mode of ['derived']) assert.deepEqual(await run(mode), ['begin', 'reset', 'getAll', 'getAll', 'matched', 'reload'], mode);
  for (const mode of ['invalid-derived', 'rule-extra'])
    assert.deepEqual(await run(mode), ['begin', 'reset', ...Array(6).fill('getAll'), 'different'], mode);
  assert.deepEqual(await run('deny-result'), ['begin', 'reset', 'getAll', 'getAll', 'matched', 'unconfirmed']);
  for (const action of ['岛风go', 'direct'])
    assert.deepEqual(await run('normal', action), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'proxyGet', 'matched'], action);
  assert.deepEqual(await run('derived', '岛风go'), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'proxyGet', 'matched']);
  const acgMatched = ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'proxyGet', 'matched'];
  assert.deepEqual(await run('proxy-replace', 'acgpower'), ['begin', 'stopPrevious', ...acgMatched.slice(1)]);
  for (const mode of ['normal', 'duplicate-reply', 'unrelated'])
    assert.deepEqual(await run(mode, 'acgpower'), acgMatched, mode);
  for (const mode of ['proxy-control', 'proxy-mode', 'proxy-url', 'proxy-optional'])
    assert.deepEqual(await run(mode, 'acgpower'), [...acgMatched.slice(0, -1), 'different'], mode);
  for (const mode of ['proxy-timeout', 'proxy-throw', 'proxy-last-error', 'proxy-changed-during-read', 'proxy-document-changed'])
    assert.deepEqual(await run(mode, 'acgpower'), [...acgMatched.slice(0, -1), 'unconfirmed'], mode);
  assert.deepEqual(await run('proxy-missing', 'acgpower'), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'unconfirmed']);
  assert.deepEqual(await run('proxy-changed-later', 'acgpower'), [...acgMatched, 'changed']);
  assert.deepEqual(await run('proxy-heartbeat', 'acgpower'), [...acgMatched, 'proxyGet', 'verified']);
  for (const mode of ['different', 'missing-profile', 'builtin-override', 'profile-override'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'different'], mode);
  assert.deepEqual(await run('runtime-fields', 'acgpower'), acgMatched);
  // Original SettingsProxyImpl uses !profile.pacScript: empty cache is URL mode.
  assert.deepEqual(await run('pac-empty', 'acgpower'), acgMatched);
  assert.deepEqual(await run('pac-empty-data', 'acgpower'), [...acgMatched.slice(0, -1), 'different']);
  const cachedMatched = ['begin', 'getAll', 'pacFetch', ...acgMatched.slice(2)];
  assert.deepEqual(await run('pac-cache', 'acgpower'), cachedMatched);
  assert.deepEqual(await run('pac-cache-heartbeat', 'acgpower'), [...cachedMatched, 'proxyGet', 'verified']);
  assert.deepEqual(await run('pac-cache-changed-later', 'acgpower'), [...cachedMatched, 'changed']);
  for (const mode of ['pac-cache-data-wrong', 'pac-cache-url', 'pac-cache-both', 'pac-cache-optional'])
    assert.deepEqual(await run(mode, 'acgpower'), [...cachedMatched.slice(0, -1), 'different'], mode);
  for (const mode of ['revision-invalid', 'timestamp-invalid', 'digest-invalid', 'cache-invalid', 'acg-extra'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'different'], mode);
  assert.deepEqual(await run('runtime-fields'), ['begin', 'reset', ...Array(6).fill('getAll'), 'different'], 'initialization remains exact');
  assert.deepEqual(await run('pac-cache-stale', 'acgpower'), ['begin', 'getAll', 'pacFetch', 'different']);
  for (const mode of ['pac-fetch-http-error', 'pac-fetch-redirect', 'pac-fetch-large', 'pac-fetch-utf8', 'pac-fetch-timeout'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'pacFetch', 'unconfirmed'], mode);
  for (const mode of ['rule-changed', 'rule-extra', 'invalid-derived'])
    assert.deepEqual(await run(mode, '岛风go'), ['begin', 'getAll', 'different'], mode);
  for (const mode of ['apply-error', 'apply-timeout'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'apply', 'applyProfile', 'unconfirmed'], mode);
  for (const mode of ['state-different', 'system'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'different'], mode);
  assert.deepEqual(await run('state-error', 'direct'), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'unconfirmed']);
  assert.deepEqual(await run('deny-apply', 'acgpower'), ['begin', 'getAll', 'apply', 'unconfirmed']);
  for (const mode of ['wrong-apply-ack', 'changed-after-apply'])
    assert.deepEqual(await run(mode, 'acgpower'), ['begin', 'getAll', 'apply', 'unconfirmed'], mode);
  assert.deepEqual(await run('changed-after-preflight', 'acgpower'), ['begin', 'getAll', 'unconfirmed']);
  assert.deepEqual(await run('host-delayed', 'direct'), ['begin', 'getAll', 'apply', 'applyProfile', 'getState', 'proxyGet', 'matched']);
  assert.deepEqual(await run('host-missing', 'direct'), [], 'missing CEF binding must never reach original worker');
  for (const mode of ['read-error', 'read-timeout'])
    assert.deepEqual(await run(mode, 'direct'), ['begin', 'getAll', 'unconfirmed'], mode);
  assert.deepEqual(await run('normal', 'unknown'), []);
  const restored = ['begin', 'getAll', 'pacFetch', 'getState', 'proxyGet', 'apply', 'matched'];
  assert.deepEqual(await run('normal', 'acgpower', true), restored, 'read-only restore issued a write');
  assert.deepEqual(await run('state-different', 'acgpower', true), ['begin', 'getAll', 'pacFetch', 'getState', 'different']);
  assert.deepEqual(await run('proxy-control', 'acgpower', true), [...restored.slice(0, 5), 'different']);
  assert.deepEqual(await run('deny-apply', 'acgpower', true), [...restored.slice(0, -1), 'unconfirmed']);
  assert.deepEqual(await run('pac-cache-heartbeat', 'acgpower', true),
      ['begin', 'getAll', 'pacFetch', 'getState', 'proxyGet', 'apply', 'matched', 'proxyGet', 'verified']);
  assert.deepEqual(await run('proxy-changed-later', 'acgpower', true), [...restored, 'changed']);
  assert.deepEqual(await run('pac-fetch-http-error', 'acgpower', true), ['begin', 'getAll', 'pacFetch', 'unconfirmed']);
  assert.deepEqual(await run('normal', 'initialize', true), [], 'read-only initialization submitted reset');
  for (const action of ['direct', '岛风go']) {
    assert.deepEqual(await run('normal', action, true), ['begin', 'getAll', 'getState', 'proxyGet', 'apply', 'matched']);
    assert.deepEqual(await run('proxy-control', action), [...acgMatched.slice(0, -1), 'different']);
    assert.deepEqual(await run('proxy-changed-later', action), [...acgMatched, 'changed']);
    assert.deepEqual(await run('proxy-heartbeat', action), [...acgMatched, 'proxyGet', 'verified']);
  }
  for (const mode of ['island-tamper-port', 'island-tamper-rule', 'island-tamper-factory', 'island-tamper-global'])
    assert.deepEqual(await run(mode, '岛风go'), [...acgMatched.slice(0, -1), 'different'], mode);
  assert.deepEqual(await run('bad-derived', '岛风go'), ['begin', 'getAll', 'unconfirmed']);
  assert.deepEqual(await run('proxy-url', '岛风go'), [...acgMatched.slice(0, -1), 'different']);
  assert.deepEqual(await run('proxy-optional', '岛风go'), [...acgMatched.slice(0, -1), 'different']);
  assert.deepEqual(await run('proxy-mode', 'direct'), [...acgMatched.slice(0, -1), 'different']);
  console.log('PASS: original-worker reset/selection script, two authorization phases, exact presets/reference chains, narrow derived-field handling, reply/readback and no replay; synthetic only');
})().catch(error => { console.error(error); process.exitCode = 1; });
