// =================================================
// tools.js — coin flip, tarot, oracle, countdown timer,
// word generator, cipher wheel (tools.h).
// =================================================

const T_BG = tft.color565(15, 15, 15), T_TEXT = tft.color565(153, 202, 255), T_DIM = tft.color565(60, 70, 90),
  T_GOLD = tft.color565(210, 160, 20), T_GOOD = tft.color565(0, 180, 80), T_BAD = tft.color565(200, 30, 30),
  T_HEAD = tft.color565(20, 30, 55);
let toolLastReveal = 0;

function toolHeader(title, col = T_TEXT, len = title.length) {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(col, T_HEAD);
  tft.setCursor(((320 - len * 6) / 2) | 0, 4); tft.print(title);
}

// ── 1. Coin flip ──────────────────────────────────
const COIN_IDLE = 0, COIN_SPIN = 1, COIN_RESULT = 2;
let coinState = COIN_IDLE, coinResult = false, coinFrame = 0, coinLastF = 0;

function toolCoinDraw() {
  toolHeader('COIN FLIP', T_TEXT, 9);
  if (coinState === COIN_IDLE) {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - 14 * 6) / 2) | 0, 80); tft.print('TAP TO FLIP');
    tft.drawCircle(160, 130, 60, T_DIM);
    tft.drawCircle(160, 130, 56, tft.color565(40, 50, 70));
    tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(160 - 12, 118); tft.print('?');
  } else if (coinState === COIN_SPIN) {
    const ph = coinFrame % 8, w = ph < 4 ? ph * 15 + 5 : (8 - ph) * 15 + 5;
    tft.fillRect(0, 90, 320, 90, T_BG);
    tft.drawEllipse(160, 135, w, 55, T_DIM);
    tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(160 - 12, 122); tft.print(FW.COIN_FACES[ph % 2]);
  } else {
    const col = coinResult ? T_GOOD : T_GOLD;
    tft.drawCircle(160, 130, 60, col);
    tft.drawCircle(160, 130, 56, tft.color565(40, 50, 70));
    tft.setTextSize(4); tft.setTextColor(col, T_BG);
    tft.setCursor(160 - 12, 118); tft.print(coinResult ? 'H' : 'T');
    const word = coinResult ? 'HEADS' : 'TAILS';
    tft.setTextSize(1); tft.setTextColor(col, T_BG);
    tft.setCursor(((320 - word.length * 6) / 2) | 0, 200); tft.print(word);
  }
  drawButton(95, 210, 130, 24, 'BACK');
}
function toolCoinTick() {
  if (coinState !== COIN_SPIN || millis() - coinLastF < 60) return;
  coinLastF = millis();
  coinFrame++;
  if (coinFrame >= 24) { coinState = COIN_RESULT; toolCoinDraw(); return; }
  tft.fillRect(60, 75, 200, 125, T_BG);
  const ph = coinFrame % 8, w = ph < 4 ? ph * 15 + 5 : (8 - ph) * 15 + 5;
  tft.drawEllipse(160, 135, w, 55, T_DIM);
  tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(160 - 12, 114); tft.print(FW.COIN_FACES[ph % 2]);
}
function toolCoinTap() {
  if (coinState === COIN_SPIN) return;
  coinState = COIN_SPIN; coinResult = random(2) === 1; coinFrame = 0; coinLastF = millis();
  toolCoinDraw();
}

// ── 2. Tarot (22 Major Arcana, persisted no-repeat deck) ──
const tarotDeck = new Array(22).fill(0);
let tarotPos = 22, tarotCurrent = -1, tarotRevealIdx = 0, tarotRevealTotal = 0, tarotRevealing = false, tarotLastRev = 0;
let tarotNameLines = [], tarotDescLines = [], tarotTotalNameChars = 0;
const tarotHistory = [];
let tarotHistScroll = 0;

