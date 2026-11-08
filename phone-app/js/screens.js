// =================================================
// screens.js — router + menus, settings, D20, game picker,
// idle animation, reset and history (beeper.ino).
// =================================================

let toolsScrollY = 0, cyberScrollY = 0;

function goTo(target) {
  previousScreen = screen;
  screen = target;
  if (previousScreen === MENU_4 && target !== MENU_4) timerBlinkCount = 0;
  if (previousScreen >= CYBER_WIFI && previousScreen <= CYBER_QR && target !== previousScreen) cyberCleanup();
  if (isPrescriptScreen(screen)) showStoredPrescript();
  switch (screen) {
    case MENU_4: drawTimedMenu(); break;
    case RESULT: drawResult(); break;
    case MAIN_MENU: drawMenu(); break;
    case SETTINGS: drawSettings(); break;
    case SETTINGS_TIMER: drawSettingsTimer(); break;
    case SETTINGS_IDLE: drawSettingsIdle(); break;
    case SETTINGS_BRIGHT: drawSettingsBright(); break;
    case SETTINGS_SPEED: drawSettingsSpeed(); break;
    case SETTINGS_DIFFICULTY: drawSettingsDifficulty(); break;
    case SETTINGS_CATEGORIES: drawSettingsCategories(); break;
    case RESET_CONFIRM: drawResetConfirm(); break;
    case HISTORY_SCREEN: drawHistoryScreen(); break;
    case DICE_ROLL: startDiceRoll(); break;
    case IDLE_ANIM: drawIdleAnim(); break;
    case GAME_PICKER: gamePickerStart(); break;
    case CYBER_MENU: cyberScrollY = 0; drawCyberMenu(); break;
    case CYBER_WIFI: cyberWifiScan(); break;
    case CYBER_BLE: cyberBLEDraw(); break;
    case CYBER_BT: cyberBTRun(); break;
    case CYBER_QR: cyberQRDraw(); break;
    case TOOLS_MENU: toolsScrollY = 0; drawToolsMenu(); break;
    case TOOL_COIN: toolCoinDraw(); break;
    case TOOL_TAROT: toolTarotInit(); toolTarotDraw(); break;
    case TOOL_ORACLE: toolOracleInit(); toolOracleDraw(); break;
    case TOOL_TIMER: toolCDDraw(); break;
    case TOOL_NAMES: toolGenInit(); toolGenDraw(); break;
    case TOOL_CIPHER: toolCipherInit(); toolCipherDraw(); break;
    case LYRICS_SCREEN: drawLyricsScreen(); break;
    case SONG_SELECT: drawSongSelect(); break;
  }
}

function goToLoopScreen(i) { goTo(loopScreens[i % loopLen]); }
function nextScreen() { loopIdx = (loopIdx + 1) % loopLen; goToLoopScreen(loopIdx); }
function prevScreen() { loopIdx = (loopIdx + loopLen - 1) % loopLen; goToLoopScreen(loopIdx); }

function centreX(str, cw) { return ((320 - str.length * cw) / 2) | 0; }

// ── D20 ───────────────────────────────────────────
const DICE_SPIN = 0, DICE_SETTLE = 1, DICE_HOLD = 2;
let dicePhase = DICE_SPIN, diceFinal = 1, diceCurrent = 1, diceFrame = 0, diceSettleStep = 0, diceLastFrame = 0;

function drawD20(cx, cy, r, number, faceColor, numColor, drawFull) {
  const sides = 20, step = 2 * Math.PI / sides, off = -Math.PI / 2;
  const pt = (rad, a) => [cx + Math.trunc(rad * Math.cos(a)), cy + Math.trunc(rad * Math.sin(a))];
  if (drawFull) for (let i = 0; i < sides; i++) {
    const [x1, y1] = pt(r, off + i * step), [x2, y2] = pt(r, off + (i + 1) * step);
    tft.fillTriangle(cx, cy, x1, y1, x2, y2, faceColor);
  }
  for (let i = 0; i < sides; i++) {
    const [x1, y1] = pt(r, off + i * step), [x2, y2] = pt(r, off + (i + 1) * step);
    tft.drawLine(x1, y1, x2, y2, TEXT_COLOR);
  }
  const ri = (r * 3 / 5) | 0;
  for (let i = 0; i < sides; i++) {
    const [x1, y1] = pt(ri, off + i * step), [x2, y2] = pt(ri, off + (i + 1) * step);
    tft.drawLine(x1, y1, x2, y2, tft.color565(40, 60, 90));
  }
  const buf = String(number), tsize = number >= 10 ? 3 : 4, cw = tsize * 6, ch = tsize * 8;
  tft.setTextSize(tsize); tft.setTextColor(numColor, faceColor);
  tft.setCursor(cx - ((buf.length * cw) / 2 | 0), cy - (ch / 2 | 0)); tft.print(buf);
}

