// Cosmetic adapter for the official GBF loading artwork. Keep the game's
// loading overlays, hit testing, timing and resource requests entirely intact.
(function installLoadingDisplay(hidden) {
  if (window !== window.top || location.origin !== 'https://game.granbluefantasy.jp') return;
  if (!window.__starryfishLoadingDisplay) {
    const style = document.createElement('style');
    style.id = 'starryfish-loading-display';
    // Public index.css places collaboration characters in .img-loading-chara
    // under #ready, and the loading text in .img-load::after under #loading.
    // #img-loading also covers the default artwork's replacement content.
    style.textContent = `
#mobage-game-container > #ready #img-loading,
#mobage-game-container > #ready .img-loading-chara,
#mobage-game-container > #loading > .img-load { opacity: 0 !important; }
`;
    let enabled = false;
    const attach = () => {
      if (enabled && !style.isConnected && document.documentElement)
        document.documentElement.appendChild(style);
    };
    const observer = new MutationObserver(attach);
    window.__starryfishLoadingDisplay = value => {
      if (typeof value !== 'boolean') return;
      enabled = value;
      observer.disconnect();
      if (enabled) {
        // CSS covers subsequently inserted/replaced artwork without scanning
        // the game DOM. Only reattach our own style if its root is replaced.
        observer.observe(document, {childList: true, subtree: true});
        attach();
      } else style.remove();
    };
  }
  if (typeof hidden === 'boolean') window.__starryfishLoadingDisplay(hidden);
  else window.chrome?.webview?.postMessage('gbrowser:loading-display');
})
