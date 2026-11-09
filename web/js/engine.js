// Game rules ported from beeper.ino. No DOM access here, so it runs under `node --test`.
import D from '../data.js';

export const MENUS = ['GAME', 'CITY', 'CON', 'TASK', 'TIMED'];
export const TIMED = 4;
const POOL_KEYS = ['game', 'city', 'con', 'task'];
const TIERS = ['easy', 'normal', 'hard'];
export const TIER_NAMES = ['EASY', 'NORMAL', 'HARD'];
const HISTORY_LEN = 10;

export const rnd = (n) => Math.floor(Math.random() * n);
export const pick = (list) => list[rnd(list.length)];

// 0 = easy, 1 = normal, 2 = hard. Progresses with passes, thresholds are global.
export function difficulty(passes, settings) {
  if (passes >= settings.diffHard) return 2;
  if (passes >= settings.diffMed) return 1;
  return 0;
}

export function ordinal(n) {
  const m = n % 100;
  const suffix = m >= 11 && m <= 13 ? 'th' : ({ 1: 'st', 2: 'nd', 3: 'rd' }[n % 10] ?? 'th');
  return n + suffix;
}

// Builds one prescript from the component pools, same templates as the firmware.
export function generate(pool, diff) {
  const g = D.gen[POOL_KEYS[pool]];
  const p = (part) => pick(g[part][TIERS[diff]]);
  if (pool === 1) {
    const ord = ordinal(1 + rnd(diff === 0 ? 20 : 100));
    if (diff === 2) {
      // Hard uses the shorter template; retry a few times to keep it readable.
      let s = '';
      for (let i = 0; i < 8; i++) {
        s = `${p('cond')}, find the ${ord} ${p('adj')} ${p('noun')} you see and ${p('act2')}. ${p('close')}`;
        if (s.length <= 110) break;
      }
      return s;
    }
    return `${p('cond')}, ${p('act1')} and find the ${ord} ${p('adj')} ${p('noun')} you see and ${p('act2')}. ${p('close')}`;
  }
  if (pool === 2) return `${p('cond')}, ${p('act1')} and ${p('act2')}. ${p('close')}`;
  return `${p('cond')}, ${p('obj')}. ${p('close')}`;
}

// A card is { text, rare }. Rare "Index" cards appear 1 in 20 once past Easy.
export function drawCard(pool, diff) {
  if (diff > 0 && rnd(20) === 0) return { text: pick(D.rare[pool]), rare: true };
  return { text: generate(pool, diff).toUpperCase(), rare: false };
}

export const cardLabel = (card) => (card.rare ? `* ${card.text} *` : `_${card.text}_`);

export function pushHistory(menu, card, now) {
  menu.history.push({ text: card.text, rare: card.rare, at: now });
  if (menu.history.length > HISTORY_LEN) menu.history.splice(0, menu.history.length - HISTORY_LEN);
}

// Updates counters for a PASS/FAIL. Returns { newBest, reward } where reward is the
// reward/punishment line due on every 5th pass or fail (not for TIMED).
export function record(state, menuIdx, pass) {
  const m = state.menus[menuIdx];
  let newBest = false;
  if (pass) {
    m.pass++; m.streak++;
    if (m.streak > m.best) { m.best = m.streak; newBest = true; }
  } else {
    m.fail++; m.streak = 0;
  }
  let reward = null;
  const count = pass ? m.pass : m.fail;
  if (menuIdx !== TIMED && count > 0 && count % 5 === 0) {
    const list = D.rewards[menuIdx][pass ? 0 : 1];
    reward = list[(count / 5 - 1) % list.length];
  }
  return { newBest, reward };
}

// The card currently shown on a regular menu; drawn on first visit and after each result.
export function currentCard(state, menuIdx, now) {
  const m = state.menus[menuIdx];
  if (!m.current) {
    m.current = drawCard(menuIdx, difficulty(m.pass, state.settings));
    pushHistory(m, m.current, now);
  }
  return m.current;
}

export function resetMenu(state, menuIdx) {
  Object.assign(state.menus[menuIdx], { pass: 0, fail: 0, streak: 0, best: 0, history: [], current: null });
  if (menuIdx === TIMED) Object.assign(state.timed, { active: false, passed: false, paused: false });
}

// ── TIMED ────────────────────────────────────────
export const durationMs = ({ d, h, m }) => ((d * 24 + h) * 60 + m) * 60000;

export function timedRemaining(state, now) {
  const t = state.timed, dur = durationMs(state.settings.timer);
  if (!t.active) return dur;
  if (t.paused) return t.pauseRem;
  return Math.max(0, dur - (now - t.start));
}

export function timedAssign(state, now) {
  const enabled = [0, 1, 2, 3].filter((i) => (state.settings.pools >> i) & 1);
  const pool = enabled.length ? pick(enabled) : 0;
  const card = drawCard(pool, difficulty(state.menus[TIMED].pass, state.settings));
  Object.assign(state.timed, {
    active: true, passed: false, paused: false, pauseRem: 0, start: now, pool, text: card.text, rare: card.rare,
  });
  pushHistory(state.menus[TIMED], card, now);
}

// Called every second from any screen. When the interval runs out an unpassed card
// counts as a fail, then a new card starts. Returns true if anything changed.
export function timedTick(state, now) {
  const t = state.timed;
  if (!t.active || t.paused || timedRemaining(state, now) > 0) return false;
  if (!t.passed) { state.menus[TIMED].fail++; state.menus[TIMED].streak = 0; }
  timedAssign(state, now);
  return true;
}

export function timedTogglePause(state, now) {
  const t = state.timed, dur = durationMs(state.settings.timer);
  if (t.paused) { t.start = now - (dur - t.pauseRem); t.paused = false; }
  else { t.pauseRem = Math.max(0, dur - (now - t.start)); t.paused = true; }
}

// FAIL restarts the clock on the same card: wait it out before a new one.
export function timedFail(state, now) {
  const r = record(state, TIMED, false);
  Object.assign(state.timed, { start: now, paused: false, pauseRem: 0 });
  return r;
}

export function timedPass(state) {
  const r = record(state, TIMED, true);
  state.timed.passed = true;
  return r;
}

// "1:05:09" or "5:09"
export function clock(ms) {
  const s = Math.ceil(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  const mm = String(m).padStart(h ? 2 : 1, '0'), ss = String(sec).padStart(2, '0');
  return h ? `${h}:${mm}:${ss}` : `${mm}:${ss}`;
}

export const pad2 = (n) => String(n).padStart(2, '0');