function startDiceRoll() {
  dicePhase = DICE_SPIN; diceFinal = random(1, 21); diceCurrent = random(1, 21);
  diceFrame = 0; diceSettleStep = 0; diceLastFrame = 0;
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(70, 70, 90), BG_COLOR);
  tft.setCursor(centreX('TAP TO REROLL', 6), 210); tft.print('TAP TO REROLL');
  drawButton(10, 200, 80, 22, 'BACK');
}

function drawDiceRoll() {
  let interval;
  if (dicePhase === DICE_SPIN) interval = DICE_SPIN_MS;
  else if (dicePhase === DICE_SETTLE) interval = DICE_SPIN_MS + ((180 * diceSettleStep) / DICE_SETTLE_FRAMES | 0);
  else return;
  if (millis() - diceLastFrame < interval) return;
  diceLastFrame = millis();
  const cx = 160, cy = 110, r = 85;
  if (dicePhase === DICE_SPIN) {
    diceCurrent = random(1, 21); diceFrame++;
    drawD20(cx + random(-3, 4), cy + random(-3, 4), r, diceCurrent, BG_COLOR, TEXT_COLOR, true);
    if (diceFrame >= DICE_SPIN_FRAMES) { dicePhase = DICE_SETTLE; diceSettleStep = 0; }
  } else {
    diceSettleStep++;
    diceCurrent = diceSettleStep % 2 === 0 ? diceFinal : random(1, 21);
    drawD20(cx, cy, r, diceCurrent, BG_COLOR, TEXT_COLOR, true);
    if (diceSettleStep >= DICE_SETTLE_FRAMES) {
      dicePhase = DICE_HOLD; diceCurrent = diceFinal;
      const fc = diceFinal === 20 ? tft.color565(180, 140, 0) : diceFinal === 1 ? tft.color565(160, 20, 20) : tft.color565(0, 35, 80);
      tft.fillCircle(cx, cy, r + 6, BG_COLOR);
      drawD20(cx, cy, r, diceFinal, fc, BG_COLOR, true);
      tft.setTextSize(1);
      if (diceFinal === 20) { tft.setTextColor(tft.color565(220, 180, 0), BG_COLOR); tft.setCursor(centreX('NAT  20!', 6), 15); tft.print('NAT  20!'); }
      else if (diceFinal === 1) { tft.setTextColor(tft.color565(200, 50, 50), BG_COLOR); tft.setCursor(centreX('NAT  1.', 6), 15); tft.print('NAT  1.'); }
      else tft.fillRect(0, 10, 320, 12, BG_COLOR);
      tft.setTextColor(tft.color565(70, 70, 90), BG_COLOR);
      tft.setCursor(centreX('TAP TO REROLL', 6), 208); tft.print('TAP TO REROLL');
    }
  }
}

// ── Main menu ─────────────────────────────────────
function drawMenu() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('MAIN MENU', 12), 30); tft.print('MAIN MENU');
  drawButton(95, 70, 130, 26, 'TOOLS');
  drawButton(95, 102, 130, 26, 'CYBER TOOLS');
  drawButton(95, 134, 130, 26, 'SETTINGS');
  tft.setTextSize(1); tft.setTextColor(tft.color565(180, 140, 220), BG_COLOR);
  tft.setCursor(((320 - 22 * 6) / 2) | 0, 175); tft.print('PICK A GAME AT RANDOM');
  tft.drawRect(60, 188, 200, 28, tft.color565(120, 90, 160));
  tft.fillRect(61, 189, 198, 26, tft.color565(35, 20, 55));
  tft.setTextSize(2); tft.setTextColor(tft.color565(200, 160, 255), tft.color565(35, 20, 55));
  tft.setCursor(centreX('GAME PICKER', 12), 194); tft.print('GAME PICKER');
  tft.setTextSize(1); tft.setTextColor(tft.color565(70, 70, 90), BG_COLOR);
  tft.setCursor(((320 - 17 * 6) / 2) | 0, 224); tft.print('SWIPE UP TO GO BACK');
}

