// =================================================
// prescript.js — the card game (beeper.ino: generators, menus 0-3,
// TIMED menu, PASS/FAIL result animation, rewards, streaks).
// =================================================

// ── Ordinal suffix ────────────────────────────────
function ordinal(n) {
  let suf = 'th';
  const m = n % 100;
  if (m < 11 || m > 13) {
    switch (n % 10) { case 1: suf = 'st'; break; case 2: suf = 'nd'; break; case 3: suf = 'rd'; break; }
  }
  return n + suf;
}

// Pick from a pool using the same random(N) bound as the firmware.
function pick(name) {
  const arr = FW[name];
  const n = Math.min(FW.randomBounds[name] || arr.length, arr.length);
  return arr[random(n)];
}

// ── Generative engines (nyos.dev prescript format) ──
function generateCityPresc(diff) {
  const num = random(1, (diff === 0 ? 20 : 100) + 1);
  const ord = ordinal(num);
  if (diff === 0) {
    return `${pick('cityCondEasy')}, ${pick('cityAct1Easy')} and find the ${ord} ${pick('cityAdjEasy')} ` +
           `${pick('cityNounEasy')} you see and ${pick('cityAct2Easy')}. ${pick('cityCloseEasy')}`;
  } else if (diff === 2) {
    let out = '';
    for (let t = 0; t < 8; t++) {
      out = `${pick('cityCondHard')}, find the ${ord} ${pick('cityAdjHard')} ${pick('cityNounHard')} ` +
            `you see and ${pick('cityAct2Hard')}. ${pick('cityCloseHard')}`;
      if (out.length <= 110) break;
    }
    return out;
  }
  return `${pick('cityCondNorm')}, ${pick('cityAct1Norm')} and find the ${ord} ${pick('cityAdjNorm')} ` +
         `${pick('cityNounNorm')} you see and ${pick('cityAct2Norm')}. ${pick('cityCloseNorm')}`;
}
function generateGamePresc(diff) {
  const t = ['Easy', 'Norm', 'Hard'][diff];
  return `${pick('gameCond' + t)}, ${pick('gameObj' + t)}. ${pick('gameClose' + t)}`;
}
function generateConPresc(diff) {
  const t = ['Easy', 'Norm', 'Hard'][diff];
  return `${pick('conCond' + t)}, ${pick('conAct1' + t)} and ${pick('conAct2' + t)}. ${pick('conClose' + t)}`;
}
function generateTaskPresc(diff) {
  const t = ['Easy', 'Norm', 'Hard'][diff];
  return `${pick('taskCond' + t)}, ${pick('taskObj' + t)}. ${pick('taskClose' + t)}`;
}
function generateFor(pool, diff) {
  switch (pool) {
    case 0: return generateGamePresc(diff);
    case 1: return generateCityPresc(diff);
    case 2: return generateConPresc(diff);
    default: return generateTaskPresc(diff);
  }
}
const RARE_SETS = ['rareGame', 'rareCity', 'rareCon', 'rareTask'];

// ── Per-menu card state ───────────────────────────
let current = '', revealIndex = 0, lines = [], lineCount = 0, logoBottomY = 0;
let isRareEvent = false;
const menuCurrent = ['', '', '', ''], menuLines = [[], [], [], []];
const menuIsRare = [false, false, false, false], menuHasPrescript = [false, false, false, false];

function screenToMenuIdx(s) {
  switch (s) { case MENU_GAME: return 0; case MENU_1: return 1; case MENU_2: return 2; case MENU_4: return 4; default: return 3; }
}
function prescriptScreenName(s) {
  return { [MENU_GAME]: 'GAME', [MENU_1]: 'CITY', [MENU_2]: 'CON', [MENU_3]: 'TASK', [MENU_4]: 'TIME' }[s] || '';
}

function drawLogo() {
  tft.pushImage(0, 0, 320, 96, logoBuf);
  tft.fillRect(0, 88, 320, 8, BG_COLOR);
  logoBottomY = 88 + 4;
}