function tarotSave() { prefs.trtd = tarotDeck.slice(); prefs.trtp = tarotPos; prefsCommit(); }
function tarotShuffle() {
  for (let i = 0; i < 22; i++) tarotDeck[i] = i;
  for (let i = 21; i > 0; i--) { const j = random(i + 1); [tarotDeck[i], tarotDeck[j]] = [tarotDeck[j], tarotDeck[i]]; }
  tarotPos = 0;
  tarotSave();
}
function toolsLoad() {
  if ('trtp' in prefs && Array.isArray(prefs.trtd)) {
    for (let i = 0; i < 22; i++) tarotDeck[i] = prefs.trtd[i] | 0;
    tarotPos = prefs.trtp;
  } else tarotPos = 22;
}
function tarotPrecompute() {
  const c = FW.TAROT[tarotCurrent];
  tarotNameLines = wrapText(c.name, 24);
  tarotDescLines = wrapText(c.desc, 24);
  tarotTotalNameChars = tarotNameLines.reduce((a, l) => a + l.length, 0);
  tarotRevealTotal = tarotTotalNameChars + totalCharsOf(tarotDescLines);
}
function tarotDrawText() {
  if (tarotCurrent < 0) return;
  const cw = 12, ch = 18;
  let gi = 0;
  tft.setTextSize(2);
  tarotNameLines.forEach((s, l) => {
    const x = ((320 - s.length * cw) / 2) | 0, y = 20 + l * ch;
    for (let i = 0; i < s.length; i++) {
      tft.setTextColor(T_GOLD, T_BG);
      tft.setCursor(x + i * cw, y); tft.print(gi < tarotRevealIdx ? s[i] : scramble());
      gi++;
    }
  });
  const sepY = 20 + tarotNameLines.length * ch + 3;
  if (gi <= tarotRevealIdx) tft.drawFastHLine(20, sepY, 280, T_GOLD);
  let di = 0;
  for (let l = 0; l < tarotDescLines.length; l++) {
    const s = tarotDescLines[l], y = sepY + 6 + l * ch;
    if (y + ch > 218) break;
    const x = ((320 - s.length * cw) / 2) | 0;
    for (let i = 0; i < s.length; i++) {
      tft.setTextColor(T_TEXT, T_BG);
      tft.setCursor(x + i * cw, y);
      tft.print(tarotTotalNameChars + di + i < tarotRevealIdx ? s[i] : scramble());
    }
    di += s.length + 1;
  }
}
function tarotDrawHistory() {
  toolHeader('CARD HISTORY', T_GOLD, 14);
  const cnt = tarotHistory.length + ' CARDS';
  tft.setTextColor(T_DIM, T_HEAD);
  tft.setCursor(320 - cnt.length * 6 - 4, 4); tft.print(cnt);
  drawButton(4, 18, 70, 20, 'BACK');
  drawButton(88, 18, 144, 20, 'CLEAR HISTORY');
  drawButton(246, 18, 36, 20, 'UP');
  drawButton(284, 18, 36, 20, 'DN');
  tft.drawFastHLine(0, 40, 320, tft.color565(30, 30, 50));
  if (!tarotHistory.length) {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - 16 * 6) / 2) | 0, 120); tft.print('NO CARDS DRAWN YET');
    return;
  }
  const maxRows = 4, rowH = 46, contentY = 42, pageStart = tarotHistScroll - 1;
  for (let i = 0; i < maxRows; i++) {
    const entry = tarotHistory.length - 1 - (pageStart + i);
    if (entry < 0) break;
    const ci = tarotHistory[entry], hy = contentY + i * rowH;
    if (hy + rowH > 234) break;
    if (i > 0) tft.drawFastHLine(4, hy - 2, 312, tft.color565(25, 25, 40));
    tft.setTextSize(1); tft.setTextColor(T_GOLD, T_BG);
    tft.setCursor(6, hy + 1); tft.print(FW.TAROT[ci].name);
    tft.setTextColor(T_DIM, T_BG);
    wrapText(FW.TAROT[ci].desc, 50).forEach((d, k) => {
      const ly = hy + 12 + k * 9;
      if (ly + 8 > hy + rowH) return;
      tft.setCursor(6, ly); tft.print(d);
    });
  }
  const shown = min(maxRows, tarotHistory.length - pageStart);
  const pg = `${pageStart + 1}-${min(pageStart + shown, tarotHistory.length)} of ${tarotHistory.length}`;
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(((320 - pg.length * 6) / 2) | 0, 228); tft.print(pg);
}
function toolTarotDraw() {
  toolHeader('TAROT DRAW', T_GOLD, 11);
  const rem = (22 - tarotPos) + '/22';
  tft.setTextColor(T_DIM, T_HEAD);
  tft.setCursor(320 - rem.length * 6 - 4, 4); tft.print(rem);
  if (tarotHistScroll > 0) { tarotDrawHistory(); return; }
  if (tarotCurrent < 0) {
    tft.drawRect(30, 22, 260, 178, T_GOLD);
    tft.drawRect(32, 24, 256, 174, tft.color565(40, 50, 70));
    tft.fillRect(33, 25, 254, 172, tft.color565(8, 10, 24));
    tft.setTextSize(1); tft.setTextColor(tft.color565(40, 50, 70), T_BG);
    tft.setCursor(((320 - 12 * 6) / 2) | 0, 107); tft.print('MAJOR ARCANA');
    tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - 16 * 6) / 2) | 0, 200); tft.print('TAP TO DRAW A CARD');
  } else {
    tarotDrawText();
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - 20 * 6) / 2) | 0, 200); tft.print('TAP TO DRAW NEXT CARD');
  }
  drawButton(4, 208, 80, 22, 'BACK');
  drawButton(236, 208, 80, 22, 'HISTORY');
}
function toolTarotInit() {
  toolsLoad();
  tarotCurrent = -1; tarotRevealIdx = 0; tarotRevealTotal = 0; tarotRevealing = false; tarotHistScroll = 0;
  tarotNameLines = []; tarotDescLines = [];
}
function tarotInHistory() { return tarotHistScroll > 0; }
function toolTarotTap(x, y) {
  if (tarotHistScroll > 0) {
    const maxScroll = max(1, tarotHistory.length - 4 + 1);
    if (hit(x, y, 4, 18, 70, 20)) { tarotHistScroll = 0; toolTarotDraw(); }
    else if (hit(x, y, 88, 18, 144, 20)) { tarotHistory.length = 0; tarotHistScroll = 0; toolTarotDraw(); }
    else if (hit(x, y, 246, 18, 36, 20)) { if (tarotHistScroll > 1) { tarotHistScroll--; tarotDrawHistory(); } }
    else if (hit(x, y, 284, 18, 36, 20)) { if (tarotHistScroll < maxScroll) { tarotHistScroll++; tarotDrawHistory(); } }
    return;
  }
  if (hit(x, y, 4, 208, 80, 22)) return;
  if (hit(x, y, 236, 208, 80, 22)) { tarotHistScroll = 1; tarotDrawHistory(); return; }
  if (tarotPos >= 22) tarotShuffle();
  tarotCurrent = tarotDeck[tarotPos++];
  tarotHistory.push(tarotCurrent);
  if (tarotHistory.length > 10) tarotHistory.shift();
  tarotPrecompute();
  tarotRevealIdx = 0; tarotRevealing = true; tarotLastRev = millis();
  tarotSave();
  toolTarotDraw();
}
function toolTarotTick() {
  if (tarotCurrent < 0 || tarotHistScroll > 0) return;
  if (tarotRevealing && millis() - tarotLastRev >= cfg_glyphSpinMs) {
    tarotLastRev = millis();
    if (tarotRevealIdx < tarotRevealTotal) tarotRevealIdx++; else tarotRevealing = false;
  }
  tarotDrawText();
}

