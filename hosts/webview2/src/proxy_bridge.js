// A private, in-document queue read through ExecuteScript. Extension documents
// need not expose the WebView2 web message bridge. No webpage host object, no
// storage writes, and no native authority carried in mutable disk metadata.
(function installProxyBridge(operation, expectedUrl) {
  const matches = () => location.href.split('#')[0] === expectedUrl && chrome.runtime &&
    `chrome-extension://${chrome.runtime.id}/options.html` === expectedUrl;
  if (!matches()) throw new Error('controller mismatch');
  const queued = [], callbacks = new Map();
  let serial = 0;
  window.gbrowserProxyQuery = options => {
    if (!matches() || typeof options.request !== 'string' ||
        !options.request.startsWith(`${operation}:`) || options.request.length > 40 || callbacks.size >= 4) {
      options.onFailure(); return;
    }
    const number = ++serial;
    const timer = setTimeout(() => {
      const callback = callbacks.get(number);
      if (callback) { callbacks.delete(number); callback.onFailure(); }
    }, 5000);
    callbacks.set(number, {...options, timer});
    queued.push(`${number}|${options.request}`);
  };
  window.__gbrowserProxyTake = () => matches() ? (queued.shift() || null) : 'OFF_DOCUMENT';
  window.__gbrowserProxyReply = (number, reply) => {
    const callback = callbacks.get(number);
    if (!callback) return;
    callbacks.delete(number); clearTimeout(callback.timer);
    if (!matches() || reply === 'denied') callback.onFailure();
    else callback.onSuccess(reply);
  };
})