function drawSessionTimer() {
  const e = ((millis() - sessionStart) / 1000) | 0;
  const buf = pad2((e / 3600) | 0) + ':' + pad2(((e % 3600) / 60) | 0);
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(50, 70, 100), BG_COLOR);
  tft.setCursor((320 - buf.length * 6) / 2, 220);
  tft.print(buf);
}

function fmtRemain(remSec) {
  const rh = (remSec / 3600) | 0, rm = ((remSec % 3600) / 60) | 0, rs = remSec % 60;
  return rh > 0 ? `${rh}:${pad2(rm)}:${pad2(rs)}` : `${rm}:${pad2(rs)}`;
}

// TIMED countdown, top-right of every prescript screen (with 3-blink on reset).
function drawTimedCountdown(bg) {
  const x = 240, y = 6, w = 76;
  if (timerBlinkCount > 0 && millis() - timerBlinkLast > 200) {
    timerBlinkLast = millis(); timerBlinkOn = !timerBlinkOn; timerBlinkCount--;
  }
  if (timerBlinkCount > 0 && !timerBlinkOn) { tft.fillRect(x - 2, y - 1, w + 4, 10, bg); return; }
  if (!timedActive) {
    tft.setTextSize(1); tft.setTextColor(tft.color565(40, 55, 75), bg);
    tft.setCursor(x, y); tft.print('--:--');
    return;
  }
  const dur = timerDurationMs();
  const elapsed = timedPaused ? dur - timedPauseRem : millis() - timedStart;
  const remSec = elapsed < dur ? ((dur - elapsed) / 1000) | 0 : 0;
  const tbuf = fmtRemain(remSec);
  let col;
  if (timedPassed) col = tft.color565(0, 130, 100);
  else if (remSec < dur / 4000) col = tft.color565(220, 80, 40);
  else col = tft.color565(0, 180, 140);
  if (timedPaused) col = tft.color565(180, 140, 40);
  tft.setTextSize(1);
  const tx = 316 - tbuf.length * 6;
  tft.fillRect(tx - 2, y - 1, 316 - tx + 4, 10, bg);
  tft.setTextColor(col, bg);
  tft.setCursor(tx, y); tft.print(tbuf);
}

function totalCharsOf(ls) { let t = 0; for (const l of ls) t += l.length + 1; return t; }

// Generate a new card for the current menu (only after pass/fail or first visit).
function loadPrescript() {
  let m = screenToMenuIdx(screen); if (m > 3) m = 3;
  const diff = menuDiffFor(m);
  const rare = diff > 0 && random(20) === 0;
  menuIsRare[m] = rare;
  if (rare) {
    const rs = FW[RARE_SETS[m]];
    menuCurrent[m] = '* ' + rs[random(rs.length)] + ' *';
    histPush(m, -1, menuCurrent[m]);
  } else {
    const g = generateFor(m, diff).toUpperCase();
    menuCurrent[m] = '_' + g + '_';
    histPush(m, 0, g);
  }
  menuLines[m] = wrapText(menuCurrent[m], 24);
  menuHasPrescript[m] = true;
  markStateDirty();
  current = menuCurrent[m]; revealIndex = 0;
  lines = menuLines[m].slice(); lineCount = lines.length;
  isRareEvent = rare;
  tft.fillScreen(BG_COLOR);
  drawLogo();
  drawPrescript();
}

// Redisplay the stored card (re-scrambles on entry).
function showStoredPrescript() {
  let m = screenToMenuIdx(screen); if (m > 3) m = 3;
  if (!menuHasPrescript[m]) { loadPrescript(); return; }
  current = menuCurrent[m]; revealIndex = 0;
  lines = menuLines[m].slice(); lineCount = lines.length;
  isRareEvent = menuIsRare[m];
  tft.fillScreen(BG_COLOR);
  drawLogo();
  drawPrescript();
}
function cyclePrescript(m) { menuHasPrescript[m] = false; markStateDirty(); }