// ── 3. Oracle ─────────────────────────────────────
let oracleCurrent = -1, oracleReveal = 0, oracleRevealing = false, oracleLastResponse = -1;
function oracleDrawOrb() {
  const cy = 115, r = 75, orbBG = tft.color565(5, 8, 20);
  if (oracleCurrent !== oracleLastResponse) { tft.fillCircle(160, cy, r - 9, orbBG); oracleLastResponse = oracleCurrent; }
  if (oracleCurrent < 0) {
    tft.setTextSize(1); tft.setTextColor(tft.color565(40, 60, 100), orbBG);
    tft.setCursor(((320 - 7 * 6) / 2) | 0, cy - 4); tft.print('TAP ORB');
    return;
  }
  const ls = wrapText(FW.ORACLE_RESPONSES[oracleCurrent], 20);
  const startY = cy - ((ls.length * 10) / 2 | 0);
  let di = 0;
  ls.forEach((s, l) => {
    const lx = ((320 - s.length * 6) / 2) | 0;
    for (let i = 0; i < s.length; i++) {
      tft.setTextColor(tft.color565(80, 120, 200), orbBG);
      tft.setCursor(lx + i * 6, startY + l * 12); tft.print(di + i < oracleReveal ? s[i] : scramble());
    }
    di += s.length + 1;
  });
  if (oracleRevealing && millis() - toolLastReveal > 80) {
    toolLastReveal = millis();
    if (oracleReveal < totalCharsOf(ls)) oracleReveal++; else oracleRevealing = false;
  }
}
function toolOracleDraw() {
  toolHeader('ORACLE', T_TEXT, 6);
  tft.fillCircle(160, 115, 75, tft.color565(5, 8, 20));
  tft.drawCircle(160, 115, 75, tft.color565(40, 60, 100));
  tft.drawCircle(160, 115, 74, tft.color565(20, 35, 70));
  tft.drawCircle(160, 115, 67, tft.color565(10, 20, 50));
  oracleDrawOrb();
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(((320 - 20 * 6) / 2) | 0, 200); tft.print('TAP THE ORB TO CONSULT');
  drawButton(95, 210, 130, 24, 'BACK');
}
function toolOracleInit() { oracleCurrent = -1; oracleReveal = 0; oracleRevealing = false; oracleLastResponse = -1; }
function toolOracleTap(x, y) {
  if (hit(x, y, 95, 210, 130, 24)) return;
  oracleCurrent = random(FW.ORACLE_RESPONSES.length); oracleReveal = 0; oracleRevealing = true;
  oracleLastResponse = -2; toolLastReveal = millis();
  toolOracleDraw();
}
function toolOracleTick() { if (oracleRevealing) oracleDrawOrb(); }

