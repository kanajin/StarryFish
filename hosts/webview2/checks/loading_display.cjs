'use strict';
// Isolated adapter lifecycle checks. No browser, user profile or network.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/loading_display.js'), 'utf8');
function fixture(origin = 'https://game.granbluefantasy.jp', top = true, root = true) {
  const styles = [], messages = [], observers = [];
  const document = {
    documentElement: null,
    createElement(tag) {
      assert.equal(tag, 'style');
      const node = {isConnected: false, remove() {this.isConnected = false;}};
      styles.push(node); return node;
    }
  };
  const makeRoot = () => ({appendChild(node) {node.isConnected = true;}});
  if (root) document.documentElement = makeRoot();
  class Observer {
    constructor(fn) {this.fn = fn; observers.push(this);}
    observe(target) {assert.equal(target, document); this.active = true;}
    disconnect() {this.active = false;}
  }
  const window = {chrome: {webview: {postMessage: value => messages.push(value)}}};
  window.top = top ? window : {};
  const context = vm.createContext({window, document, location: {origin}, MutationObserver: Observer});
  return {styles, messages, observers, document, makeRoot,
    run: value => vm.runInContext(source + `(${value === undefined ? '' : value});`, context),
    mutate: () => observers.filter(x => x.active).forEach(x => x.fn())};
}
for (const origin of ['http://game.granbluefantasy.jp', 'https://game.granbluefantasy.jp.evil.test',
  'https://example.test', 'https://connect.mobage.jp', 'chrome-extension://abcdefghijklmnop']) {
  const f = fixture(origin); f.run(true);
  assert.equal(f.styles.length, 0); assert.equal(f.messages.length, 0);
}
const frame = fixture(undefined, false); frame.run(true); assert.equal(frame.styles.length, 0);
console.log('PASS unrelated origins and child frames cannot install loading styles');
const f = fixture(); f.run();
assert.deepEqual(f.messages, ['gbrowser:loading-display']);
assert.equal(f.styles[0].isConnected, false);
f.run(true); assert.equal(f.styles[0].isConnected, true);
f.run(false); assert.equal(f.styles[0].isConnected, false);
f.mutate(); assert.equal(f.styles[0].isConnected, false);
f.run(); assert.equal(f.styles[0].isConnected, false);
assert.equal(f.styles.length, 1); assert.equal(f.observers.length, 1);
console.log('PASS document requests current host preference; repeated install and disable do not retain hidden state');
const early = fixture(undefined, true, false); early.run(true);
assert.equal(early.styles[0].isConnected, false);
early.document.documentElement = early.makeRoot(); early.mutate();
assert.equal(early.styles[0].isConnected, true);
early.styles[0].remove(); early.mutate(); assert.equal(early.styles[0].isConnected, true);
early.run(false); early.document.documentElement = early.makeRoot(); early.mutate();
assert.equal(early.styles[0].isConnected, false);
console.log('PASS pre-root install and replaced root recover only while enabled');
const next = fixture(); next.run(); next.run(false);
assert.equal(next.styles[0].isConnected, false);
console.log('PASS new document follows current disabled preference after earlier enabled document');
// The adapter only adds a stylesheet; the fixture deliberately exposes no
// game-element removal, attributes, storage, navigation, or network APIs.
for (const rule of f.styles[0].textContent.matchAll(/([^{}]+)\{([^{}]+)\}/g)) {
  for (const selector of rule[1].trim().split(','))
    assert.match(selector.trim(), /^#mobage-game-container > #(ready|loading) [^,]+$/);
  assert.equal(rule[2].trim(), 'opacity: 0 !important;');
}
console.log('PASS styles affect only known loading artwork opacity, retaining overlay hit testing and layout');
console.log('Loading display synthetic checks passed; real GBF rendering NOT_EVALUATED');
