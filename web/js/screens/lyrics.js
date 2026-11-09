// Song list and the lyrics viewer: two alternating line slots that scramble in,
// hold, then dissolve, over a per-song canvas effect.
import { h, header, glyph, onSwipe, reducedMotion } from '../ui.js';
import { rnd } from '../engine.js';
import { LOOP } from './play.js';
import D from '../../data.js';

export function songs(root, app) {
  root.replaceChildren(h('section', { class: 'screen' },
    header('SELECT SONG', { back: () => app.back(`/${LOOP[app.state.loop] ?? 'game'}`) }),
    h('div', { class: 'content' },
      h('ol', { class: 'menu-list songs', role: 'list' }, D.songs.map((s, i) => h('li', null,
        h('a', { class: 'menu-item', href: `#/lyrics/${i}`, onclick: (e) => { e.preventDefault(); app.go(`/lyrics/${i}`); } },
          h('span', null, s.title))))))));
}

// Diagonal reading angles from the firmware: dx staggers rows, dy slopes each row.
const DIRS = [[0, 0], [0, 0], [0, 1], [0, -1], [0, 2], [0, -2], [0, 3], [0, -3], [4, 2], [4, -2], [-4, 2], [-4, -2]];

// Greedy wrap at `cols`, breaking long words, same as the firmware slot loader.
function wrapCols(text, cols) {
  const rows = [];
  let rest = text;
  while (rest.length && rows.length < 8) {
    if (rest.length <= cols) { rows.push(rest); break; }
    let cut = rest.lastIndexOf(' ', cols);
    if (cut <= 0) cut = cols;
    rows.push(rest.slice(0, cut).trimEnd());
    rest = rest.slice(cut).trimStart();
  }
  return rows;
}