// ── 4. Countdown timer ────────────────────────────
const CD_SET = 0, CD_RUNNING = 1, CD_PAUSED = 2, CD_DONE = 3;
let cdState = CD_SET, cdSetH = 0, cdSetM = 5, cdSetS = 0, cdTarget = 0, cdPauseRem = 0, cdLastFlash = 0, cdFlashOn = false, cdLastBarDraw = 0;

function cdDrawSet() {
  toolHeader('COUNTDOWN TIMER', T_TEXT, 17);
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(22, 34); tft.print('HH'); tft.setCursor(130, 34); tft.print('MM'); tft.setCursor(238, 34); tft.print('SS');
  const field = (x, v) => {
    drawButton(x, 44, 56, 20, '+');
    tft.setTextSize(3); tft.setTextColor(T_TEXT, T_BG);
    tft.setCursor(x + 10, 70); tft.print(pad2(v));
    drawButton(x, 94, 56, 20, '-');
  };
  field(10, cdSetH); field(118, cdSetM); field(226, cdSetS);
  tft.setTextSize(2); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(102, 70); tft.print(':'); tft.setCursor(210, 70); tft.print(':');
  drawButton(60, 130, 90, 28, 'START');
  drawButton(170, 130, 90, 28, 'BACK');
}
function cdFmt(ms) { return pad2((ms / 3600000) | 0) + ':' + pad2(((ms % 3600000) / 60000) | 0) + ':' + pad2(((ms % 60000) / 1000) | 0); }
function cdDrawRunning() {
  toolHeader('COUNTDOWN TIMER', T_TEXT, 17);
  let remMs = 0;
  if (cdState === CD_RUNNING) remMs = max(0, cdTarget - millis());
  else if (cdState === CD_PAUSED) remMs = cdPauseRem;
  const totalSet = (cdSetH * 3600 + cdSetM * 60 + cdSetS) * 1000;
  const frac = min(1, totalSet > 0 ? remMs / totalSet : 0);
  tft.fillRect(20, 24, 280, 8, tft.color565(25, 30, 40));
  tft.fillRect(20, 24, (280 * frac) | 0, 8, frac > 0.25 ? T_TEXT : T_BAD);
  tft.setTextSize(4); tft.setTextColor(cdState === CD_DONE ? T_BAD : T_TEXT, T_BG);
  tft.setCursor(((320 - 8 * 24) / 2) | 0, 60); tft.print(cdFmt(remMs));
  if (cdState === CD_DONE) {
    tft.setTextSize(2); tft.setTextColor(T_BAD, T_BG);
    tft.setCursor(((320 - 8 * 12) / 2) | 0, 110); tft.print('TIME IS UP');
  } else {
    const hint = cdState === CD_PAUSED ? 'PAUSED' : 'RUNNING';
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - hint.length * 6) / 2) | 0, 112); tft.print(hint);
  }
  drawButton(10, 170, 80, 28, cdState === CD_PAUSED ? 'RESUME' : 'PAUSE');
  drawButton(100, 170, 80, 28, 'RESET');
  drawButton(210, 170, 80, 28, 'BACK');
}
function toolCDDraw() { if (cdState === CD_SET) cdDrawSet(); else cdDrawRunning(); }
function toolCDTick() {
  if (cdState !== CD_RUNNING) return;
  if (millis() >= cdTarget) { cdState = CD_DONE; cdDrawRunning(); buzz(); return; }
  const rem = cdTarget - millis();
  if (rem < 10000 && millis() - cdLastFlash > 400) {
    cdLastFlash = millis(); cdFlashOn = !cdFlashOn;
    tft.setTextSize(4); tft.setTextColor(cdFlashOn ? T_BAD : T_BG, T_BG);
    tft.setCursor(((320 - 8 * 24) / 2) | 0, 60); tft.print(cdFmt(rem));
  }
  if (millis() - cdLastBarDraw > 1000) { cdLastBarDraw = millis(); cdDrawRunning(); }
}
function toolCDTap(x, y) {
  if (cdState === CD_SET) {
    if (hit(x, y, 10, 44, 56, 20)) { cdSetH = (cdSetH + 1) % 24; cdDrawSet(); }
    if (hit(x, y, 10, 94, 56, 20)) { cdSetH = (cdSetH + 23) % 24; cdDrawSet(); }
    if (hit(x, y, 118, 44, 56, 20)) { cdSetM = (cdSetM + 1) % 60; cdDrawSet(); }
    if (hit(x, y, 118, 94, 56, 20)) { cdSetM = (cdSetM + 59) % 60; cdDrawSet(); }
    if (hit(x, y, 226, 44, 56, 20)) { cdSetS = (cdSetS + 1) % 60; cdDrawSet(); }
    if (hit(x, y, 226, 94, 56, 20)) { cdSetS = (cdSetS + 59) % 60; cdDrawSet(); }
    if (hit(x, y, 60, 130, 90, 28)) {
      const dur = (cdSetH * 3600 + cdSetM * 60 + cdSetS) * 1000;
      if (dur > 0) { cdTarget = millis() + dur; cdState = CD_RUNNING; cdDrawRunning(); }
    }
  } else {
    if (hit(x, y, 10, 170, 80, 28)) {
      if (cdState === CD_RUNNING) { cdPauseRem = max(0, cdTarget - millis()); cdState = CD_PAUSED; }
      else if (cdState === CD_PAUSED) { cdTarget = millis() + cdPauseRem; cdState = CD_RUNNING; }
      cdDrawRunning();
    }
    if (hit(x, y, 100, 170, 80, 28)) { cdState = CD_SET; cdDrawSet(); }
  }
}

