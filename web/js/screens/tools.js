// Side tools: D20, coin flip, tarot, oracle, countdown timer, word generator, cipher.
import { h, header, scramble, stepper, confirmDialog, reducedMotion } from '../ui.js';
import { rnd, pick, pad2 } from '../engine.js';
import D from '../../data.js';
import { beep } from '../sound.js';

const TOOLS = { dice, coin, tarot, oracle, countdown, words, cipher };

export function open(root, app, life, name) {
  const tool = TOOLS[name];
  if (!tool) return false;
  const [title, body, opts] = tool(app, life);
  root.replaceChildren(h('section', { class: `screen tool tool-${name}` },
    header(title, { back: () => app.back('/tools'), tone: 'tool', ...opts }),
    h('div', { class: 'content center' }, body)));
}

// ── D20 ──────────────────────────────────────────
const SVG = 'http://www.w3.org/2000/svg';
const s = (tag, attrs) => {
  const el = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  return el;
};
const ring = (r) => Array.from({ length: 20 }, (_, i) => {
  const a = -Math.PI / 2 + (i * 2 * Math.PI) / 20;
  return `${(100 + r * Math.cos(a)).toFixed(2)},${(100 + r * Math.sin(a)).toFixed(2)}`;
}).join(' ');

function dice(app, life) {
  const face = s('polygon', { points: ring(85), class: 'd20-face' });
  const num = s('text', { x: 100, y: 100, class: 'd20-num', 'dominant-baseline': 'central', 'text-anchor': 'middle' });
  const svg = s('svg', { viewBox: '0 0 200 200', class: 'd20', 'aria-hidden': 'true' });
  svg.append(face, s('polygon', { points: ring(51), class: 'd20-inner' }), num);
  const verdict = h('p', { class: 'd20-verdict', 'aria-live': 'polite' });
  const btn = h('button', { class: 'd20-btn', type: 'button', 'aria-label': 'Roll the D20' }, svg);

  const roll = () => {
    const final = 1 + rnd(20);
    let frame = 0, settle = 0;
    svg.dataset.state = 'spin';
    verdict.textContent = '';
    const step = () => {
      if (frame < 40) {
        frame++;
        num.textContent = 1 + rnd(20);
        svg.style.transform = `translate(${rnd(7) - 3}px, ${rnd(7) - 3}px)`;
        return life.after(30, step);
      }
      svg.style.transform = '';
      if (settle < 20) {
        settle++;
        num.textContent = settle % 2 === 0 ? final : 1 + rnd(20);
        return life.after(30 + (180 * settle) / 20, step);
      }
      num.textContent = final;
      svg.dataset.state = final === 20 ? 'crit' : final === 1 ? 'fumble' : 'done';
      verdict.textContent = final === 20 ? 'NAT 20!' : final === 1 ? 'NAT 1.' : `ROLLED ${final}`;
    };
    if (reducedMotion.matches) { frame = 40; settle = 20; }
    step();
  };
  btn.addEventListener('click', roll);
  roll();
  return ['ROLL D20', [h('p', { class: 'd20-top' }, verdict), btn, h('p', { class: 'hint' }, 'TAP TO REROLL')]];
}

// ── Coin flip ────────────────────────────────────
function coin(app, life) {
  const faceEl = h('span', { class: 'coin-face' }, '?');
  const coinEl = h('button', { class: 'coin', type: 'button', 'aria-label': 'Flip the coin' }, faceEl);
  const word = h('p', { class: 'coin-word', 'aria-live': 'polite' }, 'TAP TO FLIP');
  let spinning = false;
  coinEl.addEventListener('click', () => {
    if (spinning) return;
    spinning = true;
    const heads = rnd(2) === 1;
    coinEl.className = 'coin';
    word.textContent = '';
    let frame = reducedMotion.matches ? 24 : 0;
    const step = () => {
      if (frame >= 24) {
        spinning = false;
        coinEl.style.transform = '';
        faceEl.textContent = heads ? 'H' : 'T';
        coinEl.className = `coin ${heads ? 'heads' : 'tails'}`;
        word.textContent = heads ? 'HEADS' : 'TAILS';
        return;
      }
      // Width oscillates to fake a tumble, as on the device.
      const phase = frame % 8;
      coinEl.style.transform = `scaleX(${((phase < 4 ? phase : 8 - phase) * 15 + 5) / 65})`;
      faceEl.textContent = phase % 2 ? 'T' : 'H';
      frame++;
      life.after(60, step);
    };
    step();
  });
  return ['COIN FLIP', [coinEl, word]];
}

