// Fixed original Options + ChromeStorage; in-memory API/event fixture only.
// No browser, network, user storage or modifications to the extension package.
module.exports = async function originalStorageSamples() {
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '../../hosts/webview2');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const pac = require(path.join(root, '.deps/zeroomega/js/omega_pac.min.js'));
const config = JSON.parse(read('../../app/src/proxy_presets.config'));
const areaData = {};
const listeners = [];
const pendingEvents = [];
const callback = (fn, value) => queueMicrotask(() => fn && fn(value));
const copy = value => value === undefined ? undefined : structuredClone(value);
const stable = value => Array.isArray(value) ? value.map(stable) :
  value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort().map(k => [k, stable(value[k])])) : value;
const area = {
  get(keys, cb) {
    let out;
    if (keys == null) out = copy(areaData);
    else if (Array.isArray(keys)) out = Object.fromEntries(keys.filter(k => k in areaData).map(k => [k, copy(areaData[k])]));
    else if (typeof keys === 'string') out = keys in areaData ? { [keys]: copy(areaData[keys]) } : {};
    else out = { ...copy(keys), ...Object.fromEntries(Object.keys(keys).filter(k => k in areaData).map(k => [k, copy(areaData[k])])) };
    callback(cb, out);
  },
  set(items, cb) {
    const changes = {};
    for (const [k, v] of Object.entries(items)) {
      changes[k] = { oldValue: copy(areaData[k]), newValue: copy(v) };
      areaData[k] = copy(v);
    }
    if (Object.keys(changes).length) pendingEvents.push({ changes, areaName: 'local', kind: 'set' });
    callback(cb);
  },
  remove(keys, cb) {
    const changes = {};
    for (const k of (Array.isArray(keys) ? keys : [keys])) if (k in areaData) {
      changes[k] = { oldValue: copy(areaData[k]) };
      delete areaData[k];
    }
    if (Object.keys(changes).length) pendingEvents.push({ changes, areaName: 'local', kind: 'remove' });
    callback(cb);
  },
  clear(cb) {
    const changes = {};
    for (const [k, v] of Object.entries(areaData)) changes[k] = { oldValue: copy(v) };
    for (const k of Object.keys(areaData)) delete areaData[k];
    pendingEvents.push({ changes, areaName: 'local', kind: 'clear' });
    callback(cb);
  }
};
const chrome = {
  storage: { local: area, onChanged: { addListener(fn) { listeners.push(fn); } } },
  runtime: { onConnect: { addListener() {} } }
};
const context = {
  setTimeout, clearTimeout, setImmediate, console: { log() {}, warn() {}, error(e) { throw e; } },
  chrome, OmegaPac: pac,
  require(name) {
    if (name === '../package.json') return { version: '3.5.2' };
    if (name === './formatters') return {};
    throw Error(`unexpected module ${name}`);
  }
};
vm.runInNewContext(read('.deps/zeroomega/js/omega_target.min.js'), context);
vm.runInNewContext(read('.deps/zeroomega/js/omega_target_chromium_extension.min.js'), context);
const { OmegaTarget, OmegaTargetChromium } = context;
OmegaTarget.Log.method = () => {};
// The already-running extension worker can have its previous installed/default
// options in local storage before receiving the native reset message.
Object.assign(areaData, JSON.parse(JSON.stringify(config)));
const options = new OmegaTarget.Options(new OmegaTargetChromium.Storage('local'), new OmegaTarget.Storage(),
  { method() {}, error(e) { throw e; } }, null, {});
options.applyProfile = () => OmegaTarget.Promise.resolve();

  // A service worker has already initialized Options before the native
  // initialization message can call reset(). Deliver its first-run writes.
  await options.init();
  for (const event of pendingEvents.splice(0))
    for (const listener of listeners) listener(event.changes, event.areaName);
  await new Promise(resolve => setImmediate(resolve));
  const initialOptionsKeys = Object.keys(options.getAll()).length;
  const initialStoredKeys = Object.keys(areaData).length;
  await options.reset(JSON.parse(JSON.stringify(config)));
  const expected = JSON.stringify(config);
  const before = JSON.stringify(options.getAll());
  const beforeStoreCount = Object.keys(areaData).length;
  const eventKinds = pendingEvents.map(e => `${e.kind}:${Object.keys(e.changes).length}`);
  const clearEvent = pendingEvents.find(e => e.kind === 'clear');
  if (!clearEvent || listeners.length !== 1 || before !== expected) {
    throw Error(`harness precondition failed: clear=${!!clearEvent} watchers=${listeners.length} configKeys=${Object.keys(config).length} beforeKeys=${Object.keys(options.getAll()).length} pending=${eventKinds.join(',')}`);
  }
  // Dispatch all delayed Chrome storage events in operation order, including
  // writes/removes caused synchronously by the newly-installed watcher.
  const samples = [JSON.parse(before)];
  for (let i = 0; i < 20; ++i) {
    await new Promise(resolve => setImmediate(resolve));
    if (!pendingEvents.length) break;
    const batch = pendingEvents.splice(0);
    for (const event of batch) {
      for (const listener of listeners) listener(copy(event.changes), event.areaName);
      samples.push(copy(options.getAll()));
    }
  }
  await new Promise(resolve => setImmediate(resolve));
  samples.push(copy(options.getAll()));
  return samples;
};