const TOOLS_ITEMS = ['ROLL D20', 'COIN FLIP', 'TAROT DRAW', 'ORACLE', 'COUNTDOWN TIMER', 'WORD GENERATOR', 'CIPHER WHEEL', 'CHILDREN OF THE CITY'];
const TOOLS_TARGETS = [DICE_ROLL, TOOL_COIN, TOOL_TAROT, TOOL_ORACLE, TOOL_TIMER, TOOL_NAMES, TOOL_CIPHER, LYRICS_SCREEN];

function drawToolsMenu() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(20, 30, 55));
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, tft.color565(20, 30, 55));
  tft.setCursor(((320 - 5 * 6) / 2) | 0, 4); tft.print('TOOLS');
  const itemH = 36, startY = 18, visH = 210;
  TOOLS_ITEMS.forEach((label, i) => {
    const y = startY + i * itemH - toolsScrollY;
    if (y + itemH < startY || y > startY + visH) return;
    tft.drawRect(4, y + 2, 278, itemH - 4, tft.color565(40, 50, 80));
    tft.fillRect(5, y + 3, 276, itemH - 6, BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(14, y + ((itemH - 8) / 2 | 0)); tft.print(label);
  });
  tft.drawRect(258, 220, 60, 20, tft.color565(40, 50, 80));
  tft.fillRect(259, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(100, 120, 160), BG_COLOR);
  tft.setCursor(267, 226); tft.print('BACK');
}

// Phone names for the cyber tools (see cyber.js for why they changed).
const CYBER_ITEMS = ['NETWORK STATUS', 'BLE / TRACKERS', 'DEVICE SCAN', 'QR DISPLAY'];
const CYBER_TARGETS = [CYBER_WIFI, CYBER_BLE, CYBER_BT, CYBER_QR];

function drawCyberMenu() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(20, 50, 30));
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, tft.color565(20, 50, 30));
  tft.setCursor(((320 - 10 * 6) / 2) | 0, 4); tft.print('CYBER TOOLS');
  const itemH = 40, startY = 18, visH = 210;
  CYBER_ITEMS.forEach((label, i) => {
    const y = startY + i * itemH - cyberScrollY;
    if (y + itemH < startY || y > startY + visH) return;
    tft.drawRect(4, y + 2, 278, itemH - 4, tft.color565(30, 60, 40));
    tft.fillRect(5, y + 3, 276, itemH - 6, BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(14, y + ((itemH - 8) / 2 | 0)); tft.print(label);
  });
  tft.drawRect(2, 220, 60, 20, tft.color565(30, 60, 40));
  tft.fillRect(3, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(80, 140, 100), BG_COLOR);
  tft.setCursor(12, 226); tft.print('BACK');
}

// ── Settings ──────────────────────────────────────
function drawBackCube(colBorder, colText) {
  tft.drawRect(2, 220, 60, 20, colBorder);
  tft.fillRect(3, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(colText, BG_COLOR);
  tft.setCursor(12, 226); tft.print('BACK');
}

function drawSettings() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('SETTINGS', 12), 10); tft.print('SETTINGS');
  drawButton(70, 38, 180, 22, 'TIMER DURATION');
  drawButton(70, 66, 180, 22, 'IDLE TIMEOUT');
  drawButton(70, 94, 180, 22, 'ANIM SPEED');
  drawButton(70, 122, 180, 22, 'DIFFICULTY');
  drawButton(70, 150, 180, 22, 'CATEGORIES');
  drawButton(70, 178, 180, 22, 'RESET DECKS');
  drawButton(70, 206, 180, 22, 'HISTORY');
  drawBackCube(tft.color565(50, 50, 70), tft.color565(80, 90, 120));
  // Phone extra: brightness exists in the firmware but has no entry in its
  // settings list; it lives in the empty corner here.
  tft.drawRect(258, 220, 60, 20, tft.color565(50, 50, 70));
  tft.fillRect(259, 221, 58, 18, BG_COLOR);
  tft.setTextColor(tft.color565(80, 90, 120), BG_COLOR);
  tft.setCursor(264, 226); tft.print('BRIGHT');
}