// ── Tarot ────────────────────────────────────────
function tarot(app, life) {
  const t = app.state.tarot, glyphMs = app.state.settings.glyphMs;
  const remaining = h('span', { class: 'bar-note' });
  const view = h('div', { class: 'tarot-view' });
  let stop = () => {};
  life.add(() => stop());

  const draw = () => {
    if (t.pos >= 22 || t.deck.length !== 22) {
      t.deck = [...Array(22).keys()];
      for (let i = 21; i > 0; i--) { const j = rnd(i + 1); [t.deck[i], t.deck[j]] = [t.deck[j], t.deck[i]]; }
      t.pos = 0;
    }
    const idx = t.deck[t.pos++];
    t.history = [...t.history, idx].slice(-10);
    app.save();
    showCard(idx);
  };

  const showCard = (idx) => {
    stop();
    const card = D.tarot[idx];
    const name = h('h2', { class: 'tarot-name' }), desc = h('p', { class: 'tarot-desc' });
    const rule = h('hr', { class: 'tarot-rule', hidden: true });
    view.replaceChildren(h('button', { class: 'tarot-card drawn', type: 'button', onclick: draw, 'aria-label': 'Draw the next card' }, name, rule, desc),
      h('p', { class: 'hint' }, 'TAP TO DRAW NEXT CARD'));
    stop = scramble(name, card.name, {
      stepMs: glyphMs, onTick: beep.tick,
      onDone: () => { rule.hidden = false; stop = scramble(desc, card.desc, { stepMs: glyphMs, onTick: beep.tick }); },
    });
    paintCount();
  };

  const showBack = () => {
    stop();
    view.replaceChildren(h('button', { class: 'tarot-card', type: 'button', onclick: draw },
      h('span', { class: 'tarot-back' }, 'MAJOR ARCANA')), h('p', { class: 'hint' }, 'TAP TO DRAW A CARD'));
    paintCount();
  };

  const showHistory = () => {
    stop();
    const items = [...t.history].reverse();
    view.replaceChildren(
      items.length
        ? h('ol', { class: 'history', role: 'list' }, items.map((i) => h('li', null,
          h('p', { class: 'tarot-hist-name' }, D.tarot[i].name), h('p', { class: 'menu-note' }, D.tarot[i].desc))))
        : h('p', { class: 'empty' }, 'NO CARDS DRAWN YET'),
      h('div', { class: 'row' },
        h('button', { class: 'btn', type: 'button', onclick: showBack }, 'CARD'),
        h('button', {
          class: 'btn', type: 'button', disabled: !items.length,
          onclick: async () => {
            if (!(await confirmDialog('CLEAR YOUR TAROT HISTORY? THIS CAN’T BE UNDONE.', 'CLEAR'))) return;
            t.history = []; app.save(); showHistory();
          },
        }, 'CLEAR HISTORY')));
  };

  const paintCount = () => { remaining.textContent = `${22 - Math.min(22, t.pos)}/22 LEFT`; };
  showBack();
  return ['TAROT DRAW', [view, h('button', { class: 'btn', type: 'button', onclick: showHistory }, 'HISTORY')],
    { right: remaining }];
}

// ── Oracle ───────────────────────────────────────
function oracle(app, life) {
  const text = h('p', { class: 'orb-text' }, 'TAP THE ORB');
  let stop = () => {};
  life.add(() => stop());
  const orb = h('button', {
    class: 'orb', type: 'button', 'aria-label': 'Consult the oracle',
    onclick: () => { stop(); stop = scramble(text, pick(D.oracle), { stepMs: 80, onTick: beep.tick }); },
  }, text);
  return ['ORACLE', [orb, h('p', { class: 'hint' }, 'ASK A QUESTION, THEN TAP THE ORB')]];
}

