// =================================================
// main.js — setup() + loop() + touch dispatch (beeper.ino).
// The loop runs at the device's ~15fps so every animation keeps its
// on-device speed. A release is a swipe if it moved more than 30
// screen pixels, otherwise a tap at the release point — same rule.
// =================================================

const canvas = document.getElementById('screen');
const sleepCover = document.getElementById('sleep');

function applyBrightness() { canvas.style.filter = brightnessLevel === 2 ? '' : `brightness(${BL_LEVELS[brightnessLevel]})`; }
function buzz() { try { navigator.vibrate && navigator.vibrate([200, 100, 200]); } catch (e) { /* not on iOS */ } }

function setSleeping(on) {
  sleeping = on;
  sleepCover.classList.toggle('on', on);
}

// ── Touch → 320x240 coordinates ──────────────────
function toScreen(ev) {
  const r = canvas.getBoundingClientRect();
  const x = Math.round((ev.clientX - r.left) * TFT_W / r.width);
  const y = Math.round((ev.clientY - r.top) * TFT_H / r.height);
  return [constrain(x, 0, TFT_W - 1), constrain(y, 0, TFT_H - 1)];
}

let pressActive = false, pressSwallow = false, sx = 0, sy = 0, pressId = null;

window.addEventListener('pointerdown', ev => {
  if (pressActive) return;
  pressActive = true; pressId = ev.pointerId;
  lastTouchTime = millis();
  pressSwallow = false;
  if (sleeping) { setSleeping(false); applyBrightness(); pressSwallow = true; return; }   // wake tap is swallowed
  [sx, sy] = toScreen(ev);
}, { passive: true });

window.addEventListener('pointerup', ev => {
  if (!pressActive || ev.pointerId !== pressId) return;
  pressActive = false;
  if (pressSwallow) return;
  const [x, y] = toScreen(ev);
  onRelease(x, y, x - sx, y - sy);
  tft.present();
}, { passive: true });

window.addEventListener('pointercancel', () => { pressActive = false; });
// Stop iOS scrolling / rubber-banding / zooming the page.
document.addEventListener('touchmove', e => e.preventDefault(), { passive: false });
document.addEventListener('gesturestart', e => e.preventDefault());
document.addEventListener('dblclick', e => e.preventDefault());
document.addEventListener('contextmenu', e => e.preventDefault());

function onRelease(x, y, dx, dy) {
  lastInteractionTime = millis();
  if (abs(dx) > 30 || abs(dy) > 30) onSwipe(dx, dy);
  else onTap(x, y);
}

const NO_SWIPE = new Set([SETTINGS, SETTINGS_TIMER, SETTINGS_IDLE, SETTINGS_BRIGHT, SETTINGS_SPEED,
  SETTINGS_DIFFICULTY, SETTINGS_CATEGORIES, RESET_CONFIRM, HISTORY_SCREEN,
  TOOLS_MENU, CYBER_MENU, TOOL_COIN, TOOL_TAROT, TOOL_ORACLE, TOOL_TIMER, TOOL_NAMES, TOOL_CIPHER,
  DICE_ROLL, GAME_PICKER, CYBER_WIFI, CYBER_BLE, CYBER_BT, CYBER_QR, RESULT]);

function onSwipe(dx, dy) {
  if (NO_SWIPE.has(screen)) return;
  if (screen === SONG_SELECT) { if (dy > 30) goTo(loopScreens[loopIdx]); return; }
  if (screen === LYRICS_SCREEN) { if (dy > 30) goTo(SONG_SELECT); return; }
  if (screen === MAIN_MENU) { if (dy < -30) goToLoopScreen(loopIdx); return; }
  if (screen === IDLE_ANIM) { idleActive = false; goToLoopScreen(loopIdx); return; }
  // Prescript carousel: horizontal = change menu, down = main menu, up = songs
  if (abs(dx) > abs(dy)) { if (dx > 0) nextScreen(); else prevScreen(); }
  else if (dy > 30) goTo(MAIN_MENU);
  else if (dy < -30 && isPrescriptScreen(screen)) goTo(SONG_SELECT);
}

