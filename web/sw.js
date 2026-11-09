// Offline support. Bump VERSION when shipping changes so every file updates together.
const VERSION = 'beeper-2';
const FILES = [
  './', 'index.html', 'styles.css', 'manifest.webmanifest', 'data.js',
  'js/app.js', 'js/engine.js', 'js/store.js', 'js/ui.js', 'js/sound.js',
  'js/screens/play.js', 'js/screens/result.js', 'js/screens/menus.js',
  'js/screens/tools.js', 'js/screens/lyrics.js', 'js/screens/idle.js',
  'img/logo.png', 'img/icon-180.png', 'img/icon-192.png', 'img/icon-512.png',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k !== VERSION).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

// Cache first (works with no connection), refreshed in the background.
self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== location.origin) return;
  e.respondWith(caches.open(VERSION).then(async (cache) => {
    const hit = await cache.match(req, { ignoreSearch: true });
    const fresh = fetch(req).then((res) => {
      if (res.ok) cache.put(req, res.clone());
      return res;
    });
    if (hit) {
      e.waitUntil(fresh.catch(() => {}));
      return hit;
    }
    return fresh;
  }));
});