export function lyrics(root, app, life, idx) {
  const song = D.songs[idx];
  if (!song) return false;
  const lines = song.lines, n = lines.length, stepMs = app.state.settings.glyphMs;
  app.stayAwake(life);

  const canvas = h('canvas', { class: 'fx', 'aria-hidden': 'true' });
  const stage = h('div', { class: 'lyrics-stage', 'aria-hidden': 'true' });
  const live = h('p', { class: 'sr-only', 'aria-live': 'polite' });
  const leave = () => app.back('/songs');
  root.replaceChildren(h('section', { class: 'screen lyrics' },
    h('h1', { class: 'sr-only', tabindex: -1 }, song.title),
    canvas, stage, live,
    h('button', { class: 'lyrics-close', type: 'button', onclick: leave }, '‹ SONGS')));
  life.on(stage, 'click', leave);
  onSwipe(life, stage, (dir) => { if (dir === 'down') leave(); });

  const slots = [0, 1].map(() => ({ el: stage.appendChild(h('div', { class: 'slot' })), active: false, chars: [] }));
  let lineIdx = 0, current = 0, blank = 0;

  function load(si, raw) {
    const s = slots[si];
    s.el.replaceChildren();
    s.el.classList.remove('erasing');
    Object.assign(s, { active: false, chars: [], reveal: 0, holding: false, hold: 0, erasing: false, eraseMs: 0 });
    const emphasis = raw.startsWith('!');
    const text = emphasis ? raw.slice(1) : raw;
    if (!text) return;
    const big = emphasis || lineIdx === 0 || lineIdx === n - 1;
    const cols = big ? 13 : 20;
    const W = stage.clientWidth, H = stage.clientHeight;
    const cw = Math.min((W - 16) / cols, big ? 30 : 20), ch = cw * 1.5, k = cw / 12;
    const rows = wrapCols(text, cols);
    const maxLen = Math.max(...rows.map((r) => r.length));
    let [dx, dy] = big ? [0, 0] : DIRS[rnd(DIRS.length)];
    dx *= k; dy *= k;
    dx = Math.sign(dx) * Math.min(Math.abs(dx), Math.max(0, (W - 16 - maxLen * cw) / Math.max(1, rows.length)));
    const rowStep = ch + Math.abs(dy);
    const upOff = dy < 0 ? (maxLen - 1) * dy : 0;                         // highest char above baseY
    const downOff = (rows.length - 1) * rowStep + ch + (dy > 0 ? (maxLen - 1) * dy : 0);
    const blockW = maxLen * cw + Math.abs(dx) * (rows.length - 1);
    let x, y;
    if (big) {
      x = (W - maxLen * cw) / 2;
      y = (H - (downOff - upOff)) / 2 - upOff;
    } else {
      const lo = si === 0 ? 8 : H / 2 + 4, hi = si === 0 ? H / 2 - 4 : H - 8;
      const yMin = lo - upOff, yMax = Math.max(yMin, hi - downOff);
      x = 8 + Math.random() * Math.max(0, W - 16 - blockW) + (dx < 0 ? Math.abs(dx) * (rows.length - 1) : 0);
      y = yMin + Math.random() * (yMax - yMin);
    }
    s.el.style.setProperty('--cw', `${cw}px`);
    s.el.classList.toggle('big', big);
    rows.forEach((row, r) => {
      const rowX = big ? (W - row.length * cw) / 2 : x + r * dx;
      [...row].forEach((c, i) => {
        const span = h('span', { class: 'lc scr', style: `left:${rowX + i * cw}px;top:${y + r * rowStep + (big ? 0 : i * dy)}px;--i:${s.chars.length}` });
        s.el.append(span);
        s.chars.push({ span, c });
      });
    });
    s.text = text;
    s.active = true;
    paint(s);
  }

  function paint(s) {
    const total = s.chars.length;
    s.chars.forEach(({ span, c }, i) => {
      if (s.erasing) {
        const start = total > 1 ? (i * 1600) / (total - 1) : 0;
        if (s.eraseMs < start) return;
        if (s.eraseMs < start + 400) {
          span.textContent = glyph();
          span.className = 'lc fading';
          span.style.opacity = ((start + 400 - s.eraseMs) / 400).toFixed(2);
        } else span.style.visibility = 'hidden';
      } else if (i < s.reveal) {
        if (!span.dataset.on) { span.textContent = c; span.className = 'lc'; span.dataset.on = '1'; }
      } else span.textContent = glyph();
    });
  }

  function step() {
    if (!lines[lineIdx]) {
      // Blank line = stanza break: pause, clear, continue.
      blank += stepMs;
      if (blank > 800) {
        blank = 0;
        lineIdx = (lineIdx + 1) % n;
        load(1, '');
        current = 0;
        load(0, lines[lineIdx]);
      }
      return;
    }
    const cur = slots[current], old = slots[1 - current];
    if (old.active && old.erasing) {
      old.eraseMs += stepMs;
      if (old.eraseMs > 2200) load(1 - current, '');
      else paint(old);
    }
    if (!cur.active) return;
    if (!cur.holding) {
      if (cur.reveal < cur.chars.length) cur.reveal = reducedMotion.matches ? cur.chars.length : cur.reveal + 1;
      else { cur.holding = true; live.textContent = cur.text; }
      paint(cur);
    } else if (lineIdx < n - 1 && (cur.hold += stepMs) > 1600) {
      // The last line stays on screen.
      lineIdx++;
      cur.erasing = true;
      cur.el.classList.add('erasing');
      current = 1 - current;
      load(current, lines[lineIdx]);
    }
  }

  // Wait one frame so the stage has its real size before laying out the title.
  requestAnimationFrame(() => {
    load(0, lines[0]);
    life.every(stepMs, step);
    startFx(canvas, life, song.title);
  });
}

// ── Per-song canvas effects ──────────────────────
// Ported from the firmware's fxTick* functions. Coordinates are in the device's
// 320x240 space and scaled to the canvas.
const FX = {
  'IRON LOTUS': 'fire', 'FLY MY WINGS': 'feathers', HERO: 'stars', 'THROUGH PATCHES': 'glass',
  COMPASS: 'waves', 'TIAN TIAN': 'bubbles', SAIKAI: 'slash', 'STRING THEOCRACY': 'glitch',
  'FROM A PLACE OF LOVE': 'heartbeat', 'PASS ON': 'passOn',
};
const rr = (lo, hi) => lo + Math.random() * (hi - lo);

