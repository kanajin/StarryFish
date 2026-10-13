// Product-owned display/input adapter. No game actions, account state or
// remote code; only the known public container's numeric left inset leaves it.
(function installGameUi() {
  if (window !== window.top || location.origin !== 'https://game.granbluefantasy.jp') return;
  if (window.__gbrowserGameUi) { window.__gbrowserGameUi(); return; }
  const send = value => window.chrome?.webview?.postMessage(value);
  let last = -1;
  let lastMenu = -1;
  const fitBookmarkEditor = () => {
    const panel = document.getElementById('gbf-bookmark-setting');
    if (!panel || !panel.offsetWidth) return; // Original toggle owns visibility.
    const computed = getComputedStyle(panel);
    const rect = panel.getBoundingClientRect();
    // offsetWidth is integer-rounded and would make fractional fitted widths
    // oscillate under the mutation observer. Use the actual CSS border box.
    const extra = computed.boxSizing === 'border-box' ? 0 :
      ['paddingLeft', 'paddingRight', 'borderLeftWidth', 'borderRightWidth']
        .reduce((sum, name) => sum + (parseFloat(computed[name]) || 0), 0);
    const scale = rect.width / (parseFloat(computed.width) + extra);
    if (!Number.isFinite(scale) || scale <= 0) return;
    // The host may clip the Moba strip. Keep the editor after that strip even
    // when it is shown; CSS z-index cannot escape a native WebView boundary.
    // The original script applies deviceRatio via CSS zoom, independently of
    // browser zoom, so convert viewport coordinates back into panel CSS units.
    const left = Math.max(0, last) + 8;
    const width = Math.min(280, Math.max(1, (window.innerWidth - left - 8) / scale - extra));
    const currentLeft = parseFloat(computed.left);
    if (!Number.isFinite(currentLeft)) return;
    const targetLeft = currentLeft + (left - rect.left) / scale;
    const set = (name, value) => {
      const text = (Math.round(value * 1000) / 1000) + 'px';
      if (panel.style.getPropertyValue(name) !== text) panel.style.setProperty(name, text);
    };
    set('left', targetLeft); set('width', width);
  };
  const measure = () => {
    const node = document.getElementById('mobage-game-container');
    if (node && node.dataset.showMenubarWidth === '64') {
      const left = Math.round(node.getBoundingClientRect().left);
      if (left >= 0 && left <= 256 && left !== last) {
        last = left; send('gbrowser:moba:' + left);
      }
    }
    // Refit on editor open, late script zoom, and viewport resize even if the
    // Moba measurement itself has not changed. Idempotent styles avoid loops.
    fitBookmarkEditor();
    const menu = document.getElementById('submenu');
    if (menu && menu.querySelector('.btn-sub-setting') && menu.querySelector('.btn-sub-help') &&
        menu.querySelector('.btn-sub-chat')) {
      const rect = menu.getBoundingClientRect();
      const cut = Math.ceil(window.innerWidth - rect.left);
      if (rect.width > 0 && rect.left >= window.innerWidth / 2 && cut > 0 && cut <= 512 && cut !== lastMenu) {
        lastMenu = cut; send('gbrowser:submenu:' + cut);
      }
    }
  };
  window.__gbrowserGameUi = () => { lastMenu = -1; measure(); };
  window.addEventListener('resize', () => { lastMenu = -1; measure(); });
  new MutationObserver(measure).observe(document.documentElement, {childList: true, subtree: true,
    attributes: true, attributeFilter: ['style', 'class', 'data-show-menubar-width']});
  measure();
})