function drawSettingsTimer() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(((320 - 17 * 6) / 2) | 0, 12); tft.print('SET TIMER DURATION');
  const field = (lx, label, bx, val) => {
    tft.setTextSize(1); tft.setTextColor(tft.color565(80, 100, 130), BG_COLOR);
    tft.setCursor(lx, 44); tft.print(label);
    drawButton(bx, 55, 30, 22, '+');
    tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(bx + 36, 58); tft.print(String(val));
    drawButton(bx, 79, 30, 22, '-');
  };
  field(14, 'DAYS', 10, editDays);
  field(122, 'HOURS', 118, editHours);
  field(226, 'MIN', 222, editMinutes);
  tft.setTextSize(1);
  const totSec = editDays * 86400 + editHours * 3600 + editMinutes * 60;
  const tot = `= ${(totSec / 86400) | 0}d ${((totSec % 86400) / 3600) | 0}h ${((totSec % 3600) / 60) | 0}m`;
  tft.setTextColor(tft.color565(80, 100, 130), BG_COLOR);
  tft.setCursor(centreX(tot, 6), 112); tft.print(tot);
  if (!editDays && !editHours && !editMinutes) {
    tft.setTextColor(FAIL_COLOR, BG_COLOR);
    tft.setCursor(((320 - 16 * 6) / 2) | 0, 126); tft.print('MIN 1 MIN REQUIRED');
  }
  drawButton(20, 148, 130, 26, 'SAVE');
  drawButton(170, 148, 130, 26, 'CANCEL');
}

function drawSettingsBright() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('BRIGHTNESS', 12), 40); tft.print('BRIGHTNESS');
  ['LOW', 'MID', 'HIGH'].forEach((lbl, i) => {
    const sel = brightnessLevel === i, bx = 20 + i * 100, by = 100;
    tft.drawRect(bx, by, 80, 36, sel ? TEXT_COLOR : tft.color565(50, 50, 70));
    tft.fillRect(bx + 1, by + 1, 78, 34, sel ? tft.color565(20, 40, 80) : BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(sel ? TEXT_COLOR : tft.color565(80, 100, 130), BG_COLOR);
    tft.setCursor(bx + ((80 - lbl.length * 6) / 2 | 0), by + 14); tft.print(lbl);
  });
  drawButton(110, 170, 100, 26, 'BACK');
}

const IDLE_OPTS = [['30 SEC', 30000], ['1 MIN', 60000], ['2 MIN', 120000], ['5 MIN', 300000], ['NEVER', SLEEP_NEVER]];
function drawSettingsIdle() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('IDLE TIMEOUT', 12), 20); tft.print('IDLE TIMEOUT');
  tft.setTextSize(1); tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR);
  tft.setCursor(((320 - 28 * 6) / 2) | 0, 48); tft.print('TIME BEFORE IDLE ANIMATION');
  IDLE_OPTS.forEach(([lbl, ms], i) => {
    const sel = sleepMs === ms, bx = 20, by = 68 + i * 30;
    tft.drawRect(bx, by, 280, 24, sel ? TEXT_COLOR : tft.color565(50, 50, 70));
    tft.fillRect(bx + 1, by + 1, 278, 22, sel ? tft.color565(20, 40, 80) : BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(sel ? TEXT_COLOR : tft.color565(70, 80, 100), BG_COLOR);
    tft.setCursor(bx + ((280 - lbl.length * 6) / 2 | 0), by + 8); tft.print(lbl);
  });
  drawButton(110, 218, 100, 22, 'BACK');
}

