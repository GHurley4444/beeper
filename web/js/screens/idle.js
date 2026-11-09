// Idle screensaver: logo, lifetime stats, drifting phrases that scramble in and
// out, and a scan line. Any touch or key returns to the prescript.
import { h, glyph, lifetime, reducedMotion } from '../ui.js';
import { rnd, pick } from '../engine.js';
import D from '../../data.js';

const COLOURS = ['#2c6ea4', '#008a70', '#6a52b0', '#8a7c2a', '#2a8a52', '#8c3a52'];
const PARTICLES = 4;

export function openIdle(app) {
  const life = lifetime();
  const total = app.state.menus.reduce((n, m) => n + m.pass, 0);
  const best = Math.max(...app.state.menus.map((m) => m.best));
  const field = h('div', { class: 'idle-field', 'aria-hidden': 'true' });
  const scan = h('div', { class: 'idle-scan', 'aria-hidden': 'true' });
  const centre = h('div', { class: 'idle-centre' },
    h('img', { class: 'logo', src: 'img/logo.png', alt: '', width: 320, height: 96 }),
    h('p', { class: 'idle-stats' }, `TOTAL ${total}  ·  BEST STREAK ${best}`));
  const panel = h('div', { class: 'idle', role: 'dialog', 'aria-modal': 'true', 'aria-label': 'Idle. Tap to wake.', tabindex: -1 },
    field, scan, centre, h('p', { class: 'sr-only' }, 'Tap or press any key to wake.'));
  const overlay = app.openOverlay(panel);
  panel.focus({ preventScroll: true });
  app.stayAwake(life);

  const wake = () => { life.end(); overlay.close(); };
  // Ignore the touch that may still be in progress when the screensaver opens.
  life.after(300, () => {
    life.on(panel, 'pointerdown', wake);
    life.on(window, 'keydown', wake);
  });
  if (reducedMotion.matches) return;

  // Phrases: reveal one char per glyph step, hold 2.4s, then respawn elsewhere.
  const stepMs = app.state.settings.glyphMs;
  const parts = [];
  const overlaps = (a, b) => a.x < b.x + b.w + 8 && a.x + a.w + 8 > b.x && a.y < b.y + b.h + 4 && a.y + a.h + 4 > b.y;
  const spawn = (p) => {
    const W = field.clientWidth, H = field.clientHeight, logo = centre.getBoundingClientRect();
    const keepOut = { x: logo.left, y: logo.top, w: logo.width, h: logo.height };
    const inUse = new Set(parts.filter((q) => q !== p).map((q) => q.text));
    let text = pick(D.idle);
    for (let i = 0; i < 20 && inUse.has(text); i++) text = pick(D.idle);
    p.el.textContent = text;
    p.text = text;
    p.w = p.el.offsetWidth; p.h = p.el.offsetHeight;
    for (let i = 0; i < 12; i++) {
      p.x = 8 + rnd(Math.max(1, W - p.w - 16));
      p.y = 8 + rnd(Math.max(1, H - p.h - 16));
      if (!overlaps(p, keepOut) && !parts.some((q) => q !== p && q.x !== undefined && overlaps(p, q))) break;
    }
    Object.assign(p.el.style, { left: `${p.x}px`, top: `${p.y}px`, color: pick(COLOURS) });
    p.rev = 0; p.phase = 'reveal'; p.since = performance.now();
  };
  for (let i = 0; i < PARTICLES; i++) {
    const p = { el: field.appendChild(h('span', { class: 'idle-phrase' })) };
    parts.push(p);
    spawn(p);
    p.since -= i * 1800; // stagger so they don't all turn over together
  }

  life.every(100, () => {
    const now = performance.now();
    for (const p of parts) {
      if (p.phase === 'reveal') {
        if (now - p.since > stepMs) { p.rev++; p.since = now; }
        p.el.textContent = p.text.slice(0, p.rev) + [...p.text.slice(p.rev)].map(glyph).join('');
        if (p.rev >= p.text.length) { p.phase = 'hold'; p.since = now; }
      } else if (now - p.since > 2400) spawn(p);
    }
  });

  // Occasional short glitch line, as on the device.
  life.every(350, () => {
    if (rnd(3)) return;
    const g = h('div', { class: 'idle-glitch', style: `top:${rnd(100)}%;left:${rnd(80)}%;width:${10 + rnd(20)}%` });
    field.append(g);
    life.after(60 + rnd(120), () => g.remove());
  });
}
