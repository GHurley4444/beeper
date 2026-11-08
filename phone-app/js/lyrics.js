// =================================================
// lyrics.js — song select + lyrics viewer with per-song effects
// (beeper.ino lyrics section). Lines: "" = stanza break,
// "!text" = emphasis line (large, centred).
// =================================================

const SONG_COUNT = FW.songs.length;
let songSelectScroll = 0;
let activeLyricsLines = FW.songs[0].lines, activeLyricsCount = FW.songs[0].lines.length, activeSongIdx = 0;

function newSlot() {
  return { wrapped: [], rowCount: 0, totalChars: 0, baseX: 0, baseY: 0, cw: 12, ch: 18, dir: { dx: 0, dy: 0 },
           active: false, holding: false, erasing: false, isEmphasis: false, isTitle: false,
           reveal: 0, holdTimer: 0, eraseMs: 0 };
}
const lyricsSlots = [newSlot(), newSlot()];
let lyricsLineIdx = 0, lyricsActiveSlot = 0, lyricsSongDone = false;
const lyricsWaveInit = [false, false];
const lyricsWavePrev = [new Int8Array(32), new Int8Array(32)];
const lastWaveDraw = [0, 0];
let blankTimer = 0, lastLyricTick = 0;

const LYRICS_DIRS = [[0, 0], [0, 0], [0, 1], [0, -1], [0, 2], [0, -2], [0, 3], [0, -3], [4, 2], [4, -2], [-4, 2], [-4, -2]];

function lyricsLoadSlot(s, line, slotIdx) {
  const emphasis = line[0] === '!';
  if (emphasis) line = line.slice(1);
  const len = line.length;
  Object.assign(s, { reveal: 0, holding: false, holdTimer: 0, erasing: false, eraseMs: 0, active: true,
                     rowCount: 0, totalChars: 0, wrapped: [], isTitle: false, isEmphasis: emphasis });
  if (len === 0) { s.active = false; return; }
  const isTitle = lyricsLineIdx === 0, isLast = lyricsLineIdx === activeLyricsCount - 1;
  if (isTitle || isLast || emphasis) { s.cw = 18; s.ch = 26; s.dir = { dx: 0, dy: 0 }; }
  else { s.cw = 12; s.ch = 18; const d = LYRICS_DIRS[random(12)]; s.dir = { dx: d[0], dy: d[1] }; }
  const cols = s.cw === 18 ? 13 : 20;
  s.isTitle = isTitle;
  s.isEmphasis = emphasis || isTitle || isLast;

  // Word-wrap (firmware's own variant: hard-cuts words longer than a row).
  let p = 0;
  while (p < len && s.rowCount < 8) {
    const remaining = len - p;
    let buf;
    if (remaining <= cols) { buf = line.slice(p); p = len; }
    else {
      buf = line.slice(p, p + cols);
      let q = p + cols, bi2 = buf.length, q2 = q;
      while (bi2 > 0 && buf[bi2 - 1] !== ' ') { bi2--; q2--; }
      if (bi2 === 0) p = q;
      else { buf = buf.slice(0, bi2).replace(/ +$/, ''); p = q2; }
    }
    s.wrapped.push(buf);
    s.totalChars += buf.length;
    s.rowCount++;
  }

  let maxRlen = 0;
  for (const r of s.wrapped) maxRlen = max(maxRlen, r.length);
  if (maxRlen > 0) {
    const maxDx = max(0, ((316 - maxRlen * s.cw) / maxRlen) | 0);
    if (abs(s.dir.dx) > maxDx) s.dir.dx = maxDx * (s.dir.dx >= 0 ? 1 : -1);
  }
  let maxRowPxW = 0, maxRowDrift = 0;
  for (const r of s.wrapped) {
    maxRowPxW = max(maxRowPxW, r.length * s.cw);
    maxRowDrift = max(maxRowDrift, (r.length - 1) * abs(s.dir.dy));
  }
  const blockH = s.rowCount * s.ch + abs(s.dir.dx) * max(0, s.rowCount - 1) + maxRowDrift;

  if (emphasis || isTitle || isLast) {
    s.baseX = max(2, ((320 - maxRowPxW) / 2) | 0);
    s.baseY = max(2, ((240 - blockH) / 2) | 0);
    s.dir = { dx: 0, dy: 0 };
  } else {
    const xSafe = max(2, 320 - maxRowPxW - 2);
    s.baseX = xSafe > 4 ? random(2, xSafe) : 2;
    const yHalf = 118;
    if (slotIdx === 0) {
      const yBot = yHalf - maxRowDrift - 2, yTop = 4;
      if (s.dir.dy < 0) s.baseY = yBot > yTop + maxRowDrift ? random(yTop + maxRowDrift, yBot) : yTop + maxRowDrift;
      else s.baseY = yBot > yTop + 2 ? random(yTop, yBot) : yTop;
    } else {
      const yTop = yHalf + 2, yBot = max(yTop + 2, 236 - s.rowCount * s.ch - maxRowDrift);
      if (s.dir.dy < 0) s.baseY = yBot > yTop + maxRowDrift ? random(yTop + maxRowDrift, yBot) : yTop + maxRowDrift;
      else s.baseY = yBot > yTop + 2 ? random(yTop, yBot) : yTop;
    }
  }
}

