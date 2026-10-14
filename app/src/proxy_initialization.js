// Executed only in an explicitly confirmed, pinned original ZeroOmega options
// document. Use its replied worker API; no storage edits, silent retries or
// original-package changes. A lost reply may follow a partially completed reset.
(async function runProxyAction(operation, expectedUrl, expected, action, readOnly = false, customHex = '', rememberStartup = false) {
  window.__gbrowserProxyActionOperation=operation;
  const custom=action==='custom';
  const helper=custom?window.__starryfishProfiles:null;
  let prepared=null;
  // Fixed categories only. An older action cannot overwrite a new action's
  // record; no profile name, PAC, URL or API error leaves this document.
  const diagnostic={operation,category:0};
  if (action!=='initialize') window.__gbrowserProxySelectionDiagnostic=diagnostic;
  const mark=category => {diagnostic.category=category;};
  const host = (verb) => new Promise((resolve, reject) => {
    window.gbrowserProxyQuery({request: operation + ':' + verb, persistent: false,
      onSuccess: resolve, onFailure: () => reject(new Error('host denied'))});
  });
  const sameDocument = () => window.__gbrowserProxyActionOperation===operation && location.href.split('#')[0] === expectedUrl &&
    typeof chrome !== 'undefined' && chrome.runtime &&
    'chrome-extension://' + chrome.runtime.id + '/options.html' === expectedUrl &&
    typeof chrome.runtime.sendMessage === 'function';
  let budget = 100000;
  const equal = (a, b, depth = 0) => {
    if (--budget < 0 || depth > 64) return false;
    if (a === b) return true;
    if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object' ||
        Array.isArray(a) !== Array.isArray(b)) return false;
    const keys = Object.keys(a), other = Object.keys(b);
    return keys.length === other.length && keys.every(key =>
      Object.prototype.hasOwnProperty.call(b, key) && equal(a[key], b[key], depth + 1));
  };
  const equalProfile = (reference, actual, runtimeFields = false) => {
    if (!actual || typeof actual !== 'object') return false;
    if (runtimeFields) {
      // Original Profiles.updateRevision and Options.updateProfile maintain
      // these fields. They are not proxy destinations or reference rules.
      reference = {...reference}; actual = {...actual};
      if (Object.prototype.hasOwnProperty.call(actual, 'revision') &&
          (typeof actual.revision !== 'string' || !/^[0-9a-f]{1,16}$/.test(actual.revision))) return false;
      delete reference.revision; delete actual.revision;
      if (['PacProfile', 'RuleListProfile'].includes(reference.profileType)) {
        if (Object.prototype.hasOwnProperty.call(actual, 'lastUpdate') && actual.lastUpdate !== null &&
            (typeof actual.lastUpdate !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(actual.lastUpdate) ||
             !Number.isFinite(Date.parse(actual.lastUpdate)))) return false;
        if (Object.prototype.hasOwnProperty.call(actual, 'sha256') &&
            (typeof actual.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(actual.sha256))) return false;
        delete reference.lastUpdate; delete actual.lastUpdate;
        delete reference.sha256; delete actual.sha256;
        if (!Object.prototype.hasOwnProperty.call(reference, 'pacScript') &&
            Object.prototype.hasOwnProperty.call(actual, 'pacScript')) {
          if (typeof actual.pacScript !== 'string' || actual.pacScript.length > 2 * 1024 * 1024) return false;
          delete actual.pacScript;
        }
      }
    }
    // The original _setOptions generates this exact RuleListProfile field.
    // Compare every supplied field; this exemption is not a PAC execution check.
    if (reference.profileType === 'RuleListProfile' &&
        !Object.prototype.hasOwnProperty.call(reference, 'pacScript') &&
        Object.prototype.hasOwnProperty.call(actual, 'pacScript')) {
      if (typeof actual.pacScript !== 'string' || actual.pacScript.length > 2 * 1024 * 1024) return false;
      actual = {...actual}; delete actual.pacScript;
    }
    return equal(reference, actual);
  };
  const equalOptions = actual => {
    // Static categories only; never put actual keys, settings or API errors in
    // diagnostics. A fresh traversal budget belongs to each readback sample.
    budget = 100000;
    let difference = 0;
    if (!actual || typeof actual !== 'object' || Array.isArray(actual)) difference = 1;
    else if (Object.keys(actual).length !== Object.keys(expected).length ||
        Object.keys(expected).some(key => !Object.prototype.hasOwnProperty.call(actual, key))) difference = 2;
    else for (const key of Object.keys(expected)) {
      if (!(key.startsWith('+') ? equalProfile(expected[key], actual[key]) : equal(expected[key], actual[key]))) {
        difference = key.startsWith('+') ? 4 : 3; break;
      }
    }
    window.__gbrowserProxyInitDiagnostic = difference;
    return difference === 0;
  };
  const baselineMatches = actual => {
    if (!actual || typeof actual !== 'object' || Array.isArray(actual) ||
        Object.prototype.hasOwnProperty.call(actual, '+direct') ||
        Object.prototype.hasOwnProperty.call(actual, '-builtinProfiles')) return false;
    const visited = new Set();
    const check = name => {
      if (name === 'direct') return true;  // Original built-in, with overrides rejected above.
      const key = '+' + name, reference = expected[key];
      if (!reference || visited.has(key)) return false;
      visited.add(key);
      if (!Object.prototype.hasOwnProperty.call(actual, key) || !equalProfile(reference, actual[key], true)) return false;
      const refs = [reference.defaultProfileName, reference.matchProfileName,
        ...(reference.rules || []).map(rule => rule.profileName)].filter(Boolean);
      return refs.every(check);
    };
    return check(action);
  };
  const replied = submit => new Promise((resolve, reject) => {
    let settled = false;
    const timeout = setTimeout(() => {
      if (!settled) { settled = true; reject(new Error('reply timed out')); }
    }, 60000);
    const finish = (error, result) => {
      if (settled) return;
      settled = true; clearTimeout(timeout);
      error ? reject(new Error('API reply unavailable')) : resolve(result);
    };
    try {
      submit(finish);
    } catch (_) { finish(true); }
  });
  const call = (method, args) => replied(finish => {
    chrome.runtime.sendMessage({method, args}, response => {
      // lastError is valid only during this callback. Do not copy its text.
      finish(chrome.runtime.lastError || !response || typeof response !== 'object' ||
        Object.prototype.hasOwnProperty.call(response, 'error'), response && response.result);
    });
  });
  const prepareCustom = async options => helper.prepare(options,action,await call('getTempRules',[]));
  const proxyConfiguration = () => replied(finish => {
    chrome.proxy.settings.get({incognito: false}, result => {
      finish(chrome.runtime.lastError || !result || typeof result !== 'object', result);
    });
  });
  // The original Chrome adapter uses cached PAC data instead of the URL when
  // nonempty (its !profile.pacScript branch uses the URL for an empty cache).
  // Trust data only after comparing it with the fixed local PAC;
  // do not execute it, overwrite the cache or request remote rule updates.
  const pacAscii = text => text.replace(/[\u0080-\uffff]/g,
    c => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'));
  const readLocalPac = async () => {
    const abort = new AbortController();
    const timeout = setTimeout(() => abort.abort(), 60000);
    let reader;
    try {
      const response = await fetch(expected['+acgpower'].pacUrl,
        {redirect: 'error', credentials: 'omit', cache: 'no-store', signal: abort.signal});
      if (!response.ok || response.url !== expected['+acgpower'].pacUrl || !response.body)
        throw new Error('local PAC unavailable');
      reader = response.body.getReader();
      const decoder = new TextDecoder('utf-8', {fatal: true});
      let bytes = 0, text = '';
      while (true) {
        const part = await reader.read();
        if (part.done) break;
        bytes += part.value.byteLength;
        if (bytes > 2 * 1024 * 1024) throw new Error('local PAC too large');
        text += decoder.decode(part.value, {stream: true});
      }
      return text + decoder.decode();
    } finally {
      clearTimeout(timeout);
      abort.abort();
      if (reader) { try { await reader.cancel(); } catch (_) {} }
    }
  };
  let approvedPacData = null;
  const islandPac = actual => {
    if (typeof OmegaPac === 'undefined') throw new Error('PAC generator unavailable');
    const options = {}, visited = new Set();
    const copy = name => {
      if (name === 'direct' || visited.has(name)) return;
      visited.add(name);
      const reference = expected['+' + name], profile = actual['+' + name];
      options['+' + name] = profile;
      if (profile.profileType === 'RuleListProfile' && profile.pacScript) {
        const temp = OmegaPac.Profiles.create('__tempZeroRuleListPac', profile.profileType);
        for (const key of ['defaultProfileName', 'format', 'matchProfileName', 'ruleList']) temp[key] = profile[key];
        temp.isTempPacProfile = true;
        const generated = OmegaPac.PacGenerator.script({['+' + temp.name]: temp}, temp.name, {profileNotFound: () => 'ignore'});
        if (profile.pacScript !== OmegaPac.PacGenerator.ascii(generated.print_to_string({beautify: true, comments: true})))
          throw new Error('derived rule PAC differs');
      }
      [reference.defaultProfileName, reference.matchProfileName, ...(reference.rules || []).map(rule => rule.profileName)]
        .filter(Boolean).forEach(copy);
    };
    copy(action);
    const profile = options['+' + action];
    const ast = OmegaPac.PacGenerator.compress(OmegaPac.PacGenerator.script(options, profile,
      {profileNotFound: () => { throw new Error('missing PAC profile'); }}));
    const name = OmegaPac.PacGenerator.ascii(JSON.stringify(profile.name)).replace(/\*/g, '\\u002a').replace(/\\/g, '\\u002f');
    return '/*OmegaProfile*' + name + '*' + profile.revision + '*/' + OmegaPac.PacGenerator.ascii(ast.print_to_string());
  };
  // OmegaPac keeps module-wide minifier character frequencies. Normalize only
  // its fixed factory's local bindings. Anchoring and backreferences preserve
  // every reference; the remaining rules and endpoints compare byte for byte.
  const canonicalIslandPac = text => {
    const factory = /^(\/\*OmegaProfile\*[^\r\n]*?\*\/var FindProxyForURL=)function\(([a-zA-Z_$]),([a-zA-Z_$])\)\{return function\(([a-zA-Z_$]),([a-zA-Z_$])\)\{"use strict";var ([a-zA-Z_$])=\2,([a-zA-Z_$])=\4\.substr\(0,\4\.indexOf\(":"\)\);do\{if\(!\3\[\6\]\)return \6;\6=\3\[\6\];if\("function"===typeof \6\)\6=\6\(\4,\5,\7\)\}while\("string"!==typeof \6\|\|43===\6\.charCodeAt\(0\)\);return \6\}\}\(/;
    const match = factory.exec(text);
    if (!match || new Set(match.slice(2)).size !== 6) return null;
    return match[1] + '<validated Omega factory>(' + text.slice(match[0].length);
  };
  const matchesProxy = effective => {
    if (effective.levelOfControl !== 'controlled_by_this_extension' || !effective.value) return false;
    if (custom) return helper.matches(prepared,effective);
    if (action === 'direct') return effective.value.mode === 'direct' &&
      !Object.prototype.hasOwnProperty.call(effective.value, 'pacScript') && !Object.prototype.hasOwnProperty.call(effective.value, 'rules');
    return effective.value.mode === 'pac_script' && effective.value.pacScript &&
      effective.value.pacScript.mandatory === true &&
      (approvedPacData === null ?
        action === 'acgpower' && effective.value.pacScript.url === expected['+acgpower'].pacUrl &&
          !Object.prototype.hasOwnProperty.call(effective.value.pacScript, 'data') :
        (effective.value.pacScript.data === approvedPacData || (action === '岛风go' &&
          typeof effective.value.pacScript.data === 'string' &&
          canonicalIslandPac(approvedPacData) !== null &&
          canonicalIslandPac(effective.value.pacScript.data) === canonicalIslandPac(approvedPacData))) &&
          !Object.prototype.hasOwnProperty.call(effective.value.pacScript, 'url'));
  };
  let observer = null, watch = null, observing = false, changed = false, heartbeat = 0, verifying = false, confirmed = false;
  const waitForHost = async () => {
    const deadline = Date.now() + 2000;
    for (let tries = 0; tries < 20; ++tries) {
      if (typeof window.gbrowserProxyQuery === 'function') return;
      if (Date.now() >= deadline || location.href.split('#')[0] !== expectedUrl) break;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    // Native authorization also has a five-second pre-write deadline. Missing
    // CEF bindings never grant apply or leave a selection blocked for a minute.
    throw new Error('host unavailable');
  };
  const stopObserver = () => {
    if (heartbeat) { clearInterval(heartbeat); heartbeat = 0; }
    if (observer) {
      try { chrome.proxy.settings.onChange.removeListener(observer); } catch (_) { /* Native close/navigation also revokes. */ }
    }
    if (watch && window.__gbrowserProxyWatch === watch) delete window.__gbrowserProxyWatch;
    observer = null;
  };
  try {
    if (!['initialize', 'direct', 'acgpower', '岛风go', 'custom'].includes(action) ||
        typeof readOnly !== 'boolean' || typeof rememberStartup !== 'boolean' ||
        (rememberStartup && readOnly) || (readOnly && action === 'initialize')) return;
    if (custom) {if (!helper || !helper.sameDocument()) return;action=helper.decode(customHex);}
    await waitForHost();
    // A document signal starts only the read-only worker preflight. Runtime
    // bindings/worker replies still have to exist before native apply authority.
    if (!sameDocument()) throw new Error('controller unavailable');
    if (await host('begin') !== 'authorized' || !sameDocument()) throw new Error('controller changed');
    // Stop the complete previous observation, including its interval. Hidden
    // tabs may throttle timers, so waiting for the old operation to expire
    // would accumulate observers after repeated selections.
    if (window.__gbrowserProxyWatch) window.__gbrowserProxyWatch.stop();
    if (!custom && action === 'initialize') {
      // Authorization precedes the first mutation; never issue reset twice.
      if (window.__gbrowserProxyInitializationStarted) throw new Error('already submitted');
      window.__gbrowserProxyInitializationStarted = true;
      window.__gbrowserProxyInitDiagnostic = 10; // Reset has not returned.
      await call('reset', [expected]);
      if (!sameDocument()) throw new Error('document changed');
      // ChromeStorage delivers onChanged separately from reset's reply. Read
      // until two consecutive exact samples agree, with a bounded window.
      // Never repeat reset, applyProfile, or any other write to obtain a match.
      const started = Date.now(); let consecutive = 0, matched = false;
      for (let sample = 0; sample < 6; ++sample) {
        if (!sameDocument()) throw new Error('document changed');
        window.__gbrowserProxyInitDiagnostic = 11; // Readback has not returned.
        const actual = await call('getAll', []);
        if (!sameDocument()) throw new Error('document changed');
        consecutive = equalOptions(actual) ? consecutive + 1 : 0;
        if (Date.now() - started > 2000) { window.__gbrowserProxyInitDiagnostic = 6; break; }
        if (consecutive === 2) { matched = true; break; }
        if (sample < 5) await new Promise(resolve => setTimeout(resolve, 150));
      }
      if (!matched && window.__gbrowserProxyInitDiagnostic === 0) window.__gbrowserProxyInitDiagnostic = 5;
      await host(matched ? 'matched' : 'different');
      // Reset changes the worker while the options form retains its old copy.
      // Refresh only after native acknowledgement; never retry a write here.
      if (matched && sameDocument()) {
        try { location.reload(); } catch (_) { /* The user can refresh normally. */ }
      }
    } else {
      mark(10);
      const actual = await call('getAll', []);  // The original worker's readiness reply; no separate probe/write.
      if (custom) {
        mark(11);
        for (let i=0;i<20 && typeof OmegaPac==='undefined' && sameDocument();++i)
          await new Promise(resolve=>setTimeout(resolve,100));
        prepared=await prepareCustom(actual);
      } else if (!baselineMatches(actual)) { mark(20); await host('different'); return; }
      if (!custom && action === 'acgpower' && actual['+acgpower'].pacScript) {
        mark(12);
        const local = await readLocalPac();
        if (actual['+acgpower'].pacScript !== local) { mark(21); await host('different'); return; }
        approvedPacData = pacAscii(local);
      } else if (!custom && readOnly && action === 'acgpower') {
        // A saved URL configuration alone does not mean the local accelerator
        // is running. Startup stays at preparation when its fixed PAC service
        // is unavailable; no fallback to direct and no configuration write.
        mark(12); await readLocalPac();
      }
      if (!custom && action === '岛风go') {
        for (let i = 0; i < 20 && typeof OmegaPac === 'undefined' && sameDocument(); ++i)
          await new Promise(resolve => setTimeout(resolve, 100));
        approvedPacData = islandPac(actual);
      }
      if (!sameDocument()) throw new Error('document changed');
      if (custom && !helper.sameIntent(prepared,await prepareCustom(await call('getAll',[])))) {
        mark(22); await host('different');return;
      }
      // Reauthorize after asynchronous preflight, immediately before mutation.
      if (!readOnly) {
        mark(13);
        if (await host('apply') !== 'authorized' || !sameDocument()) throw new Error('apply denied or document changed');
        mark(14);
        await call('applyProfile', [action, {update: false}]);
      }
      if (!sameDocument()) throw new Error('document changed');
      mark(15);
      const state = await call('getState', [['currentProfileName', 'isSystemProfile']]);
      let matched = state && state.currentProfileName === action && state.isSystemProfile === false;
      if (!matched) mark(state && state.currentProfileName===action ? 24 : 23);
      if (custom && matched) {
        matched=helper.sameIntent(prepared,await prepareCustom(await call('getAll',[])));
        if (!matched) mark(22);
      }
      if (matched) {
        // Read the browser's effective regular-profile configuration, rather
        // than only the worker's in-memory selected name. Never write it here.
        // This confirms configuration/control, not PAC reachability or routing.
        const settings = chrome.proxy.settings;
        observer = () => {
          changed = true;
          if (observing) {
            observing = false;
            stopObserver();
            // Revocation only: never write or reapply a configuration here.
            void host('changed').catch(() => {});
          }
        };
        settings.onChange.addListener(observer);
        mark(16);
        const effective = await proxyConfiguration();
        if (!sameDocument()) throw new Error('document changed');
        if (changed) {mark(29); throw new Error('configuration changed during readback');}
        matched = matchesProxy(effective);
        if (!matched) mark(effective.levelOfControl!=='controlled_by_this_extension' ? 25 :
          !effective.value || (custom && effective.value.mode!==prepared.config.mode) ? 26 :
          effective.value.mode==='pac_script' && (!effective.value.pacScript || effective.value.pacScript.mandatory!==true) ? 27 : 28);
      }
      if (readOnly) {
        // Startup has authority to observe, but a saved choice is never proof
        // of live state. Fail in preflight on a mismatch; the host may then
        // choose one separately journaled restoration, never a timer retry.
        if (!matched) { await host('different'); return; }
        if (await host('apply') !== 'authorized' || !sameDocument() || changed)
          throw new Error('observation denied or document changed');
        // This handshake enters the shared readback phase; no worker write is
        // performed by the readOnly path, including an interrupted selection.
      }
      if (matched) mark(0);
      const acknowledgement=await host(matched ? 'matched' : 'different');
      if (matched && acknowledgement!=='received') throw new Error('selection not committed');
      confirmed=matched;
      if (matched && rememberStartup) {
        // The selected proxy is already applied and committed by the host.
        // Empty is ZeroOmega's original "last used" startup setting. Every
        // selection writes that same value, so late replies cannot restore an
        // older profile name over a newer choice. Never edit profile storage.
        const latest=await call('getAll',[]);
        if (!sameDocument() || changed) throw new Error('selection changed');
        const key='-startupProfileName';
        if (latest[key]!=='') {
          const delta=Object.prototype.hasOwnProperty.call(latest,key)?[latest[key],'']:[''];
          await call('patch',[{[key]:delta}]);
        }
        const saved=await call('getAll',[]);
        if (!sameDocument() || changed || saved[key]!=='') throw new Error('startup choice unavailable');
        if (custom && !helper.sameIntent(prepared,await prepareCustom(saved))) {
          mark(22); throw new Error('selection changed');
        }
        const current=await call('getState',[['currentProfileName','isSystemProfile']]);
        const effective=await proxyConfiguration();
        if (!sameDocument() || changed || !current || current.currentProfileName!==action ||
            current.isSystemProfile!==false || !matchesProxy(effective))
          throw new Error('selection changed');
      }
      if (matched && observer) {
        if (changed || !sameDocument()) {
          await host('changed');
        } else {
          observing = true;
          watch = {stop: () => { observing = false; stopObserver(); }};
          window.__gbrowserProxyWatch = watch;
          heartbeat = setInterval(async () => {
            if (!observing || verifying) return;
            verifying = true;
            try {
              const effective = await proxyConfiguration();
              if (!observing) return;
              if (custom) {
                const current=await call('getState',[['currentProfileName','isSystemProfile']]);
                const options=await call('getAll',[]);
                if (!current || current.currentProfileName!==action || current.isSystemProfile!==false ||
                    !helper.sameIntent(prepared,await prepareCustom(options))) throw new Error('profile changed');
              }
              if (!sameDocument() || !matchesProxy(effective) || await host('verified') !== 'received')
                throw new Error('observation lost');
            } catch (_) {
              if (observing && observer) observer();
            } finally { verifying = false; }
          }, 15000);
        }
      }
    }
  } catch (_) {
    try { await host(confirmed ? 'changed' : 'unconfirmed'); } catch (_) { /* Native deadline also fails closed. */ }
  } finally {
    if (!observing) stopObserver();
  }
})