function onTap(x, y) {
  switch (screen) {
    case IDLE_ANIM: idleActive = false; goToLoopScreen(loopIdx); return;
    case MENU_GAME: case MENU_1: case MENU_2: case MENU_3:
      if (y > 150) showResult(x < 160);
      return;
    case MENU_4: timedTap(x, y); return;
    case RESULT: resultTap(); return;
    case MAIN_MENU:
      if (hit(x, y, 95, 70, 130, 26)) goTo(TOOLS_MENU);
      else if (hit(x, y, 95, 102, 130, 26)) goTo(CYBER_MENU);
      else if (hit(x, y, 95, 134, 130, 26)) goTo(SETTINGS);
      else if (hit(x, y, 60, 188, 200, 28)) goTo(GAME_PICKER);
      return;
    case GAME_PICKER:
      if (hit(x, y, 40, 180, 110, 28)) gamePickerStart();
      else if (hit(x, y, 170, 180, 110, 28)) goTo(MAIN_MENU);
      return;
    case TOOLS_MENU:
      if (hit(x, y, 258, 220, 60, 20)) { goTo(MAIN_MENU); return; }
      for (let i = 0; i < TOOLS_TARGETS.length; i++) {
        if (hit(x, y, 4, 18 + i * 36 - toolsScrollY + 2, 278, 32)) { goTo(TOOLS_TARGETS[i]); return; }
      }
      return;
    case CYBER_MENU:
      if (hit(x, y, 2, 220, 60, 20)) { goTo(MAIN_MENU); return; }
      for (let i = 0; i < CYBER_TARGETS.length; i++) {
        if (hit(x, y, 4, 18 + i * 40 - cyberScrollY + 2, 278, 36)) { goTo(CYBER_TARGETS[i]); return; }
      }
      return;
    case TOOL_COIN: if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU); else toolCoinTap(x, y); return;
    case TOOL_TAROT:
      if (!tarotInHistory() && hit(x, y, 4, 208, 80, 22)) goTo(TOOLS_MENU); else toolTarotTap(x, y);
      return;
    case TOOL_ORACLE: if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU); else toolOracleTap(x, y); return;
    case TOOL_TIMER:
      if (cdState === CD_SET && hit(x, y, 170, 130, 90, 28)) goTo(TOOLS_MENU);
      else if (cdState !== CD_SET && hit(x, y, 210, 170, 80, 28)) goTo(TOOLS_MENU);
      else toolCDTap(x, y);
      return;
    case TOOL_NAMES: if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU); else toolGenTap(x, y); return;
    case TOOL_CIPHER: if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU); else toolCipherTap(x, y); return;
    case SONG_SELECT: songSelectTap(x, y); return;
    case LYRICS_SCREEN: goTo(SONG_SELECT); return;
    case CYBER_WIFI: if (hit(x, y, 95, 207, 130, 24)) goTo(CYBER_MENU); else cyberWifiTap(x, y); return;
    case CYBER_BLE: if (hit(x, y, 95, 207, 130, 24)) goTo(CYBER_MENU); else cyberBLETap(x, y); return;
    case CYBER_BT: if (hit(x, y, 95, 207, 130, 24)) goTo(CYBER_MENU); else cyberBTTap(x, y); return;
    case CYBER_QR: if (hit(x, y, 95, 207, 130, 24)) goTo(CYBER_MENU); else cyberQRTap(x, y); return;
    case DICE_ROLL: if (hit(x, y, 10, 200, 80, 22)) goTo(TOOLS_MENU); else startDiceRoll(); return;

    case SETTINGS:
      if (hit(x, y, 70, 38, 180, 22)) { editMinutes = timerMinutes; editHours = timerHours; editDays = timerDays; goTo(SETTINGS_TIMER); }
      else if (hit(x, y, 70, 66, 180, 22)) goTo(SETTINGS_IDLE);
      else if (hit(x, y, 70, 94, 180, 22)) goTo(SETTINGS_SPEED);
      else if (hit(x, y, 70, 122, 180, 22)) goTo(SETTINGS_DIFFICULTY);
      else if (hit(x, y, 70, 150, 180, 22)) goTo(SETTINGS_CATEGORIES);
      else if (hit(x, y, 70, 178, 180, 22)) goTo(RESET_CONFIRM);
      else if (hit(x, y, 70, 206, 180, 22)) { histViewMenu = 0; goTo(HISTORY_SCREEN); }
      else if (hit(x, y, 2, 220, 60, 20)) goTo(MAIN_MENU);
      else if (hit(x, y, 258, 220, 60, 20)) goTo(SETTINGS_BRIGHT);
      return;
    case SETTINGS_IDLE:
      IDLE_OPTS.forEach(([, ms], i) => { if (hit(x, y, 20, 68 + i * 30, 280, 24)) { sleepMs = ms; saveState(); drawSettingsIdle(); } });
      if (hit(x, y, 110, 218, 100, 22)) goTo(SETTINGS);
      return;
    case SETTINGS_DIFFICULTY:
      if (hit(x, y, 260, 56, 22, 18)) { if (diffMedThreshold > 1) diffMedThreshold--; saveState(); drawSettingsDifficulty(); }
      if (hit(x, y, 284, 56, 22, 18)) { diffMedThreshold++; if (diffMedThreshold >= diffHardThreshold) diffHardThreshold = diffMedThreshold + 1; saveState(); drawSettingsDifficulty(); }
      if (hit(x, y, 260, 88, 22, 18)) { if (diffHardThreshold > diffMedThreshold + 1) diffHardThreshold--; saveState(); drawSettingsDifficulty(); }
      if (hit(x, y, 284, 88, 22, 18)) { diffHardThreshold++; saveState(); drawSettingsDifficulty(); }
      if (hit(x, y, 110, 220, 100, 22)) goTo(SETTINGS);
      return;
    case SETTINGS_CATEGORIES:
      for (let i = 0; i < 4; i++) if (hit(x, y, 30, 48 + i * 38, 260, 30)) { poolToggle(i); saveState(); drawSettingsCategories(); }
      if (hit(x, y, 110, 224, 100, 20)) goTo(SETTINGS);
      return;
    case SETTINGS_SPEED:
      for (let i = 0; i < 3; i++) {
        const bx = 10 + i * 102;
        if (hit(x, y, bx, 44, 96, 22)) { cfg_glyphSpinMs = SPEED_GLYPH[i]; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, bx, 110, 96, 22)) { cfg_revealChars = SPEED_REVEAL[i]; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, bx, 176, 96, 22)) { cfg_transitionPx = SPEED_TRANS[i]; saveState(); drawSettingsSpeed(); }
      }
      if (hit(x, y, 95, 210, 130, 24)) goTo(SETTINGS);
      return;
    case SETTINGS_TIMER:
      if (hit(x, y, 10, 55, 30, 22)) { editDays++; drawSettingsTimer(); }
      if (hit(x, y, 10, 79, 30, 22)) { if (editDays > 0) editDays--; drawSettingsTimer(); }
      if (hit(x, y, 118, 55, 30, 22)) { editHours = (editHours + 1) % 24; drawSettingsTimer(); }
      if (hit(x, y, 118, 79, 30, 22)) { editHours = (editHours + 23) % 24; drawSettingsTimer(); }
      if (hit(x, y, 222, 55, 30, 22)) { editMinutes = (editMinutes + 1) % 60; drawSettingsTimer(); }
      if (hit(x, y, 222, 79, 30, 22)) { editMinutes = (editMinutes + 59) % 60; drawSettingsTimer(); }
      if (hit(x, y, 20, 148, 130, 26)) {
        if (!editDays && !editHours && !editMinutes) editMinutes = 1;
        timerDays = editDays; timerHours = editHours; timerMinutes = editMinutes;
        saveState(); goTo(SETTINGS);
      }
      if (hit(x, y, 170, 148, 130, 26)) goTo(SETTINGS);
      return;
    case SETTINGS_BRIGHT:
      for (let i = 0; i < 3; i++) if (hit(x, y, 20 + i * 100, 100, 80, 36)) { brightnessLevel = i; applyBrightness(); saveState(); drawSettingsBright(); }
      if (hit(x, y, 110, 170, 100, 26)) goTo(SETTINGS);
      return;
    case RESET_CONFIRM:
      if (hit(x, y, 20, 80, 120, 28)) { resetDeck(0, true); goTo(SETTINGS); }
      else if (hit(x, y, 180, 80, 120, 28)) { resetDeck(1, true); goTo(SETTINGS); }
      else if (hit(x, y, 20, 120, 120, 28)) { resetDeck(2, true); goTo(SETTINGS); }
      else if (hit(x, y, 180, 120, 120, 28)) { resetDeck(3, true); goTo(SETTINGS); }
      else if (hit(x, y, 20, 160, 120, 28)) { resetDeck(4, true); timedActive = false; timedPassed = false; saveState(); goTo(SETTINGS); }
      else if (hit(x, y, 180, 160, 120, 28)) {
        for (let i = 0; i < 5; i++) resetDeck(i, true);
        timedActive = false; timedPassed = false; saveState(); goTo(SETTINGS);
      } else if (hit(x, y, 95, 200, 130, 22)) goTo(SETTINGS);
      return;
    case HISTORY_SCREEN:
      for (let i = 0; i < 5; i++) if (hit(x, y, i * 64, 0, 64, 24)) { histViewMenu = i; drawHistoryScreen(); }
      if (hit(x, y, 110, 205, 100, 26)) goTo(SETTINGS);
      return;
  }
}