function drawPrescript() {
  tft.setTextSize(1);
  const label = screen === MENU_1 ? 'CITY' : screen === MENU_2 ? 'CON' : screen === MENU_3 ? 'TASK' : 'GAME';
  const mi = screen === MENU_GAME ? 0 : screen === MENU_1 ? 1 : screen === MENU_2 ? 2 : 3;

  if (isRareEvent) {
    tft.setTextColor(RARE_COLOR, BG_COLOR);
    tft.setCursor(6, 6); tft.print(label);
    const rTag = '[INDEX]';
    tft.setCursor(320 - rTag.length * 6 - 4, 6); tft.print(rTag);
    drawTimedCountdown(BG_COLOR);
  } else {
    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(6, 6); tft.print(label);
    const diff = menuDiffFor(mi), passes = passCount[mi];
    let diffBuf;
    if (diff === 0) { diffBuf = `[EASY ${passes}/${diffMedThreshold}]`; tft.setTextColor(GOOD_COLOR, BG_COLOR); }
    else if (diff === 1) { diffBuf = `[NRM ${passes}/${diffHardThreshold}]`; tft.setTextColor(TEXT_COLOR, BG_COLOR); }
    else { diffBuf = `[HARD ${passes}]`; tft.setTextColor(tft.color565(220, 80, 40), BG_COLOR); }
    tft.setCursor(6 + label.length * 6 + 6, 6); tft.print(diffBuf);
    drawTimedCountdown(BG_COLOR);
    const sCol = (streak[mi] > 0 && streak[mi] === bestStreak[mi]) ? GOOD_COLOR : tft.color565(80, 100, 130);
    tft.setTextColor(sCol, BG_COLOR);
    tft.setCursor(6, 16); tft.print('s:' + streak[mi]);
  }

  // Swipe hints
  const prevIdx = (loopIdx + loopLen - 1) % loopLen, nextIdx = (loopIdx + 1) % loopLen;
  tft.setTextSize(1); tft.setTextColor(tft.color565(40, 55, 80), BG_COLOR);
  tft.setCursor(2, 228); tft.print('< ' + prescriptScreenName(loopScreens[prevIdx]));
  const rightBuf = prescriptScreenName(loopScreens[nextIdx]) + ' >';
  tft.setCursor(318 - rightBuf.length * 6, 228); tft.print(rightBuf);

  const txtCol = isRareEvent ? RARE_COLOR : TEXT_COLOR;
  tft.setTextSize(2);
  const cw = 12, ch = 18, availTop = logoBottomY + 4, availBot = 214;
  const totalChars = totalCharsOf(lines);
  const fully = revealIndex >= totalChars;
  const textY = availTop + max(0, ((availBot - availTop - lineCount * ch) / 2) | 0);
  let idx2 = 0;
  for (let l = 0; l < lineCount; l++) {
    const s = lines[l], x = ((320 - s.length * cw) / 2) | 0, y = textY + l * ch;
    if (y + ch > availBot) break;
    for (let i = 0; i < s.length; i++) {
      if (fully || idx2 < revealIndex) tft.setTextColor(txtCol, BG_COLOR), tft.setCursor(x + i * cw, y), tft.print(s[i]);
      else tft.setTextColor(tft.color565(30, 50, 80), BG_COLOR), tft.setCursor(x + i * cw, y), tft.print(scramble());
      idx2++;
    }
    idx2++;
  }
  tft.setTextSize(2); tft.setTextColor(txtCol, BG_COLOR);
  tft.setCursor(60, 216); tft.print('PASS');
  tft.setCursor(220, 216); tft.print('FAIL');
  drawSessionTimer();
}

// Per-frame reveal for menus 0-3 (from loop()).
let lastRevealTick = 0;
function tickPrescriptReveal() {
  const totalChars = totalCharsOf(lines);
  const wasComplete = revealIndex >= totalChars;
  if (millis() - lastRevealTick >= cfg_glyphSpinMs) {
    lastRevealTick = millis();
    const prev = revealIndex;
    for (let r = 0; r < cfg_revealChars; r++) if (revealIndex < totalChars) revealIndex++;
    if (revealIndex >= totalChars && prev < totalChars)
      tft.fillRect(0, logoBottomY + 4, 320, 214 - (logoBottomY + 4), BG_COLOR);
  }
  const nowComplete = revealIndex >= totalChars;
  if (!nowComplete || !wasComplete) drawPrescript();
  if (timedActive) drawTimedCountdown(BG_COLOR);
}

