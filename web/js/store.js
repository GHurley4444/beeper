// All saved state lives in one localStorage key, the web stand-in for the
// firmware's NVS "pscript" namespace.
const KEY = 'beeper:v1';

const menu = () => ({ pass: 0, fail: 0, streak: 0, best: 0, history: [], current: null });

export const defaults = () => ({
  v: 1,
  settings: {
    timer: { d: 0, h: 0, m: 20 },
    idleMs: 120000,        // 0 = never
    glyphMs: 90,           // scramble/reveal step: 270 slow, 90 med, 30 fast
    revealChars: 1,        // chars unlocked per step: 1, 2, 4
    transitionPx: 10,      // result wipe speed: 5, 10, 25 (px per 30ms on a 240px screen)
    diffMed: 10,
    diffHard: 25,
    pools: 0b1111,         // which menus feed TIMED: bit 0 GAME, 1 CITY, 2 CON, 3 TASK
    sound: true,
  },
  menus: [menu(), menu(), menu(), menu(), menu()],
  timed: { active: false, passed: false, paused: false, start: 0, pauseRem: 0, pool: 0, text: '', rare: false },
  tarot: { deck: [], pos: 22, history: [] },
  countdown: { h: 0, m: 5, s: 0, status: 'set', target: 0, pauseRem: 0 },
  loop: 0,
});

// Fill in fields added after a save was written, keep everything the user has.
function merge(base, saved) {
  if (Array.isArray(base)) return Array.isArray(saved) ? saved.map((v, i) => (i < base.length ? merge(base[i], v) : v)) : base;
  if (base && typeof base === 'object') {
    if (!saved || typeof saved !== 'object') return base;
    const out = { ...saved };
    for (const k of Object.keys(base)) out[k] = merge(base[k], saved[k]);
    return out;
  }
  return saved === undefined || typeof saved !== typeof base ? base : saved;
}

export function load() {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY));
    if (saved?.v === 1) return merge(defaults(), saved);
  } catch { /* unreadable or blocked storage: start fresh */ }
  return defaults();
}

export function save(state) {
  try {
    localStorage.setItem(KEY, JSON.stringify(state));
    return true;
  } catch {
    return false; // private mode or quota; the app keeps working for this session
  }
}
