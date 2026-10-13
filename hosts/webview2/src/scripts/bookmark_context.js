// Adapt only the fixed bookmark UI; leave its original mouseup editor toggle
// and pinned script untouched. The editor is a sibling of the bookmark root:
// right-clicking its inputs must retain the normal copy/paste menu.
(() => {
  if (window !== window.top) return;
  document.addEventListener('auxclick', event => {
    if (!event.isTrusted || event.button !== 1 || !(event.target instanceof Element)) return;
    const link = event.target.closest('#gbf-bookmark-lacia a.bookmark-item-lacia[href]');
    if (!link) return; // reload/back/forward/none are actions, not URL bookmarks.
    let target;
    try { target = new URL(link.href, location.href); } catch (_) { return; }
    if (!['http:', 'https:'].includes(target.protocol) || target.username || target.password ||
        target.href.length > 8192) return;
    event.preventDefault();
    // A fresh name avoids reusing an earlier bookmark tab. It classifies the
    // popup; native gesture/frame/source/network checks still admit it.
    window.open(target.href, 'gbrowser-bookmark-middle-' + crypto.randomUUID());
  }, true);
  document.addEventListener('contextmenu', event => {
    const target = event.target;
    if (!(target instanceof Element)) return;
    if (target.closest('input,textarea,select,[contenteditable]:not([contenteditable="false"])')) return;
    if (target.closest('#gbf-bookmark-lacia,#show-setting-bookmark,#gbf-bookmark-setting')) event.preventDefault();
  }, true);
  document.addEventListener('mouseup', event => {
    if (event.button !== 2 || !(event.target instanceof Element)) return;
    const hotspot = event.target.closest('#show-setting-bookmark');
    // This separate ten-pixel settings hotspot only has an original click
    // listener. Bookmarks already have mouseup; never toggle those twice.
    if (hotspot) hotspot.click();
  }, true);
})();