// ── Wake lock: keep the screen on where the device keeps its backlight on ──
let wakeLock = null;
async function updateWakeLock(want) {
  if (!('wakeLock' in navigator)) return;
  try {
    if (want && !wakeLock && document.visibilityState === 'visible') {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    } else if (!want && wakeLock) { await wakeLock.release(); wakeLock = null; }
  } catch (e) { wakeLock = null; }
}

// ── loop() ────────────────────────────────────────
let lastFrame = 0, lastTimedBgTick = 0, lastTimedTick = 0, lastIdleTick = 0;

function frame() {
  requestAnimationFrame(frame);
  const now = millis();
  if (now - lastFrame < 66) return;    // ~15fps, as on the device
  lastFrame = now;

  const keepAlive = screen === IDLE_ANIM || screen === LYRICS_SCREEN || screen === SONG_SELECT ||
    (timedActive && !timedPaused && (isPrescriptScreen(screen) || screen === MENU_4));
  if (keepAlive) lastTouchTime = now;
  updateWakeLock(keepAlive);

  if (!sleeping && now - lastTouchTime > blSleepMs && !(screen === MENU_4 && timedActive)) setSleeping(true);
  if (sleeping) return;

  if (now - lastTimedBgTick > 1000) { lastTimedBgTick = now; tickTimedBackground(); }
  if (screen === MENU_4 && !_needsTimedRedraw && now - lastTimedTick > 1000) { lastTimedTick = now; tickTimedMenu(); }

  if (_needsTimedRedraw) { _needsTimedRedraw = false; drawTimedMenu(); tft.present(); return; }

  if (screen === IDLE_ANIM && idleActive && now - lastIdleTick > 100) { lastIdleTick = now; tickIdleAnim(); }
  if (screen === LYRICS_SCREEN) tickLyricsScreen();
  if (screen === GAME_PICKER && !gamePickerSettled) tickGamePicker();

  if (isPrescriptScreen(screen) && !idleActive && sleepMs !== SLEEP_NEVER && now - lastInteractionTime > sleepMs) {
    idleActive = true;
    goTo(IDLE_ANIM);
  }
  if (isPrescriptScreen(screen)) tickPrescriptReveal();
  if (screen === MENU_4 && timedActive && timedLineCount > 0) tickTimedReveal();

  if (screen === RESULT) drawResult();
  if (screen === DICE_ROLL) drawDiceRoll();
  if (screen === TOOL_COIN) toolCoinTick();
  if (screen === TOOL_TAROT) toolTarotTick();
  if (screen === TOOL_ORACLE) toolOracleTick();
  if (screen === TOOL_TIMER) toolCDTick();
  if (screen === TOOL_NAMES) toolGenTick();

  tft.present();
}

// ── setup() ───────────────────────────────────────
function setup() {
  tft.init(canvas);
  loadLogo();
  loadState();
  toolsLoad();
  applyBrightness();
  editMinutes = timerMinutes; editHours = timerHours; editDays = timerDays;
  cyberQRInit(pget('qrtx', QR_DEFAULT));
  loopIdx = 0;
  sessionStart = millis();
  lastTouchTime = millis();
  lastInteractionTime = millis();
  goTo(MENU_GAME);
  tft.present();
  requestAnimationFrame(frame);
}

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'hidden') flushState();
  else { lastTouchTime = millis(); updateWakeLock(false); }
});
window.addEventListener('pagehide', flushState);

if ('serviceWorker' in navigator && location.protocol !== 'file:') {
  navigator.serviceWorker.register('sw.js').catch(() => {});
}

setup();
