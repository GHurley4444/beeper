// =================================================
// tft.js — a software copy of the TFT_eSPI calls the firmware uses.
//
// The Beeper draws straight onto a 320x240 ILI9341 and never clears a frame
// unless it means to: screens repaint only what changes. This module keeps a
// persistent RGB565-style framebuffer with the same primitives (Adafruit GFX
// line / circle / triangle algorithms and the 5x7 GLCD font at sizes 1-4),
// so the firmware's drawing code ports over almost line for line and looks
// pixel-identical. Colours are 16-bit RGB565 values, exactly like on device.
// =================================================

const TFT_W = 320, TFT_H = 240;
const TFT_WHITE = 0xFFFF, TFT_BLACK = 0x0000;

// RGB565 -> canvas RGBA (little-endian 0xAABBGGRR), precomputed.
const C565 = new Uint32Array(65536);
for (let v = 0; v < 65536; v++) {
  const r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31;
  const r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
  C565[v] = (0xFF000000 | (b << 16) | (g << 8) | r) >>> 0;
}

const tft = {
  canvas: null, ctx: null, img: null, fb: null, dirty: true,
  cx: 0, cy: 0, size: 1, fg: 0xFFFF, bg: 0xFFFF,

  init(canvas) {
    this.canvas = canvas;
    canvas.width = TFT_W; canvas.height = TFT_H;
    this.ctx = canvas.getContext('2d', { alpha: false });
    this.img = this.ctx.createImageData(TFT_W, TFT_H);
    this.fb = new Uint32Array(this.img.data.buffer);
    this.fillScreen(0);
  },

  // Push the framebuffer to the canvas (called once per animation frame).
  present() {
    if (!this.dirty) return;
    this.ctx.putImageData(this.img, 0, 0);
    this.dirty = false;
  },

  color565(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3); },

  // ── no-ops kept so ported code reads the same ──
  startWrite() {}, endWrite() {}, writecommand() {}, setSwapBytes() {},

  drawPixel(x, y, c) {
    x |= 0; y |= 0;
    if (x < 0 || y < 0 || x >= TFT_W || y >= TFT_H) return;
    this.fb[y * TFT_W + x] = C565[c & 0xFFFF];
    this.dirty = true;
  },

  fillRect(x, y, w, h, c) {
    x |= 0; y |= 0; w |= 0; h |= 0;
    if (w <= 0 || h <= 0) return;
    let x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > TFT_W) x1 = TFT_W; if (y1 > TFT_H) y1 = TFT_H;
    if (x >= x1 || y >= y1) return;
    const col = C565[c & 0xFFFF], fb = this.fb;
    for (let yy = y; yy < y1; yy++) fb.fill(col, yy * TFT_W + x, yy * TFT_W + x1);
    this.dirty = true;
  },

  fillScreen(c) { this.fillRect(0, 0, TFT_W, TFT_H, c); },
  drawFastHLine(x, y, w, c) { this.fillRect(x, y, w, 1, c); },
  drawFastVLine(x, y, h, c) { this.fillRect(x, y, 1, h, c); },

  drawRect(x, y, w, h, c) {
    this.drawFastHLine(x, y, w, c);
    this.drawFastHLine(x, y + h - 1, w, c);
    this.drawFastVLine(x, y, h, c);
    this.drawFastVLine(x + w - 1, y, h, c);
  },

  // Bresenham, as Adafruit GFX writeLine.
  drawLine(x0, y0, x1, y1, c) {
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    const steep = Math.abs(y1 - y0) > Math.abs(x1 - x0);
    if (steep) { [x0, y0] = [y0, x0]; [x1, y1] = [y1, x1]; }
    if (x0 > x1) { [x0, x1] = [x1, x0]; [y0, y1] = [y1, y0]; }
    const dx = x1 - x0, dy = Math.abs(y1 - y0);
    let err = (dx / 2) | 0;
    const ystep = y0 < y1 ? 1 : -1;
    for (; x0 <= x1; x0++) {
      if (steep) this.drawPixel(y0, x0, c); else this.drawPixel(x0, y0, c);
      err -= dy;
      if (err < 0) { y0 += ystep; err += dx; }
    }
  },

  drawCircle(x0, y0, r, c) {
    x0 |= 0; y0 |= 0; r |= 0;
    if (r < 0) return;
    let f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    this.drawPixel(x0, y0 + r, c); this.drawPixel(x0, y0 - r, c);
    this.drawPixel(x0 + r, y0, c); this.drawPixel(x0 - r, y0, c);
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      this.drawPixel(x0 + x, y0 + y, c); this.drawPixel(x0 - x, y0 + y, c);
      this.drawPixel(x0 + x, y0 - y, c); this.drawPixel(x0 - x, y0 - y, c);
      this.drawPixel(x0 + y, y0 + x, c); this.drawPixel(x0 - y, y0 + x, c);
      this.drawPixel(x0 + y, y0 - x, c); this.drawPixel(x0 - y, y0 - x, c);
    }
  },

  fillCircle(x0, y0, r, c) {
    x0 |= 0; y0 |= 0; r |= 0;
    this.drawFastVLine(x0, y0 - r, 2 * r + 1, c);
    let f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r, px = x, py = y;
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      if (x < y + 1) {
        this.drawFastVLine(x0 + x, y0 - y, 2 * y + 1, c);
        this.drawFastVLine(x0 - x, y0 - y, 2 * y + 1, c);
      }
      if (y !== py) {
        this.drawFastVLine(x0 + py, y0 - px, 2 * px + 1, c);
        this.drawFastVLine(x0 - py, y0 - px, 2 * px + 1, c);
        py = y;
      }
      px = x;
    }
  },

  // TFT_eSPI drawEllipse (midpoint, two regions).
  drawEllipse(x0, y0, rx, ry, c) {
    x0 |= 0; y0 |= 0; rx |= 0; ry |= 0;
    if (rx < 2 || ry < 2) return;
    const rx2 = rx * rx, ry2 = ry * ry, fx2 = 4 * rx2, fy2 = 4 * ry2;
    let x, y, s;
    for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++) {
      this.drawPixel(x0 + x, y0 + y, c); this.drawPixel(x0 - x, y0 + y, c);
      this.drawPixel(x0 - x, y0 - y, c); this.drawPixel(x0 + x, y0 - y, c);
      if (s >= 0) { s += fx2 * (1 - y); y--; }
      s += ry2 * (4 * x + 6);
    }
    for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++) {
      this.drawPixel(x0 + x, y0 + y, c); this.drawPixel(x0 - x, y0 + y, c);
      this.drawPixel(x0 - x, y0 - y, c); this.drawPixel(x0 + x, y0 - y, c);
      if (s >= 0) { s += fy2 * (1 - x); x--; }
      s += rx2 * (4 * y + 6);
    }
  },

  // Adafruit GFX fillTriangle (scanline).
  fillTriangle(x0, y0, x1, y1, x2, y2, c) {
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0; x2 |= 0; y2 |= 0;
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0]; }
    if (y1 > y2) { [y2, y1] = [y1, y2]; [x2, x1] = [x1, x2]; }
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0]; }
    let a, b, y, last;
    if (y0 === y2) {
      a = b = x0;
      if (x1 < a) a = x1; else if (x1 > b) b = x1;
      if (x2 < a) a = x2; else if (x2 > b) b = x2;
      this.drawFastHLine(a, y0, b - a + 1, c);
      return;
    }
    const dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0,
          dx12 = x2 - x1, dy12 = y2 - y1;
    let sa = 0, sb = 0;
    last = (y1 === y2) ? y1 : y1 - 1;
    for (y = y0; y <= last; y++) {
      a = x0 + Math.trunc(sa / dy01); b = x0 + Math.trunc(sb / dy02);
      sa += dx01; sb += dx02;
      if (a > b) [a, b] = [b, a];
      this.drawFastHLine(a, y, b - a + 1, c);
    }
    sa = dx12 * (y - y1); sb = dx02 * (y - y0);
    for (; y <= y2; y++) {
      a = x1 + Math.trunc(sa / dy12); b = x0 + Math.trunc(sb / dy02);
      sa += dx12; sb += dx02;
      if (a > b) [a, b] = [b, a];
      this.drawFastHLine(a, y, b - a + 1, c);
    }
  },

  // Draw a 565 image (Uint16Array, row stride = w) at x,y.
  pushImage(x, y, w, h, data, offset = 0) {
    for (let r = 0; r < h; r++) {
      const py = y + r;
      if (py < 0 || py >= TFT_H) continue;
      for (let col = 0; col < w; col++) {
        const px = x + col;
        if (px < 0 || px >= TFT_W) continue;
        this.fb[py * TFT_W + px] = C565[data[offset + r * w + col]];
      }
    }
    this.dirty = true;
  },

  // ── text (font 1: 6x8 cells, scaled by textsize) ──
  setTextSize(s) { this.size = Math.max(1, s | 0); },
  setTextColor(fg, bg) { this.fg = fg; this.bg = (bg === undefined) ? fg : bg; },
  setCursor(x, y) { this.cx = x | 0; this.cy = y | 0; },

  drawChar(x, y, code, fg, bg, size) {
    // Matches TFT_eSPI: background cell only when bg != fg.
    if (code > 255 || code < 0) code = 45; // '-'
    const base = code * 5, opaque = (fg !== bg);
    if (opaque) this.fillRect(x, y, 6 * size, 8 * size, bg);
    for (let i = 0; i < 5; i++) {
      let line = GLCD_FONT[base + i];
      for (let j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) {
          if (size === 1) this.drawPixel(x + i, y + j, fg);
          else this.fillRect(x + i * size, y + j * size, size, size, fg);
        }
      }
    }
  },

  print(v) {
    const s = (typeof v === 'number') ? String(v) : String(v);
    for (let k = 0; k < s.length; k++) {
      const ch = s.charCodeAt(k);
      if (ch === 10) { this.cy += 8 * this.size; this.cx = 0; continue; }
      if (ch === 13) continue;
      // TFT_eSPI wraps text at the right edge.
      if (this.cx + 6 * this.size > TFT_W) { this.cy += 8 * this.size; this.cx = 0; }
      this.drawChar(this.cx, this.cy, ch > 126 ? 45 : ch, this.fg, this.bg, this.size);
      this.cx += 6 * this.size;
    }
  },
};
