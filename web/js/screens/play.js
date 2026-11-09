// The five swipeable prescript screens: GAME, CITY, CON, TASK and TIMED.
import { h, scramble, onSwipe } from '../ui.js';
import * as E from '../engine.js';
import { showResult } from './result.js';
import { beep } from '../sound.js';

export const LOOP = ['game', 'city', 'con', 'task', 'timed'];
const TAB_NAMES = ['GAME', 'CITY', 'CON', 'TASK', 'TIME'];

export function play(root, app, life, idx) {
  app.state.loop = idx;
  app.save();
  const goTab = (i) => app.go(`/${LOOP[(i + 5) % 5]}`, { replace: true });

  const tabs = h('nav', { class: 'tabs', 'aria-label': 'Prescript menus' },
    LOOP.map((p, i) => h('a', {
      class: 'tab', href: `#/${p}`, 'aria-current': i === idx ? 'page' : null,
      onclick: (e) => { e.preventDefault(); goTab(i); },
    }, TAB_NAMES[i])));
  const body = h('div', { class: 'play-body' });
  const page = h('section', { class: `screen play ${idx === E.TIMED ? 'is-timed' : ''}` }, tabs, body);
  root.replaceChildren(page);

  // Swipe left/right between menus, down for the main menu, up for songs.
  onSwipe(life, page, (dir) => {
    if (dir === 'left') goTab(idx + 1);
    else if (dir === 'right') goTab(idx - 1);
    else if (dir === 'down') app.go('/menu');
    else if (dir === 'up') app.go('/songs');
  });
  life.on(window, 'keydown', (e) => {
    if (e.target.closest?.('input, textarea, dialog')) return;
    if (e.key === 'ArrowRight') goTab(idx + 1);
    if (e.key === 'ArrowLeft') goTab(idx - 1);
  });

  (idx === E.TIMED ? timedView : prescriptView)(body, app, life, idx);
}

const logo = () => h('img', { class: 'logo', src: 'img/logo.png', alt: '', width: 320, height: 96, draggable: false, fetchpriority: 'high' });
const menuButton = (app) => h('button', { class: 'chip-btn', type: 'button', onclick: () => app.go('/menu') }, 'MENU');

function sessionClock(app, life) {
  const el = h('p', { class: 'session', 'aria-label': 'Time this session' });
  const paint = () => {
    const min = Math.floor((Date.now() - app.session) / 60000);
    el.textContent = `SESSION ${E.pad2(Math.floor(min / 60))}:${E.pad2(min % 60)}`;
  };
  paint();
  life.on(window, 'beeper:second', paint);
  return el;
}

function diffBadge(app, menuIdx) {
  const { pass } = app.state.menus[menuIdx], s = app.state.settings;
  const d = E.difficulty(pass, s);
  const text = [`EASY ${pass}/${s.diffMed}`, `NORMAL ${pass}/${s.diffHard}`, `HARD ${pass}`][d];
  return h('span', { class: `badge tier-${d}`, title: 'Difficulty: passes so far / passes to unlock the next tier' }, text);
}

// Countdown for the TIMED card, shown top-right on every prescript screen.
function timedChip(app, life) {
  const el = h('span', { class: 'timer-chip', role: 'timer' });
  const paint = () => {
    const t = app.state.timed, now = Date.now();
    if (!t.active) {
      el.textContent = '--:--'; el.className = 'timer-chip off';
      el.setAttribute('aria-label', 'Timed prescript not running');
      return;
    }
    const rem = E.timedRemaining(app.state, now), dur = E.durationMs(app.state.settings.timer);
    el.textContent = E.clock(rem);
    el.setAttribute('aria-label', `Timed prescript: ${E.clock(rem)} left${t.paused ? ', paused' : ''}`);
    el.className = 'timer-chip'
      + (t.paused ? ' paused' : t.passed ? ' passed' : rem < dur / 4 ? ' urgent' : '')
      + (!t.paused && now - t.start < 1300 ? ' blink' : '');
  };
  paint();
  life.on(window, 'beeper:second', paint);
  life.on(window, 'beeper:timed', paint);
  return el;
}

