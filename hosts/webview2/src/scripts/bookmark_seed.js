(function seedBookmarks(initial) {
  'use strict';
  // Keep the original script's origin-local storage and JSON schema. Seeding
  // happens synchronously before it reads defaults, only on an empty profile.
  if (window !== window.top || location.protocol !== 'https:' ||
      !['game.granbluefantasy.jp', 'gbf.game.mbga.jp'].includes(location.hostname)) return;
  const keys = ['gbf-bookmark:data', 'gbf-bookmark:config'];
  const writes = [];
  try {
    if (keys.some(key => localStorage.getItem(key) !== null)) return;
    const values = [JSON.stringify(initial.data), JSON.stringify(initial.config)];
    for (let i = 0; i < keys.length; ++i) {
      // Recheck both keys before the first write; preserve concurrent user data.
      if (i === 0 && keys.some(key => localStorage.getItem(key) !== null)) return;
      if (i > 0 && localStorage.getItem(keys[i]) !== null) throw new Error('Bookmark storage changed');
      localStorage.setItem(keys[i], values[i]); writes.push(i);
      if (localStorage.getItem(keys[i]) !== values[i]) throw new Error('Bookmark storage readback failed');
    }
  } catch (_) {
    // Best-effort rollback only of bytes written by this operation; never
    // remove a user's replacement. Quota/disabled storage is not success.
    for (const i of writes.reverse()) {
      try {
        if (localStorage.getItem(keys[i]) === JSON.stringify(i === 0 ? initial.data : initial.config))
          localStorage.removeItem(keys[i]);
      } catch (_) {}
    }
    console.warn('StarryFish：书签初始配置未保存，原有配置保持；请检查页面存储。');
  }
})