function lyricsDrawSlot(s) {
  if (!s.active) return;
  tft.setTextSize(s.cw === 18 ? 3 : 2);
  const n = s === lyricsSlots[0] ? 0 : 1;
  if (!lyricsWaveInit[n]) { lyricsWavePrev[n].fill(0); lyricsWaveInit[n] = true; }
  const waveChanged = millis() - lastWaveDraw[n] > 120;
  if (!s.erasing && !waveChanged && s.reveal >= s.totalChars) return;
  if (waveChanged) lastWaveDraw[n] = millis();

  const waveOff = new Int8Array(32);
  const phase = (millis() % 8000) / 8000 * 6.2832, amp = s.isEmphasis ? 1.2 : 1.8;
  for (let w = 0; w < 32; w++) waveOff[w] = Math.trunc(Math.sin(phase + w * 0.4) * amp);

  if (waveChanged && !s.erasing && s.reveal >= s.totalChars) {
    let ci = 0;
    for (let r = 0; r < s.rowCount; r++) {
      const rlen = s.wrapped[r].length;
      let rowBaseX = s.baseX + r * s.dir.dx;
      const rowBaseY = s.baseY + r * (s.ch + abs(s.dir.dy));
      const fflat = s.isEmphasis;
      if (fflat) rowBaseX = ((320 - rlen * s.cw) / 2) | 0;
      for (let i = 0; i < rlen; i++) {
        const cx = rowBaseX + i * s.cw, oldOff = lyricsWavePrev[n][ci % 32], newOff = waveOff[ci % 32];
        if (oldOff !== newOff) {
          const oldCy = rowBaseY + (fflat ? 0 : i * s.dir.dy) + oldOff;
          if (cx >= 0 && cx + s.cw <= 320 && oldCy >= 0 && oldCy + s.ch <= 240) tft.fillRect(cx, oldCy, s.cw, s.ch, BG_COLOR);
        }
        ci++;
      }
    }
    lyricsWavePrev[n].set(waveOff);
  }

  let charIdx = 0;
  for (let r = 0; r < s.rowCount; r++) {
    const row = s.wrapped[r], rlen = row.length;
    let rowBaseX = s.baseX + r * s.dir.dx;
    const rowBaseY = s.baseY + r * (s.ch + abs(s.dir.dy));
    const flat = s.isTitle || s.isEmphasis;
    if (flat) rowBaseX = ((320 - rlen * s.cw) / 2) | 0;
    for (let i = 0; i < rlen; i++) {
      const cx = rowBaseX + i * s.cw;
      const cy = rowBaseY + (flat ? 0 : i * s.dir.dy) + (s.erasing ? 0 : waveOff[charIdx % 32]);
      if (cx < 0 || cx + s.cw > 320 || cy < 0 || cy + s.ch > 240) { charIdx++; continue; }
      let c, col;
      if (s.erasing) {
        const start = s.totalChars > 1 ? (charIdx * 1600 / (s.totalChars - 1)) | 0 : 0, end = start + 400;
        if (s.eraseMs < start) { c = row[i]; col = TEXT_COLOR; }
        else if (s.eraseMs < end) {
          const br = (80 * (400 - (s.eraseMs - start)) / 400) | 0;
          c = scramble(); col = tft.color565((br / 4) | 0, (br / 2) | 0, br);
        } else { c = ' '; col = BG_COLOR; }
      } else if (charIdx < s.reveal) { c = row[i]; col = TEXT_COLOR; }
      else { c = scramble(); col = tft.color565(3, 5, 7); }
      tft.setTextColor(col, BG_COLOR);
      tft.setCursor(cx, cy); tft.print(c);
      charIdx++;
    }
  }
}

