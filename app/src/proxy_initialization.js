// Executed only in an explicitly confirmed, pinned original ZeroOmega options
// document. Use its replied worker API; no storage edits, silent retries or
// original-package changes. A lost reply may follow a partially completed reset.
(async function runProxyAction(operation, expectedUrl, expected, action, readOnly = false) {
  const host = (verb) => new Promise((resolve, reject) => {
    window.gbrowserProxyQuery({request: operation + ':' + verb, persistent: false,
      onSuccess: resolve, onFailure: () => reject(new Error('host denied'))});
  });
  const sameDocument = () => location.href.split('#')[0] === expectedUrl &&
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
    if (!actual || typeof actual !== 'object' || Array.isArray(actual) ||
        Object.keys(actual).length !== Object.keys(expected).length) return false;
    return Object.keys(expected).every(key => Object.prototype.hasOwnProperty.call(actual, key) &&
      (key.startsWith('+') ? equalProfile(expected[key], actual[key]) : equal(expected[key], actual[key])));
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
    }, 15000);
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
    const timeout = setTimeout(() => abort.abort(), 15000);
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
  const matchesProxy = effective => effective.levelOfControl === 'controlled_by_this_extension' &&
    effective.value && effective.value.mode === 'pac_script' && effective.value.pacScript &&
    effective.value.pacScript.mandatory === true &&
    (approvedPacData === null ?
      effective.value.pacScript.url === expected['+acgpower'].pacUrl &&
        !Object.prototype.hasOwnProperty.call(effective.value.pacScript, 'data') :
      effective.value.pacScript.data === approvedPacData &&
        !Object.prototype.hasOwnProperty.call(effective.value.pacScript, 'url'));
  let observer = null, watch = null, observing = false, changed = false, heartbeat = 0, verifying = false;
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
    if (!['initialize', 'direct', 'acgpower', '岛风go'].includes(action) ||
        typeof readOnly !== 'boolean' || (readOnly && action === 'initialize')) return;
    await waitForHost();
    // A document signal starts only the read-only worker preflight. Runtime
    // bindings/worker replies still have to exist before native apply authority.
    if (!sameDocument()) throw new Error('controller unavailable');
    if (await host('begin') !== 'authorized' || !sameDocument()) throw new Error('controller changed');
    // Stop the complete previous observation, including its interval. Hidden
    // tabs may throttle timers, so waiting for the old operation to expire
    // would accumulate observers after repeated selections.
    if (window.__gbrowserProxyWatch) window.__gbrowserProxyWatch.stop();
    if (action === 'initialize') {
      // Authorization precedes the first mutation; never issue reset twice.
      if (window.__gbrowserProxyInitializationStarted) throw new Error('already submitted');
      window.__gbrowserProxyInitializationStarted = true;
      await call('reset', [expected]);
      if (!sameDocument()) throw new Error('document changed');
      const matched = equalOptions(await call('getAll', []));
      await host(matched ? 'matched' : 'different');
      // Reset changes the worker while the options form retains its old copy.
      // Refresh only after native acknowledgement; never retry a write here.
      if (matched && sameDocument()) {
        try { location.reload(); } catch (_) { /* The user can refresh normally. */ }
      }
    } else {
      const actual = await call('getAll', []);  // The original worker's readiness reply; no separate probe/write.
      if (!baselineMatches(actual)) { await host('different'); return; }
      if (action === 'acgpower' && actual['+acgpower'].pacScript) {
        const local = await readLocalPac();
        if (actual['+acgpower'].pacScript !== local) { await host('different'); return; }
        approvedPacData = pacAscii(local);
      } else if (readOnly && action === 'acgpower') {
        // A saved URL configuration alone does not mean the local accelerator
        // is running. Startup stays at preparation when its fixed PAC service
        // is unavailable; no fallback to direct and no configuration write.
        await readLocalPac();
      }
      if (!sameDocument()) throw new Error('document changed');
      // Reauthorize after asynchronous preflight, immediately before mutation.
      if (!readOnly) {
        if (await host('apply') !== 'authorized' || !sameDocument()) throw new Error('apply denied or document changed');
        await call('applyProfile', [action, {update: false}]);
      }
      if (!sameDocument()) throw new Error('document changed');
      const state = await call('getState', [['currentProfileName', 'isSystemProfile']]);
      let matched = state && state.currentProfileName === action && state.isSystemProfile === false;
      if (matched && action === 'acgpower') {
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
        const effective = await proxyConfiguration();
        if (!sameDocument()) throw new Error('document changed');
        if (changed) throw new Error('configuration changed during readback');
        matched = matchesProxy(effective);
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
      await host(matched ? 'matched' : 'different');
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
    try { await host('unconfirmed'); } catch (_) { /* Native deadline also fails closed. */ }
  } finally {
    if (!observing) stopObserver();
  }
})
