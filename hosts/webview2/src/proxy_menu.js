(function readProxyMenu(request,expectedUrl) {
  const helper=window.__starryfishProfiles;
  const state={request,value:null};window.__starryfishProxyMenu=state;
  window.__starryfishProxyMenuTake=serial => helper.sameDocument() && serial===state.request ? state.value : null;
  const replied=submit => new Promise((resolve,reject) => {
    let settled=false;const timer=setTimeout(()=> {if (!settled) {settled=true;reject(new Error('unavailable'));}},5000);
    const finish=(error,value) => {if (settled) return;settled=true;clearTimeout(timer);error?reject(new Error('unavailable')):resolve(value);};
    try {submit(finish);} catch (_) {finish(true);}
  });
  const call=(method,args) => replied(done => chrome.runtime.sendMessage({method,args},reply =>
    done(chrome.runtime.lastError || !reply || Object.prototype.hasOwnProperty.call(reply,'error'),reply && reply.result)));
  void (async () => {
    try {
      if (!helper.sameDocument() || location.href.split('#')[0]!==expectedUrl) throw new Error('document changed');
      const options=await call('getAll',[]);
      const names=['direct',...Object.keys(options).filter(k=>k.startsWith('+')).map(k=>k.slice(1)).filter(n=>n!=='direct')];
      if (names.length>64 || !names.every(helper.nameValid)) throw new Error('invalid names');
      const current=await call('getState',[['currentProfileName','isSystemProfile']]);
      const temporary=await call('getTempRules',[]);
      const effective=await replied(done => chrome.proxy.settings.get({incognito:false},value=> done(chrome.runtime.lastError,!value?null:value)));
      let selected='';
      if (current && current.isSystemProfile===false && names.includes(current.currentProfileName) &&
          effective && effective.levelOfControl==='controlled_by_this_extension') {
        try {
          const prepared=helper.prepare(options,current.currentProfileName,temporary);
          if (helper.matches(prepared,effective)) selected=current.currentProfileName;
        } catch (_) { /* Display no check for unverified state. */ }
      }
      if (!helper.sameDocument() || window.__starryfishProxyMenu!==state) return;
      state.value='SFPM1|'+helper.hex(selected)+'|'+names.map(helper.hex).join(',');
    } catch (_) {if (window.__starryfishProxyMenu===state) state.value='unavailable';}
  })();
})
