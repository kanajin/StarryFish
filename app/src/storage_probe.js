(function () {
  "use strict";
  const KEY = "gbrowser-storage-probe-v1";
  const DB = "gbrowser-storage-probe-v1";
  const STORE = "markers", RECORD = "profile";
  const LOCK = "gbrowser-storage-probe-v1";
  const fail = code => { throw new Error(code); };

  function expectedMarker(profile) {
    if (!/^p-[0-9a-f]{32}$/.test(profile)) fail("INVALID_PROFILE");
    return JSON.stringify({ schema: 1, profile });
  }
  function inspectLocal(local, expected) {
    const value = local.getItem(KEY);
    if (value === null) return "MISSING";
    if (value !== expected) fail("LOCAL_FOREIGN_OR_INVALID");
    return "MATCHED";
  }
  function validateSchema(db) {
    if (db.version !== 1 || db.objectStoreNames.length !== 1 ||
        db.objectStoreNames[0] !== STORE) fail("DATABASE_SCHEMA_INVALID");
  }
  function awaitSignal(start, signal) {
    return new Promise((resolve, reject) => {
      let done = false;
      const finish = (error, value) => {
        if (done) return;
        done = true; signal.removeEventListener("abort", aborted);
        if (error) reject(error); else resolve(value);
      };
      const aborted = () => finish(new Error("OPERATION_TIMEOUT"));
      if (signal.aborted) { aborted(); return; }
      signal.addEventListener("abort", aborted, { once: true });
      try {
        // Abort must settle the callback that owns the Web Lock, not just its
        // outer UI race. Both late resolve/reject are consumed and ignored.
        Promise.resolve(start()).then(value => finish(null, value), error => finish(error));
      } catch (error) { finish(error); }
    });
  }
  function openDatabase(indexedDB, create, signal) {
    return new Promise((resolve, reject) => {
      let request, done = false;
      const stop = error => {
        if (done) return;
        done = true; clearTimeout(timer);
        signal.removeEventListener("abort", aborted);
        if (request && request.transaction) {
          try { request.transaction.abort(); } catch (_) { /* already aborted */ }
        }
        reject(error);
      };
      const aborted = () => stop(new Error("DATABASE_OPEN_ABORTED"));
      const timer = setTimeout(() => stop(new Error("DATABASE_OPEN_TIMEOUT")), 10000);
      signal.addEventListener("abort", aborted, { once: true });
      try {
        if (signal.aborted) fail("OPERATION_TIMEOUT");
        request = create ? indexedDB.open(DB, 1) : indexedDB.open(DB);
        let created = false;
        request.onupgradeneeded = event => {
          try {
            // A read never creates/upgrades, including a deletion race after
            // databases(). Late opens after cancellation must abort upgrades.
            if (done || !create || event.oldVersion !== 0) fail("DATABASE_CHANGED");
            request.result.createObjectStore(STORE);
            created = true;
          } catch (error) { request.transaction.abort(); stop(error); }
        };
        request.onerror = () => stop(new Error("DATABASE_OPEN_FAILED"));
        request.onblocked = () => stop(new Error("DATABASE_OPEN_BLOCKED"));
        request.onsuccess = () => {
          const db = request.result;
          if (done) { db.close(); return; }
          try {
            // A write is only allowed to create a previously absent database.
            if (create && !created) fail("DATABASE_APPEARED_DURING_WRITE");
            validateSchema(db);
            db.onversionchange = () => db.close();
            done = true; clearTimeout(timer);
            signal.removeEventListener("abort", aborted);
            resolve(db);
          } catch (error) { db.close(); stop(error); }
        };
      } catch (error) { stop(error); }
    });
  }
  function transact(db, expected, write, signal) {
    return new Promise((resolve, reject) => {
      let tx, done = false, keys, values;
      const finish = error => {
        if (done) return;
        done = true; clearTimeout(timer);
        signal.removeEventListener("abort", aborted);
        if (error && tx) { try { tx.abort(); } catch (_) { /* already completed */ } }
        db.close();
        if (error) reject(error); else resolve("MATCHED");
      };
      const aborted = () => finish(new Error("TRANSACTION_ABORTED"));
      const timer = setTimeout(() => finish(new Error("TRANSACTION_TIMEOUT")), 10000);
      signal.addEventListener("abort", aborted, { once: true });
      try {
        if (signal.aborted) fail("OPERATION_TIMEOUT");
        tx = db.transaction(STORE, write ? "readwrite" : "readonly");
        tx.onabort = () => finish(new Error("TRANSACTION_ABORTED"));
        tx.onerror = () => finish(new Error("TRANSACTION_FAILED"));
        tx.oncomplete = () => {
          try {
            if (!write && (!keys || !values || keys.length !== 1 || values.length !== 1 ||
                keys[0] !== RECORD || values[0] !== expected)) fail("DATABASE_FOREIGN_OR_INCOMPLETE");
            finish();
          } catch (error) { finish(error); }
        };
        const store = tx.objectStore(STORE);
        if (store.keyPath !== null || store.autoIncrement || store.indexNames.length !== 0)
          fail("STORE_SCHEMA_INVALID");
        if (write) {
          // add preserves any conflicting record, unlike put.
          store.add(expected, RECORD);
        } else {
          const k = store.getAllKeys(undefined, 2), v = store.getAll(undefined, 2);
          k.onsuccess = () => { keys = k.result; };
          v.onsuccess = () => { values = v.result; };
        }
      } catch (error) { finish(error); }
    });
  }
  async function readDatabase(indexedDB, expected, signal) {
    if (!indexedDB || typeof indexedDB.databases !== "function") fail("DATABASE_LIST_UNAVAILABLE");
    const list = await awaitSignal(() => indexedDB.databases(), signal);
    if (signal.aborted) fail("OPERATION_TIMEOUT");
    if (!Array.isArray(list)) fail("DATABASE_LIST_INVALID");
    const owned = list.filter(entry => entry.name === DB);
    if (owned.length === 0) return "MISSING";
    if (owned.length !== 1 || owned[0].version !== 1) fail("DATABASE_VERSION_INVALID");
    return transact(await openDatabase(indexedDB, false, signal), expected, false, signal);
  }
  function createProbe({ profile, local, indexedDB, locks }) {
    const expected = expectedMarker(profile);
    let stopped = false;
    const locked = async run => {
      if (stopped) fail("PAGE_STOPPED");
      if (!locks || typeof locks.request !== "function") { stopped = true; fail("LOCKS_UNAVAILABLE"); }
      const controller = new AbortController();
      let timer;
      // Web Locks only cancels a queued request. The deadline also rejects an
      // active operation and signals its open/transaction helpers to abort.
      const timeout = new Promise((_, reject) => {
        timer = setTimeout(() => {
          controller.abort(); reject(new Error("OPERATION_TIMEOUT"));
        }, 15000);
      });
      try {
        return await Promise.race([timeout, locks.request(LOCK,
          { mode: "exclusive", signal: controller.signal }, async () => {
            if (controller.signal.aborted || stopped) fail("OPERATION_TIMEOUT");
            return run(controller.signal);
          })]);
      } catch (error) { stopped = true; throw error; }
      finally { clearTimeout(timer); }
    };
    const inspect = async signal => {
      const l = inspectLocal(local, expected);
      const d = await readDatabase(indexedDB, expected, signal);
      // Include a second LS read: our lock serializes cooperating probe tabs,
      // but it cannot stop unrelated code from modifying this origin.
      if (inspectLocal(local, expected) !== l) fail("LOCAL_CHANGED_DURING_READ");
      if (l !== d) fail("INCOMPLETE_MARKERS");
      return l;
    };
    return {
      read: () => locked(inspect),
      write: () => locked(async signal => {
        if (await inspect(signal) === "MATCHED") return "ALREADY_MATCHED";
        if (signal.aborted || inspectLocal(local, expected) !== "MISSING") fail("WRITE_PREFLIGHT_CHANGED");
        try {
          local.setItem(KEY, expected);
          if (inspectLocal(local, expected) !== "MATCHED") fail("LOCAL_WRITE_READBACK_FAILED");
          await transact(await openDatabase(indexedDB, true, signal), expected, true, signal);
          if (await inspect(signal) !== "MATCHED") fail("WRITE_READBACK_FAILED");
          return "WRITE_READ_MATCH";
        } catch (_) {
          // No delete/clear/rollback across two non-atomic stores. Preserve
          // any partial commit, stop further writes, and ask for inspection.
          fail("WRITE_FAILED_POSSIBLY_PARTIAL");
        }
      })
    };
  }
  function createPermissionProbe(notification) {
    let used = false;
    return async () => {
      if (used) fail("PERMISSION_CHECK_ALREADY_USED");
      used = true;
      if (!notification || typeof notification.requestPermission !== "function")
        fail("NOTIFICATION_API_UNAVAILABLE");
      if (notification.permission === "denied") return "ALREADY_DENIED";
      if (notification.permission !== "default") fail("PERMISSION_PREFLIGHT_NOT_DEFAULT");
      let timer;
      try {
        // Invoke synchronously inside the user's click, before any await.
        // Never construct a Notification, reset permissions or retry a request.
        const request = notification.requestPermission();
        const deadline = new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error("PERMISSION_CHECK_TIMEOUT")), 10000);
        });
        const result = await Promise.race([Promise.resolve(request), deadline]);
        if (result !== "denied" || notification.permission !== "denied")
          fail("PERMISSION_RESULT_NOT_DENIED");
        return "REQUEST_DENIED";
      } finally { clearTimeout(timer); }
    };
  }
  function createUnloadProbe(target) {
    let state = "UNARMED";
    const handler = event => { event.preventDefault(); event.returnValue = true; };
    return {
      arm: () => {
        if (state !== "UNARMED") fail("UNLOAD_CHECK_ALREADY_USED");
        if (!target || typeof target.addEventListener !== "function" ||
            typeof target.removeEventListener !== "function") fail("UNLOAD_API_UNAVAILABLE");
        target.addEventListener("beforeunload", handler);
        state = "ARMED";
      },
      disarm: () => {
        if (state !== "ARMED") fail("UNLOAD_CHECK_NOT_ARMED");
        // Retain ARMED on failure: never claim removal when the API throws.
        target.removeEventListener("beforeunload", handler);
        state = "DISARMED";
      }
    };
  }
  const api = { createProbe, createPermissionProbe, createUnloadProbe, expectedMarker, KEY, DB, STORE, RECORD };
  if (typeof module !== "undefined" && module.exports) { module.exports = api; return; }

  const permissionButton = document.getElementById("permission-check");
  const permissionStatus = document.getElementById("permission-status");
  const permissionCheck = createPermissionProbe(window.Notification);
  permissionButton.onclick = async () => {
    permissionButton.disabled = true;
    permissionStatus.textContent = "检查中…只请求权限，不创建通知。";
    try {
      const result = await permissionCheck();
      permissionStatus.textContent = result === "REQUEST_DENIED"
        ? "本次请求返回拒绝。须与原生权限回调日志核对，页面结果不能单独证明拒绝来源。"
        : "此域名已有拒绝状态；没有再次请求，不能证明本次原生回调已执行。";
    } catch (_) {
      permissionStatus.textContent = "检查未完成或结果异常；已停止，不再请求。请保留现场并报告。";
    }
  };
  permissionButton.disabled = false;
  const unloadArm = document.getElementById("unload-arm"), unloadDisarm = document.getElementById("unload-disarm");
  const unloadStatus = document.getElementById("unload-status"), unloadCheck = createUnloadProbe(window);
  unloadArm.onclick = () => {
    unloadArm.disabled = true;
    try {
      unloadCheck.arm(); unloadDisarm.disabled = false;
      unloadStatus.textContent = "已启用。本页关闭时应请求确认；拒绝后应保留本页。测试后请解除。";
    } catch (_) {
      unloadStatus.textContent = "启用失败；请停止测试并保留现场。";
    }
  };
  unloadDisarm.onclick = () => {
    unloadDisarm.disabled = true;
    try {
      unloadCheck.disarm();
      unloadStatus.textContent = "已解除关闭确认；这项检查不读写存储标记。";
    } catch (_) {
      unloadStatus.textContent = "解除失败；请停止测试，不强制结束进程。";
    }
  };
  unloadArm.disabled = false;
  const status = document.getElementById("status");
  const read = document.getElementById("read"), write = document.getElementById("write");
  let stopped = false;
  const descriptions = {
    MISSING: "两处均未观察到标记（只读，未创建数据库）。",
    MATCHED: "localStorage 与 IndexedDB 标记均匹配当前资料。",
    ALREADY_MATCHED: "两处标记已匹配；没有重复写入。",
    WRITE_READ_MATCH: "合成标记已写入，两处读回均匹配。"
  };
  try {
    const probe = createProbe({ profile: document.getElementById("profile").dataset.profile,
      local: window.localStorage, indexedDB: window.indexedDB, locks: navigator.locks });
    const perform = async (action, operation) => {
      read.disabled = write.disabled = true;
      status.textContent = "检查中…请勿刷新、关闭或切换资料。";
      try {
        const result = await operation();
        status.textContent = action + "：" + descriptions[result] + "\n仅为合成标记结果，完整资料隔离尚未验收。";
      } catch (error) {
        stopped = true;
        status.textContent = action + "失败：" + error.message + "\n已停止写入。请保留现场并报告；不要刷新后重试写入。";
      } finally { read.disabled = write.disabled = stopped; }
    };
    read.onclick = () => perform("读取", probe.read);
    write.onclick = () => perform("写入", probe.write);
    read.disabled = write.disabled = false;
    status.textContent = "就绪。尚未读取或写入；请先点击读取标记。";
  } catch (_) {
    status.textContent = "初始化失败，未写入。请停止测试并报告。";
  }
}());