// ── 5. Word generator ─────────────────────────────
const GEN_SETS = ['GEN_NOUNS', 'GEN_ADJECTIVES', 'GEN_VERBS', 'GEN_NAMES'], GEN_LABELS = ['NOUN', 'ADJ', 'VERB', 'NAME'];
let genCategory = 0, genResult = '', genReveal = 0, genRevealing = false;
function genDrawResult() {
  if (!genResult) return;
  const cw = 18, rx = ((320 - genResult.length * cw) / 2) | 0;
  tft.fillRect(0, 75, 320, 50, T_BG);
  tft.setTextSize(3);
  for (let i = 0; i < genResult.length; i++) {
    tft.setTextColor(T_GOLD, T_BG);
    tft.setCursor(rx + i * cw, 90); tft.print(i < genReveal ? genResult[i] : scramble());
  }
  if (genRevealing && millis() - toolLastReveal > 60) {
    toolLastReveal = millis();
    if (genReveal < genResult.length) genReveal++; else genRevealing = false;
  }
}
function toolGenDraw() {
  toolHeader('WORD GENERATOR', T_TEXT, 13);
  GEN_LABELS.forEach((lbl, i) => {
    const sel = genCategory === i, bx = 4 + i * 78;
    tft.drawRect(bx, 20, 74, 20, sel ? T_TEXT : T_DIM);
    tft.fillRect(bx + 1, 21, 72, 18, sel ? tft.color565(20, 40, 80) : T_BG);
    tft.setTextSize(1); tft.setTextColor(sel ? T_TEXT : T_DIM, T_BG);
    tft.setCursor(bx + ((74 - lbl.length * 6) / 2 | 0), 27); tft.print(lbl);
  });
  if (genResult) genDrawResult();
  else {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(((320 - 18 * 6) / 2) | 0, 100); tft.print('TAP GENERATE TO DRAW');
  }
  drawButton(85, 150, 150, 28, 'GENERATE');
  drawButton(95, 210, 130, 24, 'BACK');
}
function toolGenInit() { genResult = ''; genReveal = 0; genRevealing = false; genCategory = 0; }
function toolGenTap(x, y) {
  if (hit(x, y, 95, 210, 130, 24)) return;
  for (let i = 0; i < 4; i++) {
    if (hit(x, y, 4 + i * 78, 20, 74, 20)) { genCategory = i; genResult = ''; genReveal = 0; genRevealing = false; toolGenDraw(); return; }
  }
  if (hit(x, y, 85, 150, 150, 28)) {
    const set = FW[GEN_SETS[genCategory]];
    genResult = set[random(set.length)]; genReveal = 0; genRevealing = true; toolLastReveal = millis();
    toolGenDraw();
  }
}
function toolGenTick() { if (genRevealing) genDrawResult(); }

