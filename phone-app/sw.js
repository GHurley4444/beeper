// Offline cache for the Beeper app shell. Bump VERSION when files change
// so phones pick up the new build on next launch.
const VERSION = 'beeper-v1';
const FILES = [
  './', 'index.html', 'manifest.webmanifest', 'css/app.css',
  'js/font.js', 'js/tft.js', 'js/data.js', 'js/logo.js', 'js/core.js', 'js/prescript.js',
  'js/screens.js', 'js/lyrics.js', 'js/tools.js', 'js/cyber.js', 'js/main.js',
  'icons/icon-180.png', 'icons/icon-192.png', 'icons/icon-512.png',
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(VERSION).then(c => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.origin !== location.origin) return;
  if (url.searchParams.has('ping')) return;           // NETWORK STATUS latency test: always live
  e.respondWith(caches.match(e.request, { ignoreSearch: true }).then(hit => hit || fetch(e.request)));
});