function prescriptView(body, app, life, idx) {
  const st = app.state, m = st.menus[idx];
  const card = E.currentCard(st, idx, Date.now());
  app.save();

  const text = h('p', { class: `card-text${card.rare ? ' rare' : ''}` });
  life.add(scramble(text, E.cardLabel(card), { stepMs: st.settings.glyphMs, perStep: st.settings.revealChars, onTick: beep.tick }));

  const answer = async (pass) => {
    passBtn.disabled = failBtn.disabled = true;
    const r = E.record(st, idx, pass);
    app.save();
    await showResult(app, { pass, ...r, streak: m.streak, best: m.best });
    m.current = null; // next visit draws a fresh card
    app.save();
    app.rerender();
  };
  const passBtn = h('button', { class: 'answer pass', type: 'button', onclick: () => answer(true) }, 'PASS');
  const failBtn = h('button', { class: 'answer fail', type: 'button', onclick: () => answer(false) }, 'FAIL');

  body.replaceChildren(
    h('div', { class: 'topline' },
      h('div', { class: 'who' },
        h('h1', { class: `label${card.rare ? ' rare' : ''}`, tabindex: -1 }, E.MENUS[idx]),
        card.rare ? h('span', { class: 'badge rare' }, '[INDEX]') : diffBadge(app, idx),
        h('span', { class: `streak${m.streak > 0 && m.streak === m.best ? ' best' : ''}` }, `STREAK ${m.streak}`)),
      h('div', { class: 'top-right' }, timedChip(app, life), menuButton(app))),
    logo(),
    h('div', { class: 'card-area' }, text),
    h('div', { class: 'answers' }, passBtn, failBtn),
    sessionClock(app, life));
}

function intervalText(t) {
  return [t.d && `${t.d}d`, t.h && `${t.h}h`, t.m && `${t.m}m`].filter(Boolean).join(' ') || '0m';
}

function timedView(body, app, life) {
  const st = app.state, t = st.timed, m = st.menus[E.TIMED];
  life.on(window, 'beeper:timed', () => app.rerender());

  const head = h('div', { class: 'tm-head' },
    h('div', { class: 'who' },
      h('h1', { class: 'label', tabindex: -1 }, 'TIMED'),
      t.active && t.rare ? h('span', { class: 'badge rare' }, '[INDEX]') : diffBadge(app, E.TIMED),
      t.active ? h('span', { class: 'tm-theme' }, E.MENUS[t.pool]) : null),
    h('div', { class: 'top-right' }, timedChip(app, life), menuButton(app)));
  const counts = h('p', { class: 'tm-counts' }, `PASS ${m.pass}  ·  FAIL ${m.fail}`);

  if (!t.active) {
    const begin = () => { E.timedAssign(st, Date.now()); app.save(); beep.newCard(); app.emit('timed'); };
    body.replaceChildren(head, counts, logo(),
      h('div', { class: 'tm-start' },
        h('button', { class: 'answer start', type: 'button', onclick: begin }, 'BEGIN TIMER'),
        h('p', { class: 'hint' }, `INTERVAL ${intervalText(st.settings.timer)}`)),
      sessionClock(app, life));
    return;
  }

  if (!t.paused) app.stayAwake(life);
  const text = h('p', { class: `card-text${t.rare ? ' rare' : ''}${t.paused ? ' paused' : ''}${t.passed ? ' passed' : ''}` });
  life.add(scramble(text, E.cardLabel(t), { stepMs: st.settings.glyphMs, perStep: st.settings.revealChars, onTick: beep.tick }));

  const fill = h('div', { class: 'tm-fill' });
  const bar = h('div', { class: 'tm-bar', role: 'progressbar', 'aria-label': 'Time remaining', 'aria-valuemin': 0, 'aria-valuemax': 100 }, fill);
  const next = h('p', { class: 'hint', 'aria-live': 'off' });
  const paint = () => {
    const rem = E.timedRemaining(st, Date.now()), dur = E.durationMs(st.settings.timer);
    const frac = dur ? rem / dur : 0;
    fill.style.transform = `scaleX(${frac})`;
    bar.setAttribute('aria-valuenow', Math.round(frac * 100));
    bar.className = 'tm-bar' + (t.paused ? ' paused' : t.passed ? ' passed' : frac < 0.25 ? ' urgent' : '');
    if (t.passed) next.textContent = `PASSED · NEXT IN ${E.clock(rem)}`;
  };
  paint();
  life.on(window, 'beeper:second', paint);

  const act = async (pass) => {
    for (const b of controls.querySelectorAll('button')) b.disabled = true;
    const now = Date.now();
    const r = pass ? E.timedPass(st) : E.timedFail(st, now);
    app.save();
    await showResult(app, { pass, ...r, streak: m.streak, best: m.best, timed: true });
    app.rerender();
  };
  const pause = () => { E.timedTogglePause(st, Date.now()); app.save(); app.emit('timed'); };
  const controls = t.passed
    ? h('div', { class: 'answers' }, next)
    : h('div', { class: 'answers three' },
      h('button', { class: 'answer pass', type: 'button', disabled: t.paused, onclick: () => act(true) }, 'PASS'),
      h('button', { class: 'answer pause', type: 'button', onclick: pause }, t.paused ? 'RESUME' : 'PAUSE'),
      h('button', { class: 'answer fail', type: 'button', disabled: t.paused, onclick: () => act(false) }, 'FAIL'));

  body.replaceChildren(head, counts, logo(), h('div', { class: 'card-area' }, text), bar, controls, sessionClock(app, life));
}
