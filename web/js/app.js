// App shell: routing, saving, the once-a-second background tick (TIMED expiry and
// the idle screensaver), screen wake lock and offline support.
import { load, save } from './store.js';
import { timedTick } from './engine.js';
import { lifetime } from './ui.js';
import { play, LOOP } from './screens/play.js';
import * as menus from './screens/menus.js';
import * as tools from './screens/tools.js';
import { songs, lyrics } from './screens/lyrics.js';
import { openIdle } from './screens/idle.js';
import { beep, setSound, unlockAudio } from './sound.js';

const root = document.getElementById('app');
const overlayHost = document.getElementById('overlay');
let life = null, overlayOpen = false, lastInteraction = Date.now();

export const app = {
  state: load(),
  session: Date.now(),
  save() { save(app.state); },
  emit(type) { window.dispatchEvent(new Event(`beeper:${type}`)); },

  // Routes live in the URL hash (#/settings/timer) so screens can be deep-linked.
  go(path, { replace = false } = {}) {
    const i = history.state?.i ?? 0;
    if (replace) history.replaceState({ i }, '', `#${path}`);
    else history.pushState({ i: i + 1 }, '', `#${path}`);
    render(!replace);
  },
  back(fallback) {
    if ((history.state?.i ?? 0) > 0) history.back();
    else app.go(fallback, { replace: true });
  },
  rerender() { render(false); },

  // Full-screen layer for the result wipe and the idle screensaver.
  openOverlay(el) {
    overlayOpen = true;
    root.inert = true;
    overlayHost.replaceChildren(el);
    return {
      close() {
        overlayOpen = false;
        root.inert = false;
        overlayHost.replaceChildren();
        lastInteraction = Date.now();
      },
    };
  },

  // Keep the phone's screen on while this screen is showing (timers, lyrics).
  stayAwake(screenLife) {
    wakeWanted++;
    syncWakeLock();
    screenLife.add(() => { wakeWanted--; syncWakeLock(); });
  },
};

const routes = [
  [/^(game|city|con|task|timed)$/, (m) => play(root, app, life, LOOP.indexOf(m[1]))],
  [/^menu$/, () => menus.mainMenu(root, app, life)],
  [/^tools$/, () => menus.toolsMenu(root, app, life)],
  [/^picker$/, () => menus.picker(root, app, life)],
  [/^settings$/, () => menus.settings(root, app, life)],
  [/^settings\/(\w+)$/, (m) => menus.settingsPage(root, app, life, m[1])],
  [/^history$/, () => menus.historyScreen(root, app, life)],
  [/^tool\/(\w+)$/, (m) => tools.open(root, app, life, m[1])],
  [/^songs$/, () => songs(root, app, life)],
  [/^lyrics\/(\d+)$/, (m) => lyrics(root, app, life, Number(m[1]))],
];

function render(moveFocus) {
  life?.end();
  life = lifetime();
  const path = location.hash.replace(/^#\/?/, '');
  for (const [re, show] of routes) {
    const m = path.match(re);
    if (m && show(m) !== false) {
      const title = root.querySelector('h1')?.textContent;
      document.title = title ? `${title} · Beeper` : 'Beeper';
      if (moveFocus) root.querySelector('h1')?.focus({ preventScroll: true });
      return;
    }
  }
  app.go(`/${LOOP[app.state.loop] ?? 'game'}`, { replace: true });
}

// ── Wake lock ────────────────────────────────────
let wakeWanted = 0, wakeLock = null;
async function syncWakeLock() {
  try {
    if (wakeWanted > 0 && !wakeLock && document.visibilityState === 'visible' && 'wakeLock' in navigator) {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    } else if (wakeWanted === 0 && wakeLock) {
      await wakeLock.release();
      wakeLock = null;
    }
  } catch { /* not allowed right now (low battery, background); harmless */ }
}

// ── Background tick ──────────────────────────────
function tick() {
  const now = Date.now();
  if (timedTick(app.state, now)) { app.save(); app.emit('timed'); beep.newCard(); }
  app.emit('second');
  const onPrescript = /^#\/(game|city|con|task)$/.test(location.hash);
  const idleMs = app.state.settings.idleMs;
  if (onPrescript && !overlayOpen && idleMs > 0 && now - lastInteraction > idleMs && document.visibilityState === 'visible') {
    openIdle(app);
  }
}

addEventListener('pointerdown', () => { lastInteraction = Date.now(); unlockAudio(); }, { capture: true });
addEventListener('keydown', (e) => {
  lastInteraction = Date.now();
  unlockAudio();
  // Escape acts as the on-screen BACK button.
  if (e.key === 'Escape' && !overlayOpen && !document.querySelector('dialog[open]')) root.querySelector('.bar-back, .lyrics-close')?.click();
}, { capture: true });
addEventListener('popstate', () => render(true));
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') { lastInteraction = Date.now(); tick(); }
  syncWakeLock();
});
// Another tab changed the save: reload it so two open copies don't overwrite each other.
addEventListener('storage', (e) => {
  if (e.key === 'beeper:v1') { app.state = load(); setSound(app.state.settings.sound); app.rerender(); }
});
setSound(app.state.settings.sound);

history.replaceState({ i: 0 }, '', location.hash || `#/${LOOP[app.state.loop] ?? 'game'}`);
render(false);
setInterval(tick, 1000);
tick();

if ('serviceWorker' in navigator && isSecureContext) navigator.serviceWorker.register('sw.js').catch(() => {});
navigator.storage?.persist?.().catch(() => {});