function drawSpeedRow(y, label, cur, vals) {
  tft.setTextSize(1); tft.setTextColor(tft.color565(80, 100, 130), BG_COLOR);
  tft.setCursor(4, y); tft.print(label);
  ['SLOW', 'MED', 'FAST'].forEach((lbl, i) => {
    const sel = cur === vals[i], bx = 10 + i * 102;
    tft.drawRect(bx, y + 10, 96, 22, sel ? TEXT_COLOR : tft.color565(50, 50, 70));
    tft.fillRect(bx + 1, y + 11, 94, 20, sel ? tft.color565(20, 40, 80) : BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(sel ? TEXT_COLOR : tft.color565(70, 80, 100), BG_COLOR);
    tft.setCursor(bx + ((96 - lbl.length * 6) / 2 | 0), y + 17); tft.print(lbl);
  });
}
const SPEED_GLYPH = [270, 90, 30], SPEED_REVEAL = [1, 2, 4], SPEED_TRANS = [5, 10, 25];
function drawSettingsSpeed() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('ANIM SPEED', 12), 8); tft.print('ANIM SPEED');
  drawSpeedRow(34, 'GLYPH SCRAMBLE', cfg_glyphSpinMs, SPEED_GLYPH);
  drawSpeedRow(100, 'TEXT REVEAL', cfg_revealChars, SPEED_REVEAL);
  drawSpeedRow(166, 'TRANSITIONS', cfg_transitionPx, SPEED_TRANS);
  drawButton(95, 210, 130, 24, 'BACK');
}

function drawSettingsDifficulty() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('DIFFICULTY', 12), 8); tft.print('DIFFICULTY');
  tft.setTextSize(1); tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR);
  tft.setCursor(((320 - 38 * 6) / 2) | 0, 36); tft.print('PASSES NEEDED TO UNLOCK EACH TIER');
  tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.setCursor(8, 62); tft.print('NORMAL UNLOCKS AFTER:');
  tft.setTextColor(GOOD_COLOR, BG_COLOR); tft.setCursor(200, 62); tft.print(diffMedThreshold + ' PASSES');
  drawButton(260, 56, 22, 18, '-'); drawButton(284, 56, 22, 18, '+');
  tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.setCursor(8, 94); tft.print('HARD UNLOCKS AFTER:');
  tft.setTextColor(tft.color565(200, 80, 40), BG_COLOR); tft.setCursor(200, 94); tft.print(diffHardThreshold + ' PASSES');
  drawButton(260, 88, 22, 18, '-'); drawButton(284, 88, 22, 18, '+');
  tft.drawFastHLine(0, 118, 320, tft.color565(40, 50, 70));
  tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR); tft.setCursor(8, 124); tft.print('CURRENT STATUS:');
  ['GAME', 'CITY', 'CON', 'TASK'].forEach((n, m) => {
    const d = menuDiffFor(m);
    const col = d === 0 ? tft.color565(60, 180, 60) : d === 1 ? TEXT_COLOR : tft.color565(200, 80, 40);
    tft.setTextColor(tft.color565(80, 100, 130), BG_COLOR); tft.setCursor(8, 136 + m * 18); tft.print(n);
    tft.setTextColor(col, BG_COLOR); tft.setCursor(70, 136 + m * 18); tft.print(['EASY', 'NORMAL', 'HARD'][d]);
    tft.setTextColor(tft.color565(60, 70, 90), BG_COLOR); tft.setCursor(140, 136 + m * 18); tft.print(`(${passCount[m]} passed)`);
  });
  drawButton(110, 220, 100, 22, 'BACK');
}

function drawSettingsCategories() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(centreX('TIMED POOLS', 12), 8); tft.print('TIMED POOLS');
  tft.setTextSize(1); tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR);
  tft.setCursor(((320 - 36 * 6) / 2) | 0, 34); tft.print('WHICH MENUS FEED THE TIMED SCREEN');
  ['GAME', 'CITY', 'CON', 'TASK'].forEach((n, i) => {
    const en = poolEnabled(i), by = 48 + i * 38;
    tft.drawRect(30, by, 260, 30, en ? TEXT_COLOR : tft.color565(50, 50, 70));
    tft.fillRect(31, by + 1, 258, 28, en ? tft.color565(15, 30, 65) : BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(en ? TEXT_COLOR : tft.color565(60, 70, 90), BG_COLOR);
    tft.setCursor(50, by + 11); tft.print(n);
    tft.setTextColor(en ? GOOD_COLOR : tft.color565(80, 30, 30), BG_COLOR);
    tft.setCursor(200, by + 11); tft.print(en ? 'ACTIVE' : 'INACTIVE');
  });
  tft.setTextSize(1); tft.setTextColor(tft.color565(50, 60, 80), BG_COLOR);
  tft.setCursor(((320 - 26 * 6) / 2) | 0, 214); tft.print('SWIPE ALWAYS SHOWS ALL MENUS');
  drawButton(110, 224, 100, 20, 'BACK');
}