function lyricsReset() {
  lyricsLineIdx = 0; lyricsActiveSlot = 0; lyricsSongDone = false;
  lyricsSlots[0].active = false; lyricsSlots[1].active = false;
  blankTimer = 0;
  lyricsLoadSlot(lyricsSlots[0], activeLyricsLines[0], 0);
}

function drawLyricsScreen() { tft.fillScreen(BG_COLOR); fxClear(); lyricsReset(); }

function lyricsSelectSong(idx) {
  activeSongIdx = idx;
  activeLyricsLines = FW.songs[idx].lines;
  activeLyricsCount = activeLyricsLines.length;
}

// The device uses 22px rows, which only fits 9 songs (it never scrolls, so
// the last two can't be picked there). Rows shrink just enough to fit all.
const SONG_START_Y = 18, SONG_ITEM_H = Math.min(22, Math.floor((230 - SONG_START_Y) / SONG_COUNT));
function drawSongSelect() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(((320 - 11 * 6) / 2) | 0, 4); tft.print('SELECT SONG');
  tft.drawFastHLine(0, 14, 320, tft.color565(30, 50, 80));
  for (let i = 0; i < SONG_COUNT; i++) {
    const iy = SONG_START_Y + (i - songSelectScroll) * SONG_ITEM_H;
    if (iy < 14 || iy + SONG_ITEM_H > 230) continue;
    const sel = i === activeSongIdx;
    if (sel) tft.fillRect(0, iy, 320, SONG_ITEM_H - 1, tft.color565(10, 20, 40));
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(80, 120, 180), BG_COLOR);
    tft.setCursor(8, iy + ((SONG_ITEM_H - 8) >> 1)); tft.print(FW.songs[i].title);
  }
  tft.drawFastHLine(0, 232, 320, tft.color565(30, 50, 80));
  tft.setTextColor(tft.color565(40, 60, 90), BG_COLOR);
  tft.setCursor(4, 234); tft.print('TAP: PLAY  SWIPE DOWN: BACK');
}

function songSelectTap(x, y) {
  for (let i = 0; i < SONG_COUNT; i++) {
    const iy = SONG_START_Y + (i - songSelectScroll) * SONG_ITEM_H;
    if (y >= iy && y < iy + SONG_ITEM_H && iy >= 14 && iy < 230) { lyricsSelectSong(i); goTo(LYRICS_SCREEN); return; }
  }
}

// =================================================
// PER-SONG EFFECTS (indices follow the song catalogue)
// =================================================
const FX_MAX = 24, MAX_CRACKS = 6, MAX_SLASHES = 3;
const fxParts = Array.from({ length: FX_MAX }, () => ({ x: 0, y: 0, vx: 0, vy: 0, col: 0, life: 0, type: 0 }));
const glassCracks = Array.from({ length: MAX_CRACKS }, () => ({ active: false }));
const swordSlashes = Array.from({ length: MAX_SLASHES }, () => ({ active: false }));
let fxLastTick = 0, crackLastSpawn = 0, slashLastSpawn = 0, waveX = 0;
const glitchY = [-1, -1, -1], glitchErase = [0, 0, 0];
let beatLast = 0, beatPhase = 0, beatR = 0;

function fxClear() {
  for (const p of fxParts) p.life = 0;
  for (const c of glassCracks) c.active = false;
  for (const s of swordSlashes) s.active = false;
}
function fxSpawn() { return fxParts.find(p => p.life === 0) || null; }
const cdiv = (a, b) => Math.trunc(a / b);   // C integer division

