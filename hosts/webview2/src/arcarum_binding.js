// Use the original explicit-tab interface. Never RECHECK/reset a map, change
// component files, or depend on chrome.tabs.query ordering with two games.
(function bindArcarum(target, expected) {
  if (location.href !== expected || !Number.isInteger(target) || target <= 0 || target > 2147483647) return;
  const installed = window.__gbrowserArcarumBinding;
  if (installed?.target === target && installed.expected === expected) return installed.run();
  let busy = false;
  let queuedRead = false;
  let stopEpoch = 0;
  // Capture before the original button handler, without consuming its click.
  // Storage notifications alone cannot distinguish our STOP from a user's STOP.
  document.addEventListener('click', event => {
    const button = document.getElementById('btn-stop');
    if (event.isTrusted && button && (event.target === button || button.contains(event.target))) ++stopEpoch;
  }, true);
  const run = async (coalesced = false) => {
    if (location.href !== expected) return;
    if (busy) { queuedRead = true; return; }
    busy = true;
    queuedRead = false;
    let readCompleted = false, wrote = false;
    const epoch = stopEpoch;
    const cancelled = () => location.href !== expected || stopEpoch !== epoch;
    try {
      const game = await chrome.tabs.get(target);
      if (cancelled() || !game?.url?.startsWith('https://game.granbluefantasy.jp/') ||
          !/#arcarum3(?:[/?]|$)/i.test(game.url)) return;
      const status = await chrome.runtime.sendMessage({type: 'LISTEN_STATUS'});
      if (cancelled()) return;
      readCompleted = status?.ok === true && typeof status.listening === 'boolean' &&
        (status.tabId == null ? !status.listening :
          Number.isInteger(status.tabId) && status.tabId > 0 && status.tabId <= 2147483647);
      if (!readCompleted) return;
      // The original page owns enabling/stopping. Only correct an already
      // enabled listener that selected another game; do not enable a stopped one.
      if (!status?.listening || status.tabId === target) return;
      wrote = true;
      const stopped = await chrome.runtime.sendMessage({type: 'STOP_NETWORK_LISTEN'});
      if (cancelled() || stopped?.ok !== true) return;
      const started = await chrome.runtime.sendMessage({type: 'START_NETWORK_LISTEN', tabId: target});
      // The original STOP may finish before an already submitted START. After a
      // known successful START, honor that trusted STOP once more. Never replay
      // START, compensate an unknown result, or write into a changed document.
      if (location.href === expected && stopEpoch !== epoch && started?.ok === true)
        await chrome.runtime.sendMessage({type: 'STOP_NETWORK_LISTEN'});
    } finally {
      busy = false;
      // Original START stores tabId and enabled separately. Coalesce a later
      // enabled event missed during a completed read; never replay after our
      // write, an unknown read result, or the user's STOP/document change.
      const again = !coalesced && queuedRead && readCompleted && !wrote && !cancelled();
      queuedRead = false;
      if (again) run(true).catch(() => {}); // At most one event-coalesced read.
    }
  };
  const safe = () => run().catch(() => {}); // No retry after an unknown/failed API call.
  window.__gbrowserArcarumBinding = {target, expected, run: safe};
  chrome.storage?.onChanged?.addListener((changes, area) => {
    if (area === 'local' && ('listenTabId' in changes || 'listenEnabled' in changes)) safe();
  });
  return safe();
})
