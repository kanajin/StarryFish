// Read-only DOM geometry. Native input owns every click; no game API calls.
(function characterShortcut(expected, mask, phase, locked = '') {
  if (window !== window.top || location.origin !== 'https://game.granbluefantasy.jp' || location.href !== expected ||
      !Number.isInteger(mask) || mask <= 0 || mask >= (1 << 23) || ![0,1,2,3].includes(phase) ||
      !document.hasFocus() || document.visibilityState !== 'visible') return {action:0,reason:1};
  let active = document.activeElement;
  while (active?.shadowRoot?.activeElement) active = active.shadowRoot.activeElement;
  if (active && (active.isContentEditable || active.closest('input,textarea,select,iframe,[role="textbox"],[contenteditable]:not([contenteditable="false"])'))) return {action:0,reason:2};
  const has = i => (mask & (1 << i)) !== 0;
  if (has(4)) return {action:5};
  if (!/^#raid(?:_multi)?\//.test(location.hash)) return {action:0,reason:3};
  if (phase === 3) return {action:4};
  if (phase === 2 && !has(17) && !/^(?:[0-3]|5|7|15)$/.test(locked)) return {action:4};
  // Back retains display-off when display-on is added again. Computed CSS,
  // viewport bounds and hit testing determine visibility, not stale class names.
  const visible = element => {
    if (!element || !element.isConnected) return false;
    for (let n=element; n; n=n.parentElement) {
      const s=getComputedStyle(n);
      if (s.display==='none' || s.visibility!=='visible' || Number(s.opacity)===0) return false;
    }
    const r=element.getBoundingClientRect(); return r.width>1 && r.height>1;
  };
  const all = (selector, root=document) => Array.from(root.querySelectorAll(selector)).filter(visible);
  const unique = (selector, root=document) => {const list=all(selector,root);return list.length===1?list[0]:null;};
  const itemPopup=unique('.pop-raid-item');
  const fcPopup=has(17)?unique('.pop-confirm-fatal-chain'):null;
  const panels=all('.prt-command-chara');
  if (panels.length>1) return {action:0,reason:4};
  const panel=panels[0];
  if (phase===2 && /^[0-3]$/.test(locked))
    return panel && panel.getAttribute('pos')===String(Number(locked)+1) &&
      all('.prt-ability-list [data-ability-index]',panel).length?{action:4}:{action:1};
  const summon=unique('.prt-command-summon.summon-show .prt-summon-list.opened');
  if(phase===2 && locked==='15') return itemPopup && unique('.prt-select-item',itemPopup)?{action:4}:{action:1};
  if(phase===2 && locked==='7') return summon?{action:4}:{action:1};
  if(phase===2 && locked==='5') return !panel && !summon && !unique('.btn-command-back')?{action:4}:{action:1};
  const modal=all('.pop-show,[role="dialog"],[aria-modal="true"],#ready,#loading');
  // Only the identified item picker is allowed; confirmations/target selection remain manual.
  const permitted=itemPopup || fcPopup;
  if (modal.some(n => !permitted || !(n===permitted || n.contains(permitted) || permitted.contains(n)))) return {action:0,reason:5};
  const picker = itemPopup && unique('.prt-select-item',itemPopup);
  const scope=fcPopup ? 16 : picker ? 4 : itemPopup ? 0 : panel ? 2 : summon ? 8 : 1;
  if (!scope) return {action:0,reason:6};
  let selected=-1;
  for (let i=0;i<23;i++) {
    const allowed=i<4?11:i>=8&&i<12?2:(i>=12&&i<15)||i>=20?4:i===17?27:11;
    if (has(i) && (allowed & scope)) {if(selected!==-1)return {action:0,reason:7};selected=i;}
  }
  if (selected<0) return {action:0,reason:8};
  const numeric=(selected>=8&&selected<15)||selected>=20;
  const cancel=selected===16?unique('.btn-attack-cancel.display-on'):null;
  const context=String(selected)+ (numeric ? ':'+scope+':'+(panel?.getAttribute('pos') || '') : selected===16?(cancel?':cancel':':attack'):'');
  if (locked && locked!==context) return {action:0,reason:9};
  const back=unique('.btn-command-back');
  let target=null, action=3;
  if (selected<4) {
    if (panel && panel.getAttribute('pos')===String(selected+1)) return {action:4};
    if (back) {
      if(phase===1)return {action:1};
      if(!panel&&!summon)return {action:0,reason:10};
      target=back;action=2;
    } else if(panel||summon)return {action:1};
    else target=unique('.btn-command-character[pos="'+selected+'"]');
  }
  else if (selected>=8&&selected<12) target=unique('.prt-ability-list [data-ability-index="'+(selected-7)+'"]',panel);
  else if (selected>=12&&selected<15) {
    const selectors=['.item-small','.item-large','.item-potion'];
    target=unique(selectors[selected-12],selected===14?itemPopup:picker);
  } else if(selected>=20) target=unique('.prt-event-item .btn-event-item[item-id="'+(selected-19)+'"]',itemPopup);
  else if(selected===17 && fcPopup) {target=unique('.btn-usual-ok',fcPopup);action=6;}
  else if(selected===17 && phase===2) return {action:1};
  else if(selected===16 && cancel) target=cancel;
  else if (selected===5) target=back;
  else if (selected===7 && summon) return {action:4};
  else if (selected===19) {
    // Visual and overlay copies coexist; choose the actual hit-tested control.
    const menus=all('.btn-raid-menu.menu').filter(n=>{
      const r=n.getBoundingClientRect(),h=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);
      return h && (h===n || n.contains(h));
    });
    if(menus.length===1)target=menus[0];
  }
  else if (back) {
    if (phase===1) return {action:1};
    if (!panel && !summon) return {action:0,reason:11};
    target=back;action=2;
  } else {
    if (panel || summon) return {action:1};
    const selector={6:'#js-btn-quick-summon.qs-ready',7:'.prt-summon-list .prt-list-top',
      15:'.btn-temporary',16:'.btn-attack-start',17:'#prt-advanced-command .btn-cb-gauge',18:'.btn-lock'}[selected];
    if (selector) target=unique(selector);
  }
  if (!target) return phase===1?{action:1}:{action:0,reason:100};
  for (let n=target; n && n!==document.body; n=n.parentElement)
    if (n.matches('[disabled],[aria-disabled="true"],.disable,.disabled,.unavailable,.is-sealed,.dead,.empty')) return {action:0,reason:12};
  const r=target.getBoundingClientRect(),x=r.left+r.width/2,y=r.top+r.height/2;
  if(x<0||y<0||x>=innerWidth||y>=innerHeight)return {action:0,reason:13};
  const hit=document.elementFromPoint(x,y);
  if(!hit || !(hit===target || target.contains(hit))) return {action:0,reason:14};
  return {action,context,x,y,scale:devicePixelRatio,width:innerWidth,height:innerHeight};
})