function fxTickStars() {
  const spawns = random(1, 4);
  for (let s = 0; s < spawns; s++) {
    const p = fxSpawn();
    if (p) { p.x = random(0, 320); p.y = random(0, 240); p.life = random(6, 18); p.vx = 0; p.vy = 0; p.type = 0; }
  }
  for (const p of fxParts) {
    if (!p.life || p.type !== 0) continue;
    const ph = p.life % 6, br = ph < 3 ? 60 + ph * 60 : 240 - (ph - 3) * 60;
    tft.drawPixel(p.x, p.y, tft.color565(br, br, min(255, br + 60)));
    if (br > 120) {
      const h = tft.color565(br >> 1, br >> 1, br >> 1);
      tft.drawPixel(p.x + 1, p.y, h); tft.drawPixel(p.x - 1, p.y, h);
      tft.drawPixel(p.x, p.y + 1, h); tft.drawPixel(p.x, p.y - 1, h);
    }
    p.life--;
    if (!p.life) {
      tft.drawPixel(p.x, p.y, BG_COLOR);
      tft.drawPixel(p.x + 1, p.y, BG_COLOR); tft.drawPixel(p.x - 1, p.y, BG_COLOR);
      tft.drawPixel(p.x, p.y + 1, BG_COLOR); tft.drawPixel(p.x, p.y - 1, BG_COLOR);
    }
  }
}

function fxTickFire() {
  const spawns = random(2, 5);
  for (let s = 0; s < spawns; s++) {
    const p = fxSpawn();
    if (p) { p.x = random(0, 320); p.y = random(225, 240); p.vx = random(-120, 120); p.vy = -random(150, 400); p.life = random(16, 36); p.type = 1; }
  }
  for (const p of fxParts) {
    if (!p.life || p.type !== 1) continue;
    tft.drawPixel(p.x, p.y, BG_COLOR); tft.drawPixel(p.x + 1, p.y, BG_COLOR);
    p.x += cdiv(p.vx, 100); p.y += cdiv(p.vy, 100); p.vy += 20; p.life--;
    if (p.x < 0 || p.x >= 319 || p.y < 0 || !p.life) { p.life = 0; continue; }
    const col = tft.color565(255, min(255, (36 - p.life) * 8), 0);
    tft.drawPixel(p.x, p.y, col); tft.drawPixel(p.x + 1, p.y, col);
  }
  for (let x = 0; x < 320; x += 2) {
    const fy = 235 + Math.trunc(Math.sin(x * 0.15 + millis() * 0.003) * 3);
    tft.drawPixel(x, fy, tft.color565(255, random(40, 140), 0));
  }
}

function fxTickFeathers() {
  if (random(5) === 0) {
    const p = fxSpawn();
    if (p) { p.x = random(0, 320); p.y = 230; p.vx = random(-40, 40); p.vy = -random(60, 140); p.life = random(18, 36); p.type = 2; }
  }
  for (const p of fxParts) {
    if (!p.life || p.type !== 2) continue;
    tft.drawPixel(p.x, p.y, BG_COLOR);
    p.x += cdiv(p.vx, 100) + Math.trunc(Math.sin(millis() * 0.002 + p.x * 0.1));
    p.y += cdiv(p.vy, 100); p.life--;
    if (p.y < 0 || !p.life) { p.life = 0; continue; }
    const br = (p.life * 7) & 0xFF;
    tft.drawPixel(p.x, p.y, tft.color565(br >> 1, br >> 1, br));
  }
}