// =================================================
// TIMED MENU
// =================================================
const TM_BG = tft.color565(8, 18, 20), TM_HEADER = tft.color565(12, 45, 45),
  TM_TEXT = tft.color565(255, 165, 80), TM_DIM = tft.color565(40, 90, 80),
  TM_BAR_ACT = tft.color565(0, 200, 160), TM_BAR_PASS = tft.color565(0, 110, 90),
  TM_BAR_URG = tft.color565(220, 80, 40), TM_BAR_RAIL = tft.color565(20, 45, 42),
  TM_WARN = tft.color565(200, 120, 20);

function timedAssignNew() {
  const enabled = [];
  for (let i = 0; i < 4; i++) if (poolEnabled(i)) enabled.push(i);
  if (!enabled.length) enabled.push(0);
  const pool = enabled[random(enabled.length)];
  timedPool = pool;
  const diff = menuDiffFor(4);
  isRareEvent = diff > 0 && random(20) === 0;
  timedIsRare = isRareEvent;
  if (isRareEvent) {
    const rs = FW[RARE_SETS[pool]];
    timedCurrent = rs[random(rs.length)];
    timedCardIndex = -1;
    histPush(4, -1, timedCurrent);
  } else {
    timedCurrent = generateFor(pool, diff).toUpperCase();
    timedCardIndex = 0;
    histPush(4, 0, timedCurrent);
  }
  timedStart = millis();
  timedActive = true; timedPassed = false; timedPaused = false;
  timedRevealIndex = 0;
  timerBlinkCount = 6; timerBlinkLast = millis(); timerBlinkOn = true;
  timedLines = wrapText(timedIsRare ? '* ' + timedCurrent + ' *' : '_' + timedCurrent + '_', 24);
  timedLineCount = timedLines.length;
  markStateDirty();
}

function tmBarCalc(elapsed, dur, passed) {
  let frac, col;
  if (passed) { frac = dur > 0 ? (dur - min(elapsed, dur)) / dur : 0; col = TM_BAR_PASS; }
  else { frac = dur > 0 ? 1 - min(elapsed, dur) / dur : 0; col = frac < 0.25 ? TM_BAR_URG : TM_BAR_ACT; }
  return { frac: constrain(frac, 0, 1), col };
}

function drawSessionCorner(bg) {
  const se = ((millis() - sessionStart) / 1000) | 0;
  const sb = pad2((se / 3600) | 0) + ':' + pad2(((se % 3600) / 60) | 0);
  tft.setTextSize(1); tft.setTextColor(TM_DIM, bg);
  tft.setCursor(320 - sb.length * 6 - 4, 218); tft.print(sb);
}

function drawTimedText(sY, availBot) {
  const tc = timedIsRare ? RARE_COLOR : (timedPaused ? TM_DIM : TM_TEXT);
  const cw = 12, ch = 18;
  tft.setTextSize(2);
  let idx2 = 0;
  for (let l = 0; l < timedLineCount; l++) {
    const s = timedLines[l], lx = ((320 - s.length * cw) / 2) | 0, ly = sY + l * ch;
    if (ly + ch > availBot) break;
    for (let i = 0; i < s.length; i++) {
      const locked = idx2 < timedRevealIndex;
      tft.setTextColor(locked ? (timedPassed ? TM_BAR_PASS : tc) : tft.color565(30, 60, 70), TM_BG);
      tft.setCursor(lx + i * cw, ly); tft.print(locked ? s[i] : scramble());
      idx2++;
    }
    idx2++;
  }
}