// ── Game picker ───────────────────────────────────
let gamePickerIdx = 0, gamePickerLastRoll = 0, gamePickerRollFrame = 0, gamePickerSettled = false;
const GAME_COUNT = FW.GAME_LIST.length;

function drawGamePickerName(bg, small) {
  const name = FW.GAME_LIST[gamePickerIdx];
  const col = gamePickerSettled ? GOOD_COLOR : tft.color565(160, 140, 200);
  if (name.length * 12 > 260) {
    tft.setTextSize(1); tft.setTextColor(col, bg);
    tft.setCursor(centreX(name, 6), small); tft.print(name);
  } else {
    tft.setTextSize(2); tft.setTextColor(col, bg);
    tft.setCursor(centreX(name, 12), 90); tft.print(name);
  }
}

function drawGamePicker() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(40, 20, 60));
  tft.setTextSize(1); tft.setTextColor(tft.color565(200, 160, 255), tft.color565(40, 20, 60));
  tft.setCursor(((320 - 11 * 6) / 2) | 0, 4); tft.print('GAME PICKER');
  tft.drawRect(20, 60, 280, 80, tft.color565(80, 60, 120));
  tft.drawRect(22, 62, 276, 76, tft.color565(40, 30, 60));
  drawGamePickerName(BG_COLOR, 95);
  tft.setTextSize(1); tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR);
  tft.setCursor(((320 - 22 * 6) / 2) | 0, 160);
  tft.print(gamePickerSettled ? 'TAP ROLL FOR ANOTHER' : 'ROLLING...');
  drawButton(40, 180, 110, 28, 'ROLL');
  drawButton(170, 180, 110, 28, 'BACK');
  tft.setTextSize(1); tft.setTextColor(tft.color565(40, 55, 80), BG_COLOR);
  const count = GAME_COUNT + ' GAMES';
  tft.setCursor(centreX(count, 6), 222); tft.print(count);
}

function tickGamePicker() {
  if (gamePickerSettled || millis() - gamePickerLastRoll < 80) return;
  gamePickerLastRoll = millis();
  gamePickerRollFrame++;
  gamePickerIdx = random(GAME_COUNT);
  if (gamePickerRollFrame > 18) gamePickerSettled = true;
  const boxBG = tft.color565(20, 10, 35);
  tft.fillRect(23, 63, 274, 74, boxBG);
  drawGamePickerName(boxBG, 96);
  tft.fillRect(0, 155, 320, 14, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(60, 80, 110), BG_COLOR);
  const lbl = gamePickerSettled ? 'TAP ROLL FOR ANOTHER' : 'ROLLING...';
  tft.setCursor(centreX(lbl, 6), 160); tft.print(lbl);
}

function gamePickerStart() {
  gamePickerSettled = false; gamePickerRollFrame = 0; gamePickerLastRoll = millis();
  gamePickerIdx = random(GAME_COUNT);
  drawGamePicker();
}

// ── Idle animation ────────────────────────────────
const IDLE_N = 4, IDLE_LOGO_W = 320, IDLE_LOGO_H = 96, IDLE_SCAN_INTERVAL = 25;
let idleActive = false, idleLogoX0 = 0, idleLogoY0 = 72, idleScanY = 0, idleScanLast = 0;
let glitchEraseY = -1, glitchEraseX = 0, glitchEraseW = 0;
const idleParts = Array.from({ length: IDLE_N }, () => ({ x: 0, y: 0, text: '', colour: 0, revIdx: 0, phase: 0, phaseStart: 0 }));

function idleParticleColour() {
  switch (random(6)) {
    case 0: return tft.color565(40, 90, 140);
    case 1: return tft.color565(0, 110, 90);
    case 2: return tft.color565(80, 60, 130);
    case 3: return tft.color565(100, 90, 30);
    case 4: return tft.color565(30, 100, 60);
    default: return tft.color565(100, 40, 60);
  }
}
function dim565(p) {
  const r = (((p >> 11) & 0x1F) * 2 / 5) | 0, g = (((p >> 5) & 0x3F) * 2 / 5) | 0, b = ((p & 0x1F) * 2 / 5) | 0;
  return (r << 11) | (g << 5) | b;
}
function idleRestorePixels(tx, ty, len) {
  const cw = 6, ch = 8;
  tft.fillRect(tx, ty, len * cw + 1, ch, BG_COLOR);
  for (let row = 0; row < ch; row++) {
    const py = ty + row, iy = py - idleLogoY0;
    if (iy < 0 || iy >= IDLE_LOGO_H) continue;
    for (let col = 0; col < len * cw + 1; col++) {
      const px = tx + col, ix = px - idleLogoX0;
      if (ix < 0 || ix >= IDLE_LOGO_W) continue;
      const p = logoBuf[iy * 320 + ix];
      if (p) tft.drawPixel(px, py, dim565(p));
    }
  }
}

