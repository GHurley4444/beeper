// =================================================
// core.js — globals, Arduino helpers, persistence.
// Mirrors the top of beeper.ino: same variables, same defaults,
// same storage keys (namespace "pscript"), same deck mechanics.
// =================================================

// ── Arduino helpers ──────────────────────────────
// millis() is wall-clock based so the TIMED prescript keeps counting while
// the app is closed (the device has no RTC and has to pause on reboot).
function millis() { return Date.now(); }
function random(a, b) {
  if (b === undefined) { b = a; a = 0; }
  if (b <= a) return a;
  return a + Math.floor(Math.random() * (b - a));
}
function constrain(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
function pad2(n) { return String(n).padStart(2, '0'); }
const min = Math.min, max = Math.max, abs = Math.abs;

// ── Palette (beeper.ino) ─────────────────────────
const BG_COLOR   = tft.color565(15, 15, 15);
const TEXT_COLOR = tft.color565(153, 202, 255);
const PASS_COLOR = tft.color565(0, 45, 110);
const FAIL_COLOR = tft.color565(200, 30, 30);
const RARE_COLOR = tft.color565(210, 160, 20);
const GOOD_COLOR = tft.color565(0, 180, 80);

// ── Screens (enum Screen) ────────────────────────
const MENU_GAME = 0, MENU_1 = 1, MENU_2 = 2, MENU_3 = 3, MENU_4 = 4,
  MAIN_MENU = 5, SETTINGS = 6, SETTINGS_TIMER = 7, SETTINGS_IDLE = 8,
  SETTINGS_BRIGHT = 9, SETTINGS_SPEED = 10, SETTINGS_DIFFICULTY = 11,
  SETTINGS_CATEGORIES = 12, RESET_CONFIRM = 13, HISTORY_SCREEN = 14,
  DICE_ROLL = 15, RESULT = 16, IDLE_ANIM = 17, GAME_PICKER = 18,
  CYBER_MENU = 19, CYBER_WIFI = 20, CYBER_BLE = 21, CYBER_BT = 22, CYBER_QR = 23,
  TOOLS_MENU = 24, TOOL_COIN = 25, TOOL_TAROT = 26, TOOL_ORACLE = 27,
  TOOL_TIMER = 28, TOOL_NAMES = 29, TOOL_CIPHER = 30,
  LYRICS_SCREEN = 31, SONG_SELECT = 32;

let screen = MENU_GAME, previousScreen = MENU_GAME;
const loopScreens = [MENU_GAME, MENU_1, MENU_2, MENU_3, MENU_4];
const loopLen = 5;
let loopIdx = 0;

function isPrescriptScreen(s) { return s === MENU_GAME || s === MENU_1 || s === MENU_2 || s === MENU_3; }

// ── Settings / tuning ────────────────────────────
const BL_LEVELS = [0.45, 0.72, 1.0];     // LOW / MID / HIGH as CSS brightness
let brightnessLevel = 2;
let cfg_glyphSpinMs  = 90;
let cfg_revealChars  = 1;
let cfg_transitionPx = 10;
const RESULT_REVEAL_MS = 120, DICE_SPIN_FRAMES = 40, DICE_SPIN_MS = 30, DICE_SETTLE_FRAMES = 20;
const SLEEP_NEVER = 0xFFFFFFFF;
let sleepMs = 120000;             // idle animation trigger
const blSleepMs = 300000;         // "backlight off" after 5 min
let sleeping = false;
let lastTouchTime = 0, lastInteractionTime = 0;
let diffMedThreshold = 10, diffHardThreshold = 25;
let timedPoolEnabled = 0b1111;
function poolEnabled(i) { return (timedPoolEnabled >> i) & 1; }
function poolToggle(i) { timedPoolEnabled ^= (1 << i); }

// ── Progress per menu (0=GAME 1=CITY 2=CON 3=TASK 4=TIMED) ──
const passCount = [0, 0, 0, 0, 0], failCount = [0, 0, 0, 0, 0];
const streak = [0, 0, 0, 0, 0], bestStreak = [0, 0, 0, 0, 0];
let newBest = false;
function menuDiffFor(m) {
  const p = passCount[m];
  if (p >= diffHardThreshold) return 2;
  if (p >= diffMedThreshold) return 1;
  return 0;
}

let rewardActive = false, rewardText = '';

// ── History: last 10 per menu ─────────────────────
// Same ring buffer as the firmware (-2 empty, -1 rare, else card index).
// The app also keeps the text of each card so the history screen can show
// what you actually got (the device only has the index to go on).
const HIST_LEN = 10;
const history = [0, 1, 2, 3, 4].map(() => new Array(HIST_LEN).fill(-2));
const historyText = [0, 1, 2, 3, 4].map(() => new Array(HIST_LEN).fill(''));
const histHead = [0, 0, 0, 0, 0];
function histPush(m, card, text) {
  history[m][histHead[m]] = card;
  historyText[m][histHead[m]] = text || '';
  histHead[m] = (histHead[m] + 1) % HIST_LEN;
}

// ── Timer (TIMED menu) ────────────────────────────
let timerMinutes = 20, timerHours = 0, timerDays = 0;
function timerDurationMs() { return timerDays * 86400000 + timerHours * 3600000 + timerMinutes * 60000; }
let timedActive = false, timedPassed = false, timedPaused = false, timedPauseRem = 0;
let timedCurrent = '', timedIsRare = false, timedPool = 0, timedCardIndex = -1;
let timedRevealIndex = 0, timedLines = [], timedLineCount = 0, timedStart = 0;
let timerBlinkCount = 0, timerBlinkLast = 0, timerBlinkOn = false;
let editMinutes = 20, editHours = 0, editDays = 0;
let histViewMenu = 0;
let sessionStart = 0;

// ── Decks: no-repeat shuffle per menu ─────────────
const MAX_DECK = 100;
const deckCounts = [FW.gamePrescripts.length, FW.cityPrescripts.length, FW.conPrescripts.length,
                    FW.taskPrescripts.length, FW.gamePrescripts.length];
const decks = [0, 1, 2, 3, 4].map(() => new Array(MAX_DECK).fill(0));
const deckPos = [MAX_DECK, MAX_DECK, MAX_DECK, MAX_DECK, MAX_DECK];
function shuffleDeck(deck, count) {
  for (let i = 0; i < count; i++) deck[i] = i;
  for (let i = count - 1; i > 0; i--) {
    const j = random(i + 1);
    const t = deck[i]; deck[i] = deck[j]; deck[j] = t;
  }
}
function drawCard(m) {
  const count = deckCounts[m];
  if (deckPos[m] >= count) { shuffleDeck(decks[m], count); deckPos[m] = 0; markDeckDirty(); }
  return decks[m][deckPos[m]++];
}

// =================================================
// PERSISTENCE — localStorage, same key names as the NVS namespace.
// The firmware debounces writes to 5s to spare flash; a phone can drop the
// app at any moment, so this writes quickly (0.5s debounce + on hide).
// =================================================
const STORE_KEY = 'beeper.pscript';
let prefs = {};
let _stateDirty = false, _deckDirty = false, _saveTimer = 0;

function prefsLoad() {
  try { prefs = JSON.parse(localStorage.getItem(STORE_KEY) || '{}') || {}; }
  catch (e) { prefs = {}; }
}
function prefsCommit() {
  try { localStorage.setItem(STORE_KEY, JSON.stringify(prefs)); } catch (e) { /* storage full / private */ }
}
function pget(k, d) { return (k in prefs) ? prefs[k] : d; }

function saveStateFast() {
  for (let i = 0; i < 5; i++) {
    prefs['pos' + i] = deckPos[i];
    prefs['pas' + i] = passCount[i];
    prefs['fai' + i] = failCount[i];
    prefs['str' + i] = streak[i];
    prefs['bst' + i] = bestStreak[i];
    prefs['hhd' + i] = histHead[i];
    prefs['hist' + i] = history[i].slice();
    prefs['htx' + i] = historyText[i].slice();
  }
  prefs.tmin = timerMinutes; prefs.thr = timerHours; prefs.tdy = timerDays;
  prefs.brt = brightnessLevel; prefs.slp = sleepMs;
  prefs.spg = cfg_glyphSpinMs; prefs.spr = cfg_revealChars; prefs.spt = cfg_transitionPx;
  prefs.dmed = diffMedThreshold; prefs.dhrd = diffHardThreshold;
  prefs.tpol = timedPoolEnabled;
  prefs.tact = timedActive; prefs.tpass = timedPassed; prefs.tpaus = timedPaused;
  let rem = 0;
  const dur = timerDurationMs();
  if (timedActive) {
    if (timedPaused) rem = timedPauseRem;
    else { const e = millis() - timedStart; rem = (e < dur) ? dur - e : 0; }
  }
  prefs.trem = rem;
  prefs.tsav = millis();          // app-only: lets the timer keep running while closed
  prefs.tcur = timedCurrent; prefs.trare = timedIsRare;
  prefs.tpool = timedPool; prefs.tcidx = timedCardIndex;
  prefs.trtd = tarotDeck.slice(); prefs.trtp = tarotPos;
  // App-only: keep each menu's current card. iOS relaunches web apps often,
  // and a relaunch shouldn't reroll your card the way a device reboot does.
  prefs.mcur = menuHasPrescript.map((h, m) => h ? menuCurrent[m] : '');
  prefs.mrar = menuIsRare.slice();
  prefsCommit();
  _stateDirty = false;
}

function saveState() {
  for (let i = 0; i < 5; i++) prefs['deck' + i] = decks[i].slice(0, deckCounts[i]);
  _deckDirty = false;
  saveStateFast();
}

function scheduleSave() {
  if (_saveTimer) return;
  _saveTimer = setTimeout(() => {
    _saveTimer = 0;
    if (_deckDirty) saveState(); else if (_stateDirty) saveStateFast();
  }, 500);
}
function markStateDirty() { _stateDirty = true; scheduleSave(); }
function markDeckDirty() { _deckDirty = true; _stateDirty = true; scheduleSave(); }
function flushState() { if (_deckDirty) saveState(); else if (_stateDirty) saveStateFast(); }

function loadState() {
  prefsLoad();
  const hasData = ('pos0' in prefs);
  if (hasData) {
    for (let i = 0; i < 5; i++) {
      const d = pget('deck' + i, null);
      if (Array.isArray(d)) for (let j = 0; j < deckCounts[i]; j++) decks[i][j] = d[j] | 0;
      else shuffleDeck(decks[i], deckCounts[i]);
      deckPos[i] = pget('pos' + i, MAX_DECK);
      passCount[i] = pget('pas' + i, 0);
      failCount[i] = pget('fai' + i, 0);
      streak[i] = pget('str' + i, 0);
      bestStreak[i] = pget('bst' + i, 0);
      histHead[i] = pget('hhd' + i, 0);
      const h = pget('hist' + i, null), t = pget('htx' + i, null);
      for (let j = 0; j < HIST_LEN; j++) {
        history[i][j] = Array.isArray(h) ? h[j] : -2;
        historyText[i][j] = Array.isArray(t) ? (t[j] || '') : '';
      }
    }
    timerMinutes = pget('tmin', 20); timerHours = pget('thr', 0); timerDays = pget('tdy', 0);
    brightnessLevel = pget('brt', 2); sleepMs = pget('slp', 120000);
    cfg_glyphSpinMs = pget('spg', 90); cfg_revealChars = pget('spr', 1); cfg_transitionPx = pget('spt', 10);
    diffMedThreshold = pget('dmed', 10); diffHardThreshold = pget('dhrd', 25);
    timedPoolEnabled = pget('tpol', 0b1111);
    timedActive = pget('tact', false); timedPassed = pget('tpass', false);
    timedCurrent = pget('tcur', ''); timedIsRare = pget('trare', false);
    timedPool = pget('tpool', 0); timedCardIndex = pget('tcidx', -1);

    // Restore the TIMED countdown. The device always comes back paused
    // (no clock); here time spent closed still counts.
    const savedPaused = pget('tpaus', false);
    const rem = pget('trem', 0), savedAt = pget('tsav', millis());
    const dur = timerDurationMs();
    if (timedActive && savedPaused) {
      timedPaused = true; timedPauseRem = rem;
    } else if (timedActive) {
      timedPaused = false; timedPauseRem = 0;
      const remNow = rem - (millis() - savedAt);
      timedStart = millis() - (dur - remNow);   // may already be expired: the background tick handles it
    } else {
      timedPaused = savedPaused; timedPauseRem = 0; timedStart = 0;
    }
    const mc = pget('mcur', null), mr = pget('mrar', null);
    if (Array.isArray(mc)) for (let m = 0; m < 4; m++) if (mc[m]) {
      menuCurrent[m] = mc[m]; menuIsRare[m] = !!(mr && mr[m]);
      menuLines[m] = wrapText(mc[m], 24); menuHasPrescript[m] = true;
    }
    if (timedActive && timedCurrent) {
      timedLines = wrapText(timedIsRare ? '* ' + timedCurrent + ' *' : '_' + timedCurrent + '_', 24);
      timedLineCount = timedLines.length;
    }
  } else {
    for (let i = 0; i < 5; i++) {
      shuffleDeck(decks[i], deckCounts[i]);
      shuffleDeck(decks[i], deckCounts[i]);
      deckPos[i] = 0;
      history[i].fill(-2); historyText[i].fill('');
    }
  }
}

function resetDeck(m, resetStats) {
  shuffleDeck(decks[m], deckCounts[m]);
  deckPos[m] = 0;
  if (resetStats) {
    passCount[m] = 0; failCount[m] = 0; streak[m] = 0; bestStreak[m] = 0;
    histHead[m] = 0;
    history[m].fill(-2); historyText[m].fill('');
  }
  markDeckDirty();
}

// =================================================
// WORD WRAP — same algorithm as wrapText()/toolWrap() in the firmware.
// Words longer than the line are kept whole on their own line (the device
// truncates them to its buffer; none of the data hits that).
// =================================================
function wrapText(src, maxCols) {
  const out = [];
  let line = '', word = '';
  for (let i = 0; i <= src.length; i++) {
    if (i === src.length || src[i] === ' ') {
      const test = line ? line + ' ' + word : word;
      if (test.length > maxCols) { out.push(line); line = word; }
      else line = test;
      word = '';
    } else word += src[i];
  }
  if (line) out.push(line);
  return out;
}

function hit(tx, ty, bx, by, bw, bh) { return tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh; }

function drawButton(x, y, w, h, label, highlight = false) {
  const border = highlight ? TEXT_COLOR : tft.color565(60, 60, 80);
  tft.drawRect(x, y, w, h, border);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(x + ((w - label.length * 6) / 2 | 0), y + ((h - 8) / 2 | 0));
  tft.print(label);
}

function scramble() { return String.fromCharCode(random(33, 126)); }

// ── Logo (index.h), decoded once ──────────────────
let logoBuf = null;
function loadLogo() {
  const bin = atob(LOGO_B64);
  logoBuf = new Uint16Array(LOGO_W * LOGO_H);
  for (let i = 0; i < logoBuf.length; i++) logoBuf[i] = bin.charCodeAt(2 * i) | (bin.charCodeAt(2 * i + 1) << 8);
}