function drawTimedMenu() {
  tft.fillScreen(TM_BG);
  const dur = timerDurationMs();
  const elapsed = timedPaused ? dur - timedPauseRem : timedActive ? millis() - timedStart : 0;

  tft.fillRect(0, 0, 320, 14, TM_HEADER);
  tft.setTextSize(1); tft.setTextColor(TM_TEXT, TM_HEADER);
  tft.setCursor(4, 3); tft.print('TIMED');
  {
    const diff = menuDiffFor(4), passes = passCount[4];
    let buf, col;
    if (diff === 0) { buf = `[E ${passes}/${diffMedThreshold}]`; col = tft.color565(0, 160, 80); }
    else if (diff === 1) { buf = `[N ${passes}/${diffHardThreshold}]`; col = TM_DIM; }
    else { buf = `[H ${passes}]`; col = tft.color565(220, 80, 40); }
    tft.setTextColor(col, TM_HEADER);
    tft.setCursor(4 + 5 * 6 + 3, 3); tft.print(buf);
  }
  if (timedActive) {
    const theme = ['GAME', 'CITY', 'CON', 'TASK'][timedPool] || 'TIMED';
    tft.setTextColor(TM_DIM, TM_HEADER);
    tft.setCursor(((320 - theme.length * 6) / 2) | 0, 3); tft.print(theme);
  }
  if (timedPaused) {
    tft.setTextColor(TM_WARN, TM_HEADER);
    tft.setCursor(320 - 7 * 6 - 4, 3); tft.print('[PAUSE]');
  } else if (timedIsRare) {
    tft.setTextColor(RARE_COLOR, TM_HEADER);
    tft.setCursor(((320 - 9 * 6) / 2) | 0, 3); tft.print('[INDEX]');
  } else if (timedActive) {
    const remSec = dur > elapsed ? ((dur - elapsed) / 1000) | 0 : 0;
    const tbuf = fmtRemain(remSec);
    const { col } = tmBarCalc(elapsed, dur, timedPassed);
    tft.setTextColor(timedPassed ? TM_BAR_PASS : col, TM_HEADER);
    tft.setCursor(320 - tbuf.length * 6 - 4, 3); tft.print(tbuf);
  }
  tft.drawFastHLine(0, 14, 320, TM_TEXT);

  tft.fillRect(0, 15, 320, 10, TM_BG);
  const pf = `PASS: ${passCount[4]}   FAIL: ${failCount[4]}`;
  tft.setTextColor(TM_DIM, TM_BG);
  tft.setCursor(((320 - pf.length * 6) / 2) | 0, 16); tft.print(pf);

  tft.pushImage(0, 26, 320, 79, logoBuf, 9 * 320);
  logoBottomY = 26 + 79 + 2;
  const cY = logoBottomY;

  if (!timedActive) {
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    tft.setCursor(((320 - 20 * 6) / 2) | 0, cY + 18); tft.print('TAP TO BEGIN TIMER');
    const tot = (timerDurationMs() / 1000) | 0;
    const dbuf = tot >= 3600 ? `INTERVAL: ${(tot / 3600) | 0}h ${((tot % 3600) / 60) | 0}m` : `INTERVAL: ${(tot / 60) | 0}m`;
    tft.setCursor(((320 - dbuf.length * 6) / 2) | 0, cY + 32); tft.print(dbuf);
    drawSessionCorner(TM_BG);
    return;
  }

  const bY = 205, bH = 8, bW = 300, bX = 10;
  let { frac, col: bc } = tmBarCalc(elapsed, dur, timedPassed);
  if (timedPaused) bc = TM_WARN;
  tft.fillRect(bX, bY - 1, bW, bH + 1, TM_BAR_RAIL);
  tft.fillRect(bX, bY, (bW * frac) | 0, bH, bc);
  const be = bX + ((bW * frac) | 0);
  if (be > bX && be < bX + bW) tft.fillRect(be - 1, bY, 2, bH, bc);

  const availBot = 204;
  const sY = cY + max(0, ((availBot - cY - timedLineCount * 18) / 2) | 0);
  drawTimedText(sY, availBot);

  tft.setTextSize(2);
  if (!timedPassed) {
    tft.setTextColor(timedPaused ? TM_DIM : TM_BAR_ACT, TM_BG); tft.setCursor(30, 216); tft.print('PASS');
    tft.setTextColor(timedPaused ? TM_DIM : TM_BAR_URG, TM_BG); tft.setCursor(214, 216); tft.print('FAIL');
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    const pl = timedPaused ? 'RESUME' : 'PAUSE';
    tft.setCursor(((320 - pl.length * 6) / 2) | 0, 222); tft.print(pl);
  } else {
    const ws = dur > elapsed ? (((dur - elapsed) / 1000) | 0) + 1 : 0;
    const wbuf = `NEXT IN ${(ws / 60) | 0}m ${ws % 60}s`;
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    tft.setCursor(((320 - wbuf.length * 6) / 2) | 0, 222); tft.print(wbuf);
  }
  drawSessionCorner(TM_BG);
}