function idleSpawnPart(idx) {
  const p = idleParts[idx], cw = 6, ch = 8;
  const lx0 = idleLogoX0, lx1 = idleLogoX0 + IDLE_LOGO_W, ly0 = idleLogoY0, ly1 = idleLogoY0 + IDLE_LOGO_H;
  let chosen = null;
  for (let a = 0; a < 20; a++) {
    const cand = FW.idleTexts[random(FW.idleTexts.length)];
    if (!idleParts.some((o, j) => j !== idx && o.phase < 2 && o.text === cand)) { chosen = cand; break; }
  }
  p.text = chosen || FW.idleTexts[random(FW.idleTexts.length)];
  const pw = p.text.length * cw;
  for (let a = 0; a < 10; a++) {
    let tx, ty;
    switch (random(4)) {
      case 0: tx = random(4, 316); ty = random(4, max(5, ly0 - 10)); break;
      case 1: tx = random(4, max(5, lx0 - 4)); ty = random(4, 230); break;
      case 2: tx = random(min(316, lx1 + 2), 312); ty = random(4, 230); break;
      default: tx = random(4, 316); ty = random(ly1 + 2, 234); break;
    }
    if (tx + pw > 316) tx = max(4, 316 - pw);
    const overlaps = idleParts.some((o, j) => {
      if (j === idx || o.phase > 1) return false;
      const ow = o.text.length * cw;
      return tx < o.x + ow + 4 && tx + pw + 4 > o.x && ty < o.y + ch + 2 && ty + ch + 2 > o.y;
    });
    if (!overlaps || a === 9) { p.x = tx; p.y = ty; break; }
  }
  p.colour = idleParticleColour(); p.revIdx = 0; p.phase = 0; p.phaseStart = millis();
}

function drawIdleAnim() {
  tft.fillScreen(BG_COLOR);
  idleLogoX0 = 0; idleLogoY0 = 72;
  tft.pushImage(idleLogoX0, idleLogoY0, IDLE_LOGO_W, IDLE_LOGO_H, logoBuf);
  let best = 0, total = 0;
  for (let i = 0; i < 5; i++) { total += passCount[i]; if (bestStreak[i] > best) best = bestStreak[i]; }
  const stat = `TOTAL: ${total}   BEST STREAK: ${best}`;
  tft.setTextSize(1); tft.setTextColor(tft.color565(25, 40, 60), BG_COLOR);
  tft.setCursor(centreX(stat, 6), 166); tft.print(stat);
  idleScanY = 0; idleScanLast = millis();
  for (let i = 0; i < IDLE_N; i++) { idleSpawnPart(i); idleParts[i].phaseStart -= i * 1800; }
  idleActive = true;
}