function fxTickGlass() {
  const now = millis();
  if (now - crackLastSpawn > random(1500, 3000)) {
    crackLastSpawn = now;
    const c = glassCracks.find(k => !k.active);
    if (c) {
      c.ox = random(20, 300); c.oy = random(20, 220);
      const angle = random(0, 628) / 100, len = random(40, 120);
      c.x0 = c.ox; c.y0 = c.oy;
      c.x1 = c.ox + Math.trunc(Math.cos(angle) * len); c.y1 = c.oy + Math.trunc(Math.sin(angle) * len);
      const ba = angle + (random(0, 2) ? 0.6 : -0.6) + random(-20, 20) * 0.01, blen = random(20, 70);
      const midX = ((c.ox + c.x1) >> 1) + random(-10, 10), midY = ((c.oy + c.y1) >> 1) + random(-10, 10);
      c.x2 = midX + Math.trunc(Math.cos(ba) * blen); c.y2 = midY + Math.trunc(Math.sin(ba) * blen);
      c.hasBranch = random(3) !== 0;
      c.born = now; c.lifetime = random(3000, 7000); c.active = true;
      const col = tft.color565(80, 0, 120);
      tft.drawLine(c.x0, c.y0, c.x1, c.y1, col);
      if (c.hasBranch) tft.drawLine(midX, midY, c.x2, c.y2, col);
      const splinters = random(2, 5);
      for (let s = 0; s < splinters; s++) {
        const sx = random(min(c.x0, c.x1), max(c.x0, c.x1) + 1), sy = random(min(c.y0, c.y1), max(c.y0, c.y1) + 1);
        const sa = angle + (random(0, 2) ? 1.2 : -1.2), sl = random(8, 25);
        tft.drawLine(sx, sy, sx + Math.trunc(Math.cos(sa) * sl), sy + Math.trunc(Math.sin(sa) * sl), tft.color565(60, 0, 90));
      }
    }
  }
  for (const c of glassCracks) {
    if (!c.active || now - c.born <= c.lifetime) continue;
    tft.drawLine(c.x0, c.y0, c.x1, c.y1, BG_COLOR);
    if (c.hasBranch) tft.drawLine((c.ox + c.x1) >> 1, (c.oy + c.y1) >> 1, c.x2, c.y2, BG_COLOR);
    c.active = false;
  }
}

function fxTickWaves() {
  waveX = (waveX + 6) % 320;
  for (let x = 0; x < 320; x++) {
    const wy1 = 222 + Math.trunc(Math.sin((x + waveX) * 0.05) * 7);
    const wy2 = 226 + Math.trunc(Math.sin((x + waveX * 1.4) * 0.09) * 5);
    const wy3 = 230 + Math.trunc(Math.sin((x + waveX * 2.1) * 0.13) * 4);
    tft.drawPixel(x, wy1, tft.color565(0, 40, 140));
    tft.drawPixel(x, wy2, tft.color565(0, 120 + random(30), 200));
    tft.drawPixel(x, wy3, tft.color565(40, 180 + random(40), 255));
    for (let y = wy1 - 1; y < wy3 + 2 && y < 240; y++) if (y !== wy1 && y !== wy2 && y !== wy3) tft.drawPixel(x, y, BG_COLOR);
  }
}

function fxTickBubbles() {
  if (random(6) === 0) {
    const p = fxSpawn();
    if (p) { p.x = random(10, 310); p.y = random(200, 235); p.vx = random(-20, 20); p.vy = -random(20, 60); p.life = random(20, 40); p.type = 5; }
  }
  for (const p of fxParts) {
    if (!p.life || p.type !== 5) continue;
    tft.drawPixel(p.x, p.y, BG_COLOR); tft.drawPixel(p.x + 1, p.y, BG_COLOR); tft.drawPixel(p.x, p.y + 1, BG_COLOR);
    p.x += cdiv(p.vx, 100); p.y += cdiv(p.vy, 100); p.life--;
    if (p.y < 0 || !p.life) { p.life = 0; continue; }
    const br = (p.life * 5) & 0xFF, c = tft.color565(br, br >> 1, br);
    tft.drawPixel(p.x, p.y, c); tft.drawPixel(p.x + 1, p.y, c); tft.drawPixel(p.x, p.y + 1, c);
  }
}