// Runs every second on any screen: expiry records a fail if not passed.
let _needsTimedRedraw = false;
function tickTimedBackground() {
  if (!timedActive || timedPaused) return;
  const dur = timerDurationMs();
  if (millis() - timedStart < dur) return;
  if (!timedPassed) { failCount[4]++; streak[4] = 0; }
  timedAssignNew();
  if (screen === MENU_4) _needsTimedRedraw = true;
}

// Lightweight per-second update while the TIMED screen is visible.
function tickTimedMenu() {
  if (!timedActive || timedPaused) return;
  const dur = timerDurationMs(), elapsed = millis() - timedStart;
  if (elapsed >= dur) return;
  const bY = 205, bH = 8, bW = 300, bX = 10;
  const { frac, col: bc } = tmBarCalc(elapsed, dur, timedPassed);
  tft.fillRect(bX, bY - 1, bW, bH + 1, TM_BAR_RAIL);
  tft.fillRect(bX, bY, (bW * frac) | 0, bH, bc);
  const be = bX + ((bW * frac) | 0);
  if (be > bX && be < bX + bW) tft.fillRect(be - 1, bY, 2, bH, bc);
  if (!timedIsRare) {
    const tbuf = fmtRemain(dur > elapsed ? ((dur - elapsed) / 1000) | 0 : 0);
    tft.setTextSize(1);
    tft.fillRect(180, 0, 136, 14, TM_HEADER);
    tft.setTextColor(timedPassed ? TM_BAR_PASS : bc, TM_HEADER);
    tft.setCursor(320 - tbuf.length * 6 - 4, 3); tft.print(tbuf);
    tft.drawFastHLine(180, 14, 140, TM_TEXT);
  }
  tft.fillRect(0, 212, 320, 28, TM_BG);
  tft.setTextSize(2);
  if (!timedPassed) {
    tft.setTextColor(TM_BAR_ACT, TM_BG); tft.setCursor(30, 216); tft.print('PASS');
    tft.setTextColor(TM_BAR_URG, TM_BG); tft.setCursor(214, 216); tft.print('FAIL');
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    tft.setCursor(((320 - 5 * 6) / 2) | 0, 222); tft.print('PAUSE');
  } else {
    const ws = dur > elapsed ? (((dur - elapsed) / 1000) | 0) + 1 : 0;
    const wbuf = `NEXT IN ${(ws / 60) | 0}m ${ws % 60}s`;
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    tft.setCursor(((320 - wbuf.length * 6) / 2) | 0, 222); tft.print(wbuf);
  }
  drawSessionCorner(TM_BG);
}

// Per-frame scramble reveal on the TIMED screen.
let lastTimedRevealTick = 0;
function tickTimedReveal() {
  const total = totalCharsOf(timedLines);
  const wasComplete = timedRevealIndex >= total;
  if (millis() - lastTimedRevealTick >= cfg_glyphSpinMs) {
    lastTimedRevealTick = millis();
    for (let r = 0; r < cfg_revealChars; r++) if (timedRevealIndex < total) timedRevealIndex++;
  }
  const nowComplete = timedRevealIndex >= total;
  if (nowComplete && wasComplete) return;
  const availBot = 204, cY = logoBottomY;
  const sY = cY + max(0, ((availBot - cY - timedLineCount * 18) / 2) | 0);
  // The loop's reveal always uses the un-paused colour (as on device).
  const savedPaused = timedPaused; timedPaused = false;
  drawTimedText(sY, availBot);
  timedPaused = savedPaused;
}

