'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const header = fs.readFileSync(path.join(__dirname, '../src/login_resume.h'), 'utf8');
const source = header.match(/LR"JS\(([\s\S]*?)\)JS"/)[1];
const prompt = 'ログインするアカウントを選択してください。';
function run(text, options = {}) {
  const window = {}, top = options.frame ? {} : window;
  const node = {children: [], textContent: text,
    checkVisibility: () => !options.opacityHidden,
    getBoundingClientRect: () => ({width: options.zero ? 0 : 250, height: 20})};
  const document = {querySelectorAll: () => [node]};
  return vm.runInNewContext(source, {window, top, document,
    location: {origin: options.origin || 'https://game.granbluefantasy.jp'},
    getComputedStyle: () => ({display: options.hidden ? 'none' : 'block', visibility: 'visible'})});
}
assert.equal(run(prompt), true);
assert.equal(run(prompt, {origin: 'https://gbf.game.mbga.jp'}), true);
for (const options of [{frame: true}, {hidden: true}, {zero: true}, {opacityHidden: true},
  {origin: 'https://connect.mobage.jp'}, {origin: 'https://game.granbluefantasy.jp.evil.test'}])
  assert.equal(run(prompt, options), false);
assert.equal(run('戦闘'), false);
assert.equal(run('ログイン'), false);
console.log('PASS login channel fixed prompt visibility and origin checks; no browser/account/network');