function fxTickSlash() {
  const now = millis();
  if (now - slashLastSpawn > random(1800, 4000)) {
    slashLastSpawn = now;
    const s = swordSlashes.find(k => !k.active);
    if (s) {
      const t = random(3);
      if (t === 0) { s.x0 = random(0, 80); s.y0 = random(0, 60); s.x1 = random(240, 320); s.y1 = random(180, 240); }
      else if (t === 1) { s.x0 = random(240, 320); s.y0 = random(0, 60); s.x1 = random(0, 80); s.y1 = random(180, 240); }
      else {
        const cx = random(60, 260), cy = random(40, 200), len = random(60, 140), a = random(-30, 30) * 0.05;
        s.x0 = cx - Math.trunc(Math.sin(a) * len / 2); s.y0 = cy - (len >> 1);
        s.x1 = cx + Math.trunc(Math.sin(a) * len / 2); s.y1 = cy + (len >> 1);
      }
      s.born = now; s.drawMs = random(30, 80); s.holdMs = random(200, 600); s.active = true; s.drawn = false;
    }
  }
  for (const s of swordSlashes) {
    if (!s.active) continue;
    const age = now - s.born;
    if (!s.drawn && age >= s.drawMs) {
      tft.drawLine(s.x0, s.y0, s.x1, s.y1, tft.color565(255, 255, 255));
      tft.drawLine(s.x0 + 1, s.y0, s.x1 + 1, s.y1, tft.color565(200, 40, 40));
      tft.drawLine(s.x0 - 1, s.y0, s.x1 - 1, s.y1, tft.color565(200, 40, 40));
      tft.drawLine(s.x0, s.y0 + 1, s.x1, s.y1 + 1, tft.color565(120, 20, 20));
      tft.drawLine(s.x0, s.y0 - 1, s.x1, s.y1 - 1, tft.color565(120, 20, 20));
      s.drawn = true;
    }
    if (s.drawn && age >= s.drawMs + s.holdMs) {
      tft.drawLine(s.x0 + 1, s.y0, s.x1 + 1, s.y1, BG_COLOR);
      tft.drawLine(s.x0 - 1, s.y0, s.x1 - 1, s.y1, BG_COLOR);
      tft.drawLine(s.x0, s.y0 + 1, s.x1, s.y1 + 1, BG_COLOR);
      tft.drawLine(s.x0, s.y0 - 1, s.x1, s.y1 - 1, BG_COLOR);
      tft.drawLine(s.x0, s.y0, s.x1, s.y1, BG_COLOR);
      s.active = false;
    }
  }
}

function fxTickGlitch() {
  const now = millis();
  for (let g = 0; g < 3; g++) {
    if (glitchY[g] >= 0 && now > glitchErase[g]) { tft.drawFastHLine(0, glitchY[g], 320, BG_COLOR); glitchY[g] = -1; }
    if (glitchY[g] < 0 && random(5) === 0) {
      glitchY[g] = random(0, 240);
      const gx = random(0, 200), gw = random(40, 160);
      tft.drawFastHLine(gx, glitchY[g], gw, tft.color565(0, random(80, 180), random(100, 220)));
      if (glitchY[g] + 1 < 240) tft.drawFastHLine(gx + random(-8, 8), glitchY[g] + 1, gw >> 1, tft.color565(0, random(40, 80), random(60, 120)));
      glitchErase[g] = now + random(60, 250);
    }
  }
}

function fxTickHeartbeat() {
  const now = millis();
  if (beatPhase === 0 && now - beatLast > 900) { beatLast = now; beatPhase = 1; beatR = 0; }
  if (beatPhase > 0) {
    if (beatR > 0) tft.drawCircle(160, 120, beatR - 1, BG_COLOR);
    const br = max(0, 120 - beatR * 2);
    tft.drawCircle(160, 120, beatR, tft.color565(br, 0, br >> 2));
    if (beatR > 15) { const b2 = max(0, 80 - (beatR - 15) * 3); tft.drawCircle(160, 120, beatR - 15, tft.color565(b2, 0, b2 >> 2)); }
    beatR += 5;
    if (beatR > 100) {
      tft.drawCircle(160, 120, beatR - 1, BG_COLOR);
      if (beatR > 115) tft.drawCircle(160, 120, beatR - 16, BG_COLOR);
      beatPhase = 0;
    }
  }
}

