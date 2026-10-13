// Only the auxiliary game hides the fixed bookmark UI. Role is native-owned,
// document-local, and never persisted in shared localStorage or plugin files.
(function configureGameRole(secondary, hideMenu = false) {
  if (window !== window.top || !['https://game.granbluefantasy.jp', 'https://gbf.game.mbga.jp'].includes(location.origin)) return;
  let style = document.getElementById('gbrowser-secondary-bookmark-style');
  let menu = document.getElementById('gbrowser-secondary-menu-style');
  if (!secondary || !hideMenu) menu?.remove();
  else if (!menu) {
    menu = document.createElement('style'); menu.id = 'gbrowser-secondary-menu-style';
    // Keep measurable geometry and original shared submenu state untouched.
    menu.textContent = '#submenu{visibility:hidden!important;pointer-events:none!important}';
    document.documentElement.appendChild(menu);
  }
  if (!secondary) { style?.remove(); return; }
  if (!style) {
    style = document.createElement('style'); style.id = 'gbrowser-secondary-bookmark-style';
    style.textContent = '#gbf-bookmark-lacia,#show-setting-bookmark,#gbf-bookmark-setting{display:none!important}';
    document.documentElement.appendChild(style);
  }
})