function startFx(canvas, life, title) {
  const kind = FX[title];
  if (!kind || reducedMotion.matches) return;
  const g = canvas.getContext('2d');
  let W = 0, H = 0, sx = 1, sy = 1;
  const resize = () => {
    const dpr = devicePixelRatio || 1;
    W = canvas.clientWidth; H = canvas.clientHeight;
    canvas.width = W * dpr; canvas.height = H * dpr;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    sx = W / 320; sy = H / 240;
  };
  resize();
  const ro = new ResizeObserver(resize);
  ro.observe(canvas);
  life.add(() => ro.disconnect());

  const parts = [], cracks = [], slashes = [], glitches = [];
  let waveX = 0, beatR = -1, lastBeat = 0, nextCrack = 0, nextSlash = 0;
  const dot = (x, y, col, size = 1) => {
    const d = Math.max(1.5, sx * 1.2) * size;
    g.fillStyle = col; g.fillRect(x * sx, y * sy, d, d);
  };
  const line = (x0, y0, x1, y1, col, w = 1) => {
    g.strokeStyle = col; g.lineWidth = w * Math.max(1, sx * 0.8);
    g.beginPath(); g.moveTo(x0 * sx, y0 * sy); g.lineTo(x1 * sx, y1 * sy); g.stroke();
  };
  const spawn = (p) => { if (parts.length < 48) parts.push(p); };
  const age = (p) => --p.life > 0;
  const rgb = (r, gg, b) => `rgb(${r | 0},${gg | 0},${b | 0})`;
  const twinkle = (p, period, base, stepBr) => {
    const phase = p.life % period, half = period / 2;
    const br = phase < half ? base + phase * stepBr : base + half * stepBr - (phase - half) * stepBr;
    dot(p.x, p.y, rgb(br, br, Math.min(255, br + 60)));
    return br;
  };

  const effects = {
    fire(now) {
      for (let i = rnd(3) + 2; i > 0; i--) spawn({ x: rr(0, 320), y: rr(225, 240), vx: rr(-1.2, 1.2), vy: -rr(1.5, 4), life: rr(16, 36) | 0 });
      for (const p of parts) {
        p.x += p.vx; p.y += p.vy; p.vy += 0.2;
        dot(p.x, p.y, rgb(255, Math.min(255, (36 - p.life) * 8), 0), 1.5);
      }
      for (let x = 0; x < 320; x += 2) dot(x, 235 + Math.sin(x * 0.15 + now * 0.003) * 3, rgb(255, rr(40, 140), 0));
    },
    feathers(now) {
      if (!rnd(5)) spawn({ x: rr(0, 320), y: 230, vx: rr(-0.4, 0.4), vy: -rr(0.6, 1.4), life: rr(18, 36) | 0 });
      for (const p of parts) {
        p.x += p.vx + Math.sin(now * 0.002 + p.x * 0.1); p.y += p.vy;
        const br = p.life * 7;
        dot(p.x, p.y, rgb(br / 2, br / 2, br));
      }
    },
    stars() {
      for (let i = rnd(3) + 1; i > 0; i--) spawn({ x: rr(0, 320), y: rr(0, 240), life: rr(6, 18) | 0 });
      for (const p of parts) {
        const br = twinkle(p, 6, 60, 60);
        if (br > 120) {
          const c = rgb(br / 2, br / 2, br / 2), d = 1 / sx;
          dot(p.x + d * 2, p.y, c); dot(p.x - d * 2, p.y, c); dot(p.x, p.y + 2 / sy, c); dot(p.x, p.y - 2 / sy, c);
        }
      }
    },
    glass(now) {
      if (now > nextCrack) {
        nextCrack = now + rr(1500, 3000);
        const ox = rr(20, 300), oy = rr(20, 220), a = rr(0, 6.28), len = rr(40, 120);
        const x1 = ox + Math.cos(a) * len, y1 = oy + Math.sin(a) * len;
        const ba = a + (rnd(2) ? 0.6 : -0.6) + rr(-0.2, 0.2), bl = rr(20, 70);
        const mx = (ox + x1) / 2 + rr(-10, 10), my = (oy + y1) / 2 + rr(-10, 10);
        const splinters = Array.from({ length: rnd(3) + 2 }, () => {
          const t = Math.random(), px = ox + (x1 - ox) * t, py = oy + (y1 - oy) * t;
          const sa = a + (rnd(2) ? 1.2 : -1.2), sl = rr(8, 25);
          return [px, py, px + Math.cos(sa) * sl, py + Math.sin(sa) * sl];
        });
        cracks.push({ ox, oy, x1, y1, mx, my, bx: mx + Math.cos(ba) * bl, by: my + Math.sin(ba) * bl, branch: rnd(3) !== 0, splinters, until: now + rr(3000, 7000) });
        if (cracks.length > 6) cracks.shift();
      }
      for (let i = cracks.length - 1; i >= 0; i--) {
        const c = cracks[i];
        if (now > c.until) { cracks.splice(i, 1); continue; }
        line(c.ox, c.oy, c.x1, c.y1, 'rgb(110,0,165)');
        if (c.branch) line(c.mx, c.my, c.bx, c.by, 'rgb(110,0,165)');
        for (const s of c.splinters) line(...s, 'rgb(80,0,120)');
      }
    },
    waves() {
      waveX = (waveX + 6) % 320;
      for (let x = 0; x < 320; x++) {
        dot(x, 222 + Math.sin((x + waveX) * 0.05) * 7, 'rgb(0,40,140)');
        dot(x, 226 + Math.sin((x + waveX * 1.4) * 0.09) * 5, rgb(0, 120 + rnd(30), 200));
        dot(x, 230 + Math.sin((x + waveX * 2.1) * 0.13) * 4, rgb(40, 180 + rnd(40), 255));
      }
    },
    bubbles() {
      if (!rnd(6)) spawn({ x: rr(10, 310), y: rr(200, 235), vx: rr(-0.2, 0.2), vy: -rr(0.2, 0.6), life: rr(20, 40) | 0 });
      for (const p of parts) {
        p.x += p.vx; p.y += p.vy;
        const br = p.life * 5;
        dot(p.x, p.y, rgb(br, br / 2, br), 2);
      }
    },
    slash(now) {
      if (now > nextSlash) {
        nextSlash = now + rr(1800, 4000);
        const type = rnd(3);
        let s;
        if (type === 0) s = [rr(0, 80), rr(0, 60), rr(240, 320), rr(180, 240)];
        else if (type === 1) s = [rr(240, 320), rr(0, 60), rr(0, 80), rr(180, 240)];
        else {
          const cx = rr(60, 260), cy = rr(40, 200), len = rr(60, 140), a = rr(-1.5, 1.5);
          s = [cx - Math.sin(a) * len / 2, cy - len / 2, cx + Math.sin(a) * len / 2, cy + len / 2];
        }
        const show = now + rr(30, 80);
        slashes.push({ s, show, until: show + rr(200, 600) });
      }
      for (let i = slashes.length - 1; i >= 0; i--) {
        const { s, show, until } = slashes[i];
        if (now > until) { slashes.splice(i, 1); continue; }
        if (now < show) continue;
        const [x0, y0, x1, y1] = s, o = 1 / sx;
        line(x0 + o * 2, y0, x1 + o * 2, y1, 'rgb(200,40,40)');
        line(x0 - o * 2, y0, x1 - o * 2, y1, 'rgb(200,40,40)');
        line(x0, y0, x1, y1, '#fff', 1.5);
      }
    },
    glitch(now) {
      for (let i = glitches.length - 1; i >= 0; i--) if (now > glitches[i].until) glitches.splice(i, 1);
      if (glitches.length < 3 && !rnd(5)) {
        glitches.push({ y: rr(0, 240), x: rr(0, 200), w: rr(40, 160), off: rr(-8, 8), c1: rgb(0, rr(80, 180), rr(100, 220)), c2: rgb(0, rr(40, 80), rr(60, 120)), until: now + rr(60, 250) });
      }
      for (const q of glitches) {
        g.fillStyle = q.c1; g.fillRect(q.x * sx, q.y * sy, q.w * sx, Math.max(1, sy * 0.6));
        g.fillStyle = q.c2; g.fillRect((q.x + q.off) * sx, (q.y + 1) * sy, (q.w / 2) * sx, Math.max(1, sy * 0.6));
      }
    },
    heartbeat(now) {
      if (beatR < 0 && now - lastBeat > 900) { lastBeat = now; beatR = 0; }
      if (beatR < 0) return;
      const k = Math.min(sx, sy), cx = W / 2, cy = H / 2;
      const ringAt = (r, br) => {
        if (r <= 0 || br <= 0) return;
        g.strokeStyle = rgb(br, 0, br / 4); g.lineWidth = Math.max(1.5, k);
        g.beginPath(); g.arc(cx, cy, r * k, 0, Math.PI * 2); g.stroke();
      };
      ringAt(beatR, 160 - beatR * 1.6);
      ringAt(beatR - 15, 110 - (beatR - 15) * 2);
      beatR += 5;
      if (beatR > 100) beatR = -1;
    },
    passOn() {
      if (!rnd(5)) spawn({ kind: 'star', x: rr(0, 320), y: rr(0, 180), life: rr(12, 32) | 0 });
      if (!rnd(4)) spawn({ kind: 'ember', x: 140 + rr(-20, 20), y: 230, vx: rr(-0.3, 0.3), vy: -rr(0.4, 1.2), life: rr(10, 22) | 0 });
      for (const p of parts) {
        if (p.kind === 'star') twinkle(p, 8, 30, 40);
        else {
          p.x += p.vx; p.y += p.vy; p.vy += 0.08;
          dot(p.x, p.y, rgb(255, Math.min(255, (22 - p.life) * 12), 0), 1.5);
        }
      }
    },
  };

  life.every(40, () => {
    const now = performance.now();
    g.clearRect(0, 0, W, H);
    effects[kind](now);
    for (let i = parts.length - 1; i >= 0; i--) {
      const p = parts[i];
      if (!age(p) || p.y < 0 || p.y > 240 || p.x < 0 || p.x > 320) parts.splice(i, 1);
    }
  });
}