// Timed screen taps.
function timedTap(x, y) {
  if (!timedActive) { timedAssignNew(); drawTimedMenu(); return; }
  if (timedPassed) return;
  if (y > 213 && x < 130 && !timedPaused) {
    checkRewards(4, true);
    timedPassed = true;
    markStateDirty();
    showResult(true);
  } else if (y > 213 && x > 200 && !timedPaused) {
    checkRewards(4, false);
    timedStart = millis(); timedPaused = false; timedPauseRem = 0;
    timedRevealIndex = 0;
    markStateDirty();
    showResult(false);
  } else if (y > 210) {
    if (timedPaused) { timedStart = millis() - (timerDurationMs() - timedPauseRem); timedPaused = false; }
    else { timedPauseRem = timerDurationMs() - (millis() - timedStart); timedPaused = true; }
    markStateDirty();
    drawTimedMenu();
  }
}

// =================================================
// REWARDS + RESULT SCREEN
// =================================================
const REWARD_SETS = [['coreRewards', 'corePunishments'], ['cityRewards', 'cityPunishments'],
                     ['conRewards', 'conPunishments'], ['taskRewards', 'taskPunishments']];

function checkRewards(m, pass) {
  if (pass) {
    passCount[m]++; streak[m]++; newBest = false;
    if (streak[m] > bestStreak[m]) { bestStreak[m] = streak[m]; newBest = true; }
  } else { failCount[m]++; streak[m] = 0; newBest = false; }
  rewardText = '';
  if (m === 4) { markStateDirty(); return; }
  const ri = min(m, 3), set = FW[REWARD_SETS[ri][pass ? 0 : 1]];
  const cnt = pass ? passCount[m] : failCount[m];
  if (cnt % 5 === 0 && cnt > 0) rewardText = set[(cnt / 5 - 1) % set.length];
  if (rewardText) rewardActive = true;
  markStateDirty();
}

const RESULT_FILL = 0, RESULT_REVEAL = 1, RESULT_HOLD = 2, RESULT_REWARD_REVEAL = 3,
      RESULT_REWARD_HOLD = 4, RESULT_DRAIN = 5;
let resultPass = false, resultState = RESULT_FILL, resultFillY = 240, resultRevealIndex = 0,
    rewardRevealIndex = 0, resultInitialized = false, resultFullyDrawn = false, resultText = '',
    lastAnimFrame = 0, lastReveal = 0;

function drawResultWord(color) {
  tft.setTextSize(3); tft.setTextColor(TEXT_COLOR, color);
  tft.setCursor(((320 - resultText.length * 18) / 2) | 0, 90); tft.print(resultText);
}

function drawRewardReveal(color, hold) {
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, color);
  const cw = 6, lineH = 14;
  const rl = wrapText(rewardText, 50);
  const total = totalCharsOf(rl);
  const availTop = 120, availBot = 235;
  const startY = availTop + max(0, ((availBot - availTop - rl.length * lineH) / 2) | 0);
  let idx = 0;
  for (let l = 0; l < rl.length; l++) {
    const s = rl[l], x = ((320 - s.length * cw) / 2) | 0, y = startY + l * lineH;
    if (y + lineH > availBot) break;
    for (let i = 0; i < s.length; i++) {
      tft.setCursor(x + i * cw, y);
      tft.print(hold ? s[i] : (idx < rewardRevealIndex ? s[i] : scramble()));
      idx++;
    }
    idx++;
  }
  if (!hold && millis() - lastReveal > 45) { rewardRevealIndex++; lastReveal = millis(); }
  return rewardRevealIndex >= total;
}

function drawStreakBanner(color, m) {
  const buf = `STREAK ${streak[m]}  BEST ${bestStreak[m]}`;
  tft.setTextSize(1); tft.setTextColor(GOOD_COLOR, color);
  tft.setCursor(((320 - buf.length * 6) / 2) | 0, 170); tft.print(buf);
  if (newBest) {
    tft.setTextColor(RARE_COLOR, color);
    tft.setCursor(((320 - 9 * 6) / 2) | 0, 182); tft.print('NEW BEST!');
  }
}

