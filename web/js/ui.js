// Small DOM helpers shared by every screen.
import { rnd } from './engine.js';

// h('button', { class: 'btn', onclick }, 'LABEL') -> element. Text children are always
// inserted as text, never parsed as HTML.
export function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v == null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'class') el.className = v;
    else if (k === 'style') el.style.cssText = v;
    else if (k.includes('-') || !(k in el)) el.setAttribute(k, v === true ? '' : v);
    else el[k] = v;
  }
  el.append(...kids.flat(Infinity).filter((c) => c != null && c !== false));
  return el;
}

export const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
export const glyph = () => String.fromCharCode(33 + rnd(93)); // printable ASCII, like random(33,126)

// Scramble-reveal: characters lock in left to right while the rest flicker as
// random glyphs. Spaces stay spaces so line wrapping never jumps.
// Screen readers get the plain text; the flicker is hidden from them.
// `voice` (optional) is a sound factory like beep.decode: it starts with the
// reveal, steps with each locked character, and finishes or is cut off with it.
export function scramble(el, text, { stepMs = 90, perStep = 1, frameMs = 66, onDone, voice } = {}) {
  const done = h('span'), rest = h('span', { class: 'scr' });
  el.replaceChildren(h('span', { class: 'sr-only' }, text), h('span', { 'aria-hidden': 'true' }, done, rest));
  let i = reducedMotion.matches ? text.length : 0, last = performance.now(), id = 0;
  const paint = () => {
    done.textContent = text.slice(0, i);
    rest.textContent = text.slice(i).replace(/\S/g, glyph);
  };
  paint();
  if (i >= text.length) { queueMicrotask(() => onDone?.()); return () => {}; }
  const v = voice?.();
  const finish = () => { clearInterval(id); id = 0; v?.done(); onDone?.(); };
  id = setInterval(() => {
    const now = performance.now();
    if (now - last >= stepMs) { i = Math.min(text.length, i + perStep); last = now; v?.step(); }
    paint();
    if (i >= text.length) finish();
  }, Math.min(frameMs, stepMs));
  return () => { if (id) { clearInterval(id); id = 0; v?.cancel(); } };
}

// Per-screen lifetime: timers and listeners registered here are removed
// automatically when the user navigates away.
export function lifetime() {
  const cleanups = [];
  return {
    every(ms, fn) { const id = setInterval(fn, ms); cleanups.push(() => clearInterval(id)); return id; },
    after(ms, fn) { const id = setTimeout(fn, ms); cleanups.push(() => clearTimeout(id)); return id; },
    on(target, type, fn, opts) { target.addEventListener(type, fn, opts); cleanups.push(() => target.removeEventListener(type, fn, opts)); },
    add(fn) { if (typeof fn === 'function') cleanups.push(fn); },
    end() { while (cleanups.length) cleanups.pop()(); },
  };
}

// Swipe detection over an element. cb receives 'left' | 'right' | 'up' | 'down'.
// A swipe that starts on a button must not also count as a tap on it.
export function onSwipe(life, el, cb, threshold = 40) {
  let start = null;
  life.on(el, 'pointerdown', (e) => { if (e.isPrimary) start = { x: e.clientX, y: e.clientY }; });
  life.on(el, 'pointercancel', () => { start = null; });
  life.on(el, 'pointerup', (e) => {
    if (!start || !e.isPrimary) return;
    const dx = e.clientX - start.x, dy = e.clientY - start.y;
    start = null;
    if (Math.max(Math.abs(dx), Math.abs(dy)) < threshold) return;
    const swallow = (ev) => { ev.stopPropagation(); ev.preventDefault(); };
    window.addEventListener('click', swallow, { capture: true, once: true });
    setTimeout(() => window.removeEventListener('click', swallow, { capture: true }), 400);
    cb(Math.abs(dx) > Math.abs(dy) ? (dx < 0 ? 'left' : 'right') : (dy < 0 ? 'up' : 'down'));
  });
}

export function header(title, { back, right, tone } = {}) {
  return h('header', { class: `bar${tone ? ' bar-' + tone : ''}` },
    back ? h('button', { class: 'bar-back', type: 'button', onclick: back, 'aria-label': 'Back' }, '‹ BACK') : h('span'),
    h('h1', { class: 'bar-title' }, title),
    right ?? h('span'));
}

// [-] value [+] control. get/set read and write the number; wrap cycles min..max.
export function stepper(label, { get, set, min = 0, max = 99, wrap = false, format = String }) {
  const out = h('output', { class: 'stepper-value', 'aria-live': 'polite' });
  const paint = () => { out.textContent = format(get()); };
  const bump = (d) => {
    let v = get() + d;
    if (wrap) v = v > max ? min : v < min ? max : v;
    else v = Math.min(max, Math.max(min, v));
    set(v); paint();
  };
  paint();
  const el = h('div', { class: 'stepper', role: 'group', 'aria-label': label },
    h('span', { class: 'stepper-label' }, label),
    h('button', { class: 'btn btn-sq', type: 'button', 'aria-label': `Decrease ${label}`, onclick: () => bump(-1) }, '−'),
    out,
    h('button', { class: 'btn btn-sq', type: 'button', 'aria-label': `Increase ${label}`, onclick: () => bump(1) }, '+'));
  el.sync = paint; // re-read the value after something else changed it
  return el;
}

// A row of mutually exclusive options rendered as toggle buttons.
export function choices(label, options, { get, set }) {
  const buttons = options.map(([text, value]) => h('button', {
    class: 'btn', type: 'button', onclick: () => { set(value); paint(); },
  }, text));
  const group = h('div', { class: 'choices', role: 'group', 'aria-label': label }, buttons);
  const paint = () => buttons.forEach((b, i) => b.setAttribute('aria-pressed', String(get() === options[i][1])));
  paint();
  return h('div', { class: 'field' }, h('div', { class: 'field-label' }, label), group);
}

// Native <dialog> confirm. Resolves true when the user confirms.
export function confirmDialog(message, okLabel) {
  return new Promise((resolve) => {
    const dlg = h('dialog', { class: 'dialog', 'aria-label': 'Confirm' },
      h('p', null, message),
      h('form', { method: 'dialog', class: 'dialog-actions' },
        h('button', { class: 'btn', value: 'cancel', autofocus: true }, 'CANCEL'),
        h('button', { class: 'btn btn-danger', value: 'ok' }, okLabel)));
    dlg.addEventListener('close', () => { resolve(dlg.returnValue === 'ok'); dlg.remove(); });
    document.body.append(dlg);
    dlg.showModal();
  });
}