function tickIdleAnim() {
  if (!idleActive) return;
  if (millis() - idleScanLast > IDLE_SCAN_INTERVAL) {
    idleScanLast = millis();
    if (glitchEraseY >= 0) { tft.drawFastHLine(glitchEraseX, glitchEraseY, glitchEraseW, BG_COLOR); glitchEraseY = -1; }
    tft.drawFastHLine(0, idleScanY, 320, BG_COLOR);
    const iy = idleScanY - idleLogoY0;
    if (iy >= 0 && iy < IDLE_LOGO_H) {
      for (let ix = 0; ix < IDLE_LOGO_W; ix++) {
        const px = logoBuf[iy * 320 + ix];
        if (px) tft.drawPixel(idleLogoX0 + ix, idleScanY, dim565(px));
      }
    }
    idleScanY += random(10) === 0 ? random(2, 5) : 1;
    if (idleScanY > 235) idleScanY = 0;
    const br = 16 + random(14);
    tft.drawFastHLine(0, idleScanY, 320, tft.color565((br / 3) | 0, (br / 2) | 0, br));
    if (random(14) === 0) {
      glitchEraseX = random(0, 260); glitchEraseW = random(10, 60); glitchEraseY = random(0, 235);
      tft.drawFastHLine(glitchEraseX, glitchEraseY, glitchEraseW, tft.color565(0, random(40, 100), random(60, 150)));
    }
  }
  for (let i = 0; i < IDLE_N; i++) {
    const p = idleParts[i], len = p.text.length;
    if (p.phase === 0) {
      tft.setTextSize(1);
      for (let j = 0; j < len; j++) {
        if (j < p.revIdx) tft.setTextColor(p.colour, BG_COLOR);
        else tft.setTextColor(tft.color565(20, 35, 55), BG_COLOR);
        tft.setCursor(p.x + j * 6, p.y); tft.print(j < p.revIdx ? p.text[j] : scramble());
      }
      if (millis() - p.phaseStart > cfg_glyphSpinMs) { if (p.revIdx < len) p.revIdx++; p.phaseStart = millis(); }
      if (p.revIdx >= len) { p.phase = 1; p.phaseStart = millis(); }
    } else if (p.phase === 1) {
      tft.setTextSize(1); tft.setTextColor(p.colour, BG_COLOR);
      tft.setCursor(p.x, p.y); tft.print(p.text);
      if (millis() - p.phaseStart > 2400) { p.phase = 2; p.phaseStart = millis(); }
    } else {
      tft.fillRect(p.x, p.y, len * 6 + 1, 8, BG_COLOR);
      idleRestorePixels(p.x, p.y, len);
      idleSpawnPart(i);
    }
  }
}

// ── Reset / history ───────────────────────────────
function drawResetConfirm() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(((320 - 19 * 6) / 2) | 0, 40); tft.print('RESET WHICH DECK?');
  drawButton(20, 80, 120, 28, 'GAME'); drawButton(180, 80, 120, 28, 'CITY');
  drawButton(20, 120, 120, 28, 'CON'); drawButton(180, 120, 120, 28, 'TASK');
  drawButton(20, 160, 120, 28, 'TIMED'); drawButton(180, 160, 120, 28, 'ALL');
  drawButton(95, 200, 130, 22, 'CANCEL');
}

function drawHistoryScreen() {
  tft.fillScreen(BG_COLOR);
  const names = ['GAME', 'CITY', 'CON', 'TASK', 'TIMED'];
  const src = ['gamePrescripts', 'cityPrescripts', 'conPrescripts', 'taskPrescripts', 'gamePrescripts'];
  names.forEach((n, i) => {
    const sel = histViewMenu === i, bx = i * 64;
    const bg = sel ? tft.color565(20, 40, 80) : tft.color565(10, 14, 24);
    tft.fillRect(bx, 0, 64, 24, bg);
    tft.drawRect(bx, 0, 64, 24, tft.color565(40, 50, 70));
    tft.setTextSize(1); tft.setTextColor(sel ? TEXT_COLOR : tft.color565(60, 70, 90), bg);
    tft.setCursor(bx + ((64 - n.length * 6) / 2 | 0), 8); tft.print(n);
  });
  tft.drawFastHLine(0, 24, 320, tft.color565(40, 50, 70));
  const m = histViewMenu, head = histHead[m];
  let y = 30, any = false;
  for (let n = 0; n < HIST_LEN; n++) {
    const idx = (head - 1 - n + HIST_LEN) % HIST_LEN, card = history[m][idx];
    if (card === -2) continue;
    any = true;
    tft.setTextSize(1);
    if (card === -1) {
      tft.setTextColor(RARE_COLOR, BG_COLOR); tft.setCursor(4, y); tft.print('[INDEX EVENT]');
    } else {
      const pool = FW[src[m]];
      let hs = historyText[m][idx] || pool[card % pool.length];
      if (hs.length > 46) hs = hs.slice(0, 43) + '...';
      tft.setTextColor(TEXT_COLOR, BG_COLOR); tft.setCursor(4, y); tft.print(hs);
    }
    y += 11;
    if (y > 220) break;
  }
  if (!any) {
    tft.setTextColor(tft.color565(50, 60, 80), BG_COLOR); tft.setTextSize(1);
    tft.setCursor(((320 - 12 * 6) / 2) | 0, 110); tft.print('NO HISTORY YET');
  }
  drawButton(110, 205, 100, 26, 'BACK');
}