function drawResult() {
  if (millis() - lastAnimFrame < 30) return;
  lastAnimFrame = millis();
  const color = resultPass ? PASS_COLOR : FAIL_COLOR;
  if (!resultInitialized) {
    resultRevealIndex = 0; rewardRevealIndex = 0;
    resultText = resultPass ? '_CleAr_' : '_FaIL_';
    resultFillY = resultPass ? 240 : 0;
    resultState = RESULT_FILL;
    resultInitialized = true; resultFullyDrawn = false;
    tft.fillScreen(BG_COLOR);
    // The TIMED screen scores its pass/fail in its own tap handler.
    if (previousScreen !== MENU_4) checkRewards(screenToMenuIdx(previousScreen), resultPass);
  }
  if (resultState === RESULT_FILL) {
    const dir = resultPass ? -cfg_transitionPx : cfg_transitionPx;
    const oldY = resultFillY;
    resultFillY += dir;
    if (resultFillY <= 0) { resultFillY = 0; resultState = RESULT_REVEAL; }
    if (resultFillY >= 240) { resultFillY = 240; resultState = RESULT_REVEAL; }
    tft.fillRect(0, min(oldY, resultFillY), 320, abs(oldY - resultFillY), color);
  } else if (resultState === RESULT_REVEAL) {
    if (!resultFullyDrawn) { tft.fillScreen(color); resultFullyDrawn = true; }
    tft.setTextSize(3); tft.setTextColor(TEXT_COLOR, color);
    const cw = 18, x = ((320 - resultText.length * cw) / 2) | 0, y = 90;
    tft.fillRect(x - 10, y - 10, resultText.length * cw + 20, 40, color);
    for (let i = 0; i < resultText.length; i++) {
      tft.setCursor(x + i * cw, y); tft.print(i < resultRevealIndex ? resultText[i] : scramble());
    }
    if (millis() - lastReveal > RESULT_REVEAL_MS) { resultRevealIndex++; lastReveal = millis(); }
    if (resultRevealIndex >= resultText.length) resultState = RESULT_HOLD;
  } else if (resultState === RESULT_HOLD) {
    drawResultWord(color);
    if (resultPass) drawStreakBanner(color, screenToMenuIdx(previousScreen));
  } else if (resultState === RESULT_REWARD_REVEAL) {
    drawResultWord(color);
    if (drawRewardReveal(color, false)) resultState = RESULT_REWARD_HOLD;
  } else if (resultState === RESULT_REWARD_HOLD) {
    drawResultWord(color); drawRewardReveal(color, true);
  } else if (resultState === RESULT_DRAIN) {
    const dir = resultPass ? cfg_transitionPx : -cfg_transitionPx;
    const oldY = resultFillY;
    resultFillY += dir;
    const done = resultPass ? resultFillY >= 240 : resultFillY <= 0;
    if (done) {
      resultInitialized = false; resultFullyDrawn = false;
      const rm = screenToMenuIdx(previousScreen);
      if (rm >= 0 && rm <= 3) cyclePrescript(rm);
      goTo(previousScreen);
      return;
    }
    tft.fillRect(0, min(oldY, resultFillY), 320, abs(oldY - resultFillY), BG_COLOR);
  }
}

function showResult(pass) {
  resultPass = pass; resultInitialized = false;
  lastAnimFrame = 0; lastReveal = 0;
  goTo(RESULT);
}

function resultTap() {
  if (resultState === RESULT_HOLD) {
    if (previousScreen === MENU_4 && resultPass) {
      resultInitialized = false; resultFullyDrawn = false;
      goTo(MENU_4);
    } else if (rewardActive) {
      rewardRevealIndex = 0; resultState = RESULT_REWARD_REVEAL; rewardActive = false;
    } else {
      resultState = RESULT_DRAIN;
      resultFillY = resultPass ? 0 : 240;
    }
  } else if (resultState === RESULT_REWARD_HOLD) {
    resultState = RESULT_DRAIN;
    resultFillY = resultPass ? 0 : 240;
  }
}