function fxTickPassOn() {
  if (random(5) === 0) { const p = fxSpawn(); if (p) { p.x = random(0, 320); p.y = random(0, 180); p.life = random(12, 32); p.vx = 0; p.vy = 0; p.type = 7; } }
  if (random(4) === 0) { const p = fxSpawn(); if (p) { p.x = 140 + random(-20, 20); p.y = 230; p.vx = random(-30, 30); p.vy = -random(40, 120); p.life = random(10, 22); p.type = 8; } }
  for (const p of fxParts) {
    if (!p.life) continue;
    if (p.type === 7) {
      const ph = p.life % 8, br = ph < 4 ? 30 + ph * 40 : 190 - (ph - 4) * 40;
      tft.drawPixel(p.x, p.y, tft.color565(br, br, min(255, br + 60)));
      p.life--;
      if (!p.life) tft.drawPixel(p.x, p.y, BG_COLOR);
    } else if (p.type === 8) {
      tft.drawPixel(p.x, p.y, BG_COLOR);
      p.x += cdiv(p.vx, 100); p.y += cdiv(p.vy, 100); p.vy += 8; p.life--;
      if (p.y < 0 || p.y >= 240 || !p.life) { p.life = 0; continue; }
      tft.drawPixel(p.x, p.y, tft.color565(255, min(255, (22 - p.life) * 12), 0));
    }
  }
}

const FX_BY_SONG = [null, fxTickFire, fxTickFeathers, fxTickStars, fxTickGlass, fxTickWaves,
                    fxTickBubbles, fxTickSlash, fxTickGlitch, fxTickHeartbeat, fxTickPassOn];
function fxTickEffect() {
  if (millis() - fxLastTick < 40) return;
  fxLastTick = millis();
  const fx = FX_BY_SONG[activeSongIdx];
  if (fx) fx();
}

function tickLyricsScreen() {
  fxTickEffect();
  if (millis() - lastLyricTick < cfg_glyphSpinMs) return;
  lastLyricTick = millis();

  if (activeLyricsLines[lyricsLineIdx].length === 0) {
    blankTimer += cfg_glyphSpinMs;
    if (blankTimer > 800) {
      blankTimer = 0;
      lyricsLineIdx = (lyricsLineIdx + 1) % activeLyricsCount;
      tft.fillScreen(BG_COLOR);
      lyricsSlots[0].active = false; lyricsSlots[1].active = false;
      lyricsActiveSlot = 0;
      lyricsLoadSlot(lyricsSlots[0], activeLyricsLines[lyricsLineIdx], 0);
    }
    return;
  }

  const cur = lyricsSlots[lyricsActiveSlot], old = lyricsSlots[1 - lyricsActiveSlot];
  if (old.active && old.erasing) {
    if (old.eraseMs === 0) {
      let minY = 240, maxY = 0;
      for (let r = 0; r < old.rowCount; r++) {
        const rlen = old.wrapped[r].length, rowBaseY = old.baseY + r * (old.ch + abs(old.dir.dy));
        minY = min(minY, rowBaseY + min(0, (rlen - 1) * old.dir.dy) - 3);
        maxY = max(maxY, rowBaseY + max(0, (rlen - 1) * old.dir.dy) + old.ch + 3);
      }
      minY = max(0, minY); maxY = min(240, maxY);
      tft.fillRect(0, minY, 320, maxY - minY, BG_COLOR);
      lyricsWaveInit[old === lyricsSlots[0] ? 0 : 1] = false;
    }
    old.eraseMs += cfg_glyphSpinMs;
    if (old.eraseMs > 2200) { old.active = false; lyricsWaveInit[old === lyricsSlots[0] ? 0 : 1] = false; }
  }
  if (cur.active) {
    if (!cur.holding) {
      if (cur.reveal < cur.totalChars) cur.reveal++;
      else { cur.holding = true; cur.holdTimer = 0; }
    } else {
      cur.holdTimer += cfg_glyphSpinMs;
      const isLast = lyricsLineIdx === activeLyricsCount - 1;
      if (isLast) lyricsSongDone = true;
      if (!isLast && cur.holdTimer > 1600) {
        lyricsLineIdx = (lyricsLineIdx + 1) % activeLyricsCount;
        cur.erasing = true; cur.eraseMs = 0;
        const next = 1 - lyricsActiveSlot;
        lyricsLoadSlot(lyricsSlots[next], activeLyricsLines[lyricsLineIdx], next);
        lyricsActiveSlot = next;
      }
    }
  }
  lyricsDrawSlot(old);
  lyricsDrawSlot(cur);
}
