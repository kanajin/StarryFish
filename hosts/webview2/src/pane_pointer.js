// One input path for games and the two fixed helper documents. The native
// handler independently checks the actual source/controller generation.
(function installPanePointer(expected = null) {
  if (window !== window.top) return;
  const matches = () => expected === null ?
    ['https://game.granbluefantasy.jp', 'https://gbf.game.mbga.jp'].includes(location.origin) :
    location.href.split('#')[0] === expected;
  if (!matches() || window.__gbrowserPanePointer) return;
  window.__gbrowserPanePointer = true;
  const side = event => matches() && event.isTrusted && (event.button === 3 || event.button === 4);
  document.addEventListener('mousedown', event => {
    if (expected === null && matches() && event.isTrusted && event.button === 0)
      window.chrome?.webview?.postMessage('gbrowser:active-game');
    if (side(event)) event.preventDefault();
  }, true);
  document.addEventListener('auxclick', event => { if (side(event)) event.preventDefault(); }, true);
  document.addEventListener('mouseup', event => {
    if (!side(event)) return;
    event.preventDefault();
    window.chrome?.webview?.postMessage(event.button === 3 ? 'gbrowser:back' : 'gbrowser:forward');
  }, true);
})