// ── 6. Cipher wheel ───────────────────────────────
let cipherInput = '', cipherOutput = '', cipherShift = 13, cipherEncoding = true;
function cipherCompute() {
  const shift = cipherEncoding ? cipherShift : 26 - cipherShift;
  cipherOutput = [...cipherInput].map(c => (c < 'A' || c > 'Z') ? c
    : String.fromCharCode(65 + (c.charCodeAt(0) - 65 + shift) % 26)).join('');
}
function toolCipherDraw() {
  toolHeader('CIPHER WHEEL', T_TEXT, 12);
  drawButton(10, 20, 80, 18, 'ENCODE', cipherEncoding);
  drawButton(100, 20, 80, 18, 'DECODE', !cipherEncoding);
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(190, 23); tft.print('SHIFT:');
  drawButton(246, 20, 20, 18, '-');
  tft.setTextSize(1); tft.setTextColor(T_GOLD, T_BG);
  tft.setCursor(268, 23); tft.print(String(cipherShift).padStart(2, ' '));
  drawButton(290, 20, 20, 18, '+');
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(4, 44); tft.print('INPUT:');
  let inDisp = cipherInput + (cipherInput.length < 32 ? '_' : '');
  if (inDisp.length > 26) inDisp = inDisp.slice(-26);
  tft.setTextColor(T_TEXT, T_BG); tft.setCursor(4, 54); tft.print(inDisp);
  tft.setTextColor(T_DIM, T_BG); tft.setCursor(4, 68); tft.print('OUTPUT:');
  tft.setTextColor(T_GOLD, T_BG); tft.setCursor(4, 78);
  tft.print(cipherOutput.length > 26 ? cipherOutput.slice(-26) : cipherOutput);
  tft.drawFastHLine(0, 90, 320, T_DIM);
  for (let i = 0; i < 29; i++) {
    const c = FW.CIPHER_CHARS[i], bx = 1 + (i % 6) * 53, by = 93 + ((i / 6) | 0) * 22;
    tft.drawRect(bx, by, 51, 20, T_DIM);
    tft.fillRect(bx + 1, by + 1, 49, 18, T_BG);
    const label = c === ' ' ? 'SP' : c;
    tft.setTextSize(1); tft.setTextColor(c === '<' ? T_BAD : T_TEXT, T_BG);
    tft.setCursor(bx + ((51 - label.length * 6) / 2 | 0), by + 6); tft.print(label);
  }
  drawButton(95, 208, 130, 24, 'BACK');
}
function toolCipherInit() { cipherInput = ''; cipherOutput = ''; cipherShift = 13; cipherEncoding = true; }
function toolCipherTap(x, y) {
  if (hit(x, y, 95, 208, 130, 24)) return;
  if (hit(x, y, 10, 20, 80, 18)) { cipherEncoding = true; cipherCompute(); toolCipherDraw(); return; }
  if (hit(x, y, 100, 20, 80, 18)) { cipherEncoding = false; cipherCompute(); toolCipherDraw(); return; }
  if (hit(x, y, 246, 20, 20, 18)) { if (cipherShift > 1) cipherShift--; cipherCompute(); toolCipherDraw(); return; }
  if (hit(x, y, 290, 20, 20, 18)) { if (cipherShift < 25) cipherShift++; cipherCompute(); toolCipherDraw(); return; }
  for (let i = 0; i < 29; i++) {
    const bx = 1 + (i % 6) * 53, by = 93 + ((i / 6) | 0) * 22;
    if (hit(x, y, bx, by, 51, 20)) {
      const c = FW.CIPHER_CHARS[i];
      if (c === '<') cipherInput = cipherInput.slice(0, -1);
      else if (cipherInput.length < 32) cipherInput += c;
      cipherCompute(); toolCipherDraw(); return;
    }
  }
}
