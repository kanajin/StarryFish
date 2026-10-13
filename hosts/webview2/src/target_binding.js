// Read only public tab URLs/IDs from the loaded original extension context.
// First bind requires a unique game tab. The permanent native game controller
// then retains that verified runtime ID; extra tabs never replace its target.
// Never read page contents, login or extension storage. The isolated runtime
// validated query({}); apply the exact game origin locally instead of relying
// on the embedded browser's URL-filter implementation.
(function bindGameTarget(generation, expectedUrl, knownId = 0) {
  const matches = () => location.href.split('#')[0] === expectedUrl &&
    `chrome-extension://${chrome.runtime.id}/options.html` === expectedUrl;
  if (!matches()) throw new Error('controller mismatch');
  window.__gbrowserGameTarget = null;
  window.__gbrowserGameTargetGeneration = generation;
  chrome.tabs.query({}, tabs => {
    if (!matches() || window.__gbrowserGameTargetGeneration !== generation) return;
    let id = 0, reason = 0, sample = 0;
    if (chrome.runtime.lastError) reason = 1;
    else if (!Array.isArray(tabs) || tabs.length > 32) reason = 2;
    else {
      const game = tabs.filter(tab => tab && typeof tab.url === 'string' &&
        tab.url.startsWith('https://game.granbluefantasy.jp/'));
      const candidates = knownId > 0 ? game.filter(tab => tab.id === knownId) : game;
      if (candidates.length !== 1) reason = 3;
      else {
        const candidate = candidates[0].id;
        if (Number.isInteger(candidate) && candidate >= -2147483648 && candidate <= 2147483647) sample = candidate;
        // Chrome tab IDs are signed 32-bit integers, not nine-digit numbers.
        if (!Number.isInteger(candidate) || candidate <= 0 || candidate > 2147483647) reason = 4;
        else id = candidate;
      }
    }
    // Bounded numeric diagnostics distinguish query, ambiguity and ID failures;
    // no URL, query string or page data leaves the controller context.
    window.__gbrowserGameTarget = `${generation}:${id}:${reason}:${sample}`;
  });
})