// ── Countdown timer ──────────────────────────────
// Kept in saved state against the wall clock, so it survives the app being closed.
function countdown(app, life) {
  const c = app.state.countdown;
  const wrap = h('div', { class: 'cd' });
  const totalMs = () => ((c.h * 60 + c.m) * 60 + c.s) * 1000;
  const remaining = () => (c.status === 'paused' ? c.pauseRem : Math.max(0, c.target - Date.now()));
  const set = (patch) => { Object.assign(c, patch); app.save(); paint(); };

  let tick = () => {};

  // Rebuilt only when the status changes; `tick` updates the numbers in place so
  // buttons keep focus while the clock runs.
  const paint = () => {
    if (c.status === 'running' && remaining() === 0) c.status = 'done';
    if (c.status === 'set') {
      tick = () => {};
      const field = (label, key, max) => stepper(label, { get: () => c[key], set: (v) => { c[key] = v; app.save(); start.disabled = !totalMs(); }, max, wrap: true, format: pad2 });
      const start = h('button', {
        class: 'btn btn-primary', type: 'button', disabled: !totalMs(),
        onclick: () => set({ status: 'running', target: Date.now() + totalMs() }),
      }, 'START');
      wrap.replaceChildren(h('div', { class: 'steppers' }, field('HOURS', 'h', 23), field('MINUTES', 'm', 59), field('SECONDS', 's', 59)), start);
      return;
    }
    const done = c.status === 'done';
    const fill = h('div', { class: 'cd-fill' });
    const bar = h('div', { class: 'cd-bar', role: 'progressbar', 'aria-label': 'Time remaining', 'aria-valuemin': 0, 'aria-valuemax': 100 }, fill);
    const time = h('p', { class: `cd-time${done ? ' done' : ''}`, role: 'timer' });
    tick = () => {
      const rem = remaining(), frac = totalMs() ? rem / totalMs() : 0, sec = Math.ceil(rem / 1000);
      if (c.status === 'running' && rem === 0) { app.save(); beep.alarm(); return paint(); }
      fill.style.transform = `scaleX(${frac})`;
      bar.classList.toggle('low', frac < 0.25);
      bar.setAttribute('aria-valuenow', Math.round(frac * 100));
      time.classList.toggle('flash', !done && c.status === 'running' && rem < 10000);
      time.textContent = `${pad2(Math.floor(sec / 3600))}:${pad2(Math.floor((sec % 3600) / 60))}:${pad2(sec % 60)}`;
    };
    tick();
    wrap.replaceChildren(bar, time,
      h('p', { class: done ? 'cd-status done' : 'cd-status', 'aria-live': 'polite' }, done ? 'TIME IS UP' : c.status === 'paused' ? 'PAUSED' : 'RUNNING'),
      h('div', { class: 'row' },
        done ? null : h('button', {
          class: 'btn', type: 'button',
          onclick: () => (c.status === 'paused'
            ? set({ status: 'running', target: Date.now() + c.pauseRem })
            : set({ status: 'paused', pauseRem: remaining() })),
        }, c.status === 'paused' ? 'RESUME' : 'PAUSE'),
        h('button', { class: 'btn', type: 'button', onclick: () => set({ status: 'set' }) }, 'RESET')));
  };
  paint();
  life.every(250, () => tick());
  app.stayAwake(life);
  return ['COUNTDOWN', [wrap]];
}

// ── Word generator ───────────────────────────────
function words(app, life) {
  const cats = Object.keys(D.words);
  let cat = cats[0], stop = () => {};
  life.add(() => stop());
  const result = h('p', { class: 'word-result', 'aria-live': 'polite' });
  const buttons = cats.map((k) => h('button', {
    class: 'seg-btn', role: 'tab', type: 'button',
    onclick: () => { cat = k; stop(); result.textContent = ''; paintTabs(); },
  }, k));
  const tabs = h('div', { class: 'seg', role: 'tablist', 'aria-label': 'Word type' }, buttons);
  const paintTabs = () => buttons.forEach((b, i) => b.setAttribute('aria-selected', String(cats[i] === cat)));
  paintTabs();
  return ['WORD GENERATOR', [tabs, result,
    h('button', { class: 'btn btn-primary', type: 'button', onclick: () => { stop(); stop = scramble(result, pick(D.words[cat]), { stepMs: 60, onTick: beep.tick }); } }, 'GENERATE')]];
}

// ── Cipher wheel (Caesar shift) ──────────────────
export function caesar(text, shift) {
  return text.replace(/[A-Z]/g, (ch) => String.fromCharCode(65 + ((ch.charCodeAt(0) - 65 + shift + 26) % 26)));
}

function cipher() {
  let encode = true, shift = 13;
  const input = h('input', {
    id: 'cipher-in', name: 'message', class: 'input', type: 'text', maxLength: 64, autocomplete: 'off', autocapitalize: 'characters', spellcheck: false,
    placeholder: 'TYPE A MESSAGE…', oninput: () => paint(),
  });
  const output = h('output', { class: 'cipher-out', for: 'cipher-in', 'aria-live': 'polite' });
  const modes = [['ENCODE', true], ['DECODE', false]].map(([label, v]) => h('button', {
    class: 'seg-btn', type: 'button', onclick: () => { encode = v; paint(); },
  }, label));
  const mode = h('div', { class: 'seg', role: 'group', 'aria-label': 'Mode' }, modes);
  const paint = () => {
    output.textContent = caesar(input.value.toUpperCase(), encode ? shift : -shift) || ' ';
    modes.forEach((b, i) => b.setAttribute('aria-pressed', String(encode === (i === 0))));
  };
  paint();
  return ['CIPHER WHEEL', [mode,
    stepper('SHIFT', { get: () => shift, set: (v) => { shift = v; paint(); }, min: 1, max: 25 }),
    h('label', { class: 'field-label', for: 'cipher-in' }, 'INPUT'), input,
    h('p', { class: 'field-label' }, 'OUTPUT'), output]];
}
