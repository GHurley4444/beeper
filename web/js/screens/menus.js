// Main menu, tools list, game picker, settings and history.
import { h, header, stepper, choices, confirmDialog, scramble } from '../ui.js';
import * as E from '../engine.js';
import { LOOP } from './play.js';
import D from '../../data.js';
import { beep, setSound } from '../sound.js';

const toCarousel = (app) => () => app.go(`/${LOOP[app.state.loop] ?? 'game'}`, { replace: true });
// Navigation rows are real links (long-press / middle-click work) routed in-app.
const link = (app, path, kids, cls = 'menu-item') => h('a', {
  class: cls, href: `#${path}`,
  onclick: (e) => { if (!e.metaKey && !e.ctrlKey && !e.shiftKey) { e.preventDefault(); app.go(path); } },
}, kids);
const list = (app, items) => h('ul', { class: 'menu-list', role: 'list' },
  items.map(([label, path]) => h('li', null, link(app, path, h('span', null, label)))));

export function mainMenu(root, app) {
  root.replaceChildren(h('section', { class: 'screen' },
    header('MAIN MENU', { back: toCarousel(app) }),
    h('div', { class: 'content' },
      list(app, [['TOOLS', '/tools'], ['SONGS', '/songs'], ['SETTINGS', '/settings']]),
      h('div', { class: 'picker-promo' },
        h('p', { class: 'picker-kicker' }, 'PICK A GAME AT RANDOM'),
        link(app, '/picker', 'GAME PICKER', 'btn btn-picker')))));
}

export function toolsMenu(root, app) {
  root.replaceChildren(h('section', { class: 'screen' },
    header('TOOLS', { back: () => app.back('/menu') }),
    h('div', { class: 'content' }, list(app, [
      ['ROLL D20', '/tool/dice'],
      ['COIN FLIP', '/tool/coin'],
      ['TAROT DRAW', '/tool/tarot'],
      ['ORACLE', '/tool/oracle'],
      ['COUNTDOWN TIMER', '/tool/countdown'],
      ['WORD GENERATOR', '/tool/words'],
      ['CIPHER WHEEL', '/tool/cipher'],
      ['SONGS', '/songs'],
    ]))));
}

export function picker(root, app, life) {
  const name = h('p', { class: 'picker-name', 'aria-live': 'polite' });
  const status = h('p', { class: 'hint' });
  const roll = () => {
    let frame = 0;
    name.classList.remove('settled');
    status.textContent = 'ROLLING…';
    rollBtn.disabled = true;
    const id = life.every(80, () => {
      name.textContent = E.pick(D.games);
      if (++frame > 18) {
        clearInterval(id);
        name.classList.add('settled');
        status.textContent = 'TAP ROLL FOR ANOTHER';
        rollBtn.disabled = false;
      }
    });
  };
  const rollBtn = h('button', { class: 'btn', type: 'button', onclick: roll }, 'ROLL');
  root.replaceChildren(h('section', { class: 'screen tone-picker' },
    header('GAME PICKER', { back: () => app.back('/menu'), tone: 'picker' }),
    h('div', { class: 'content center' },
      h('div', { class: 'picker-box' }, name),
      status,
      rollBtn,
      h('p', { class: 'hint' }, `${D.games.length} GAMES`))));
  roll();
}

export function settings(root, app) {
  root.replaceChildren(h('section', { class: 'screen' },
    header('SETTINGS', { back: () => app.back('/menu') }),
    h('div', { class: 'content' }, list(app, [
      ['TIMER DURATION', '/settings/timer'],
      ['IDLE TIMEOUT', '/settings/idle'],
      ['ANIM SPEED', '/settings/speed'],
      ['SOUND', '/settings/sound'],
      ['DIFFICULTY', '/settings/difficulty'],
      ['TIMED POOLS', '/settings/pools'],
      ['RESET DECKS', '/settings/reset'],
      ['HISTORY', '/history'],
    ]))));
}

const PAGES = { timer: timerPage, idle: idlePage, speed: speedPage, sound: soundPage, difficulty: difficultyPage, pools: poolsPage, reset: resetPage };

function soundPage(app, life) {
  const s = app.state.settings;
  // Preview plays a real decode at the current speed settings.
  const sample = h('p', { class: 'card-text sound-sample' }, '_RECEIVE THE PRESCRIPT._');
  let stop = () => {};
  life.add(() => stop());
  const preview = () => {
    stop();
    stop = scramble(sample, '_RECEIVE THE PRESCRIPT._', { stepMs: s.glyphMs, perStep: s.revealChars, voice: beep.decode });
  };
  return ['SOUND',
    h('p', { class: 'lede' }, 'A CHIRP AND DATA WARBLE WHILE TEXT DECODES, A LOCK-IN BEEP WHEN IT SETTLES, CHIMES FOR PASS AND FAIL, NEW TIMED CARDS AND THE COUNTDOWN ALARM. FOLLOWS THE IPHONE SILENT SWITCH.'),
    choices('SOUND', [['ON', true], ['OFF', false]], {
      get: () => s.sound,
      set: (v) => { s.sound = v; setSound(v); app.save(); if (v) preview(); },
    }),
    sample,
    h('button', { class: 'btn btn-primary', type: 'button', onclick: preview }, 'PREVIEW')];
}

export function settingsPage(root, app, life, name) {
  const page = PAGES[name];
  if (!page) return false;
  const [title, ...content] = page(app, life);
  root.replaceChildren(h('section', { class: 'screen' },
    header(title, { back: () => app.back('/settings') }),
    h('div', { class: 'content' }, content)));
}

function timerPage(app) {
  const s = app.state.settings;
  const edit = { ...s.timer };
  const total = h('p', { class: 'hint', 'aria-live': 'polite' });
  const paint = () => {
    total.textContent = edit.d || edit.h || edit.m
      ? `= ${edit.d}D ${edit.h}H ${edit.m}M`
      : 'MINIMUM 1 MINUTE: SAVING USES 1M';
    total.classList.toggle('warn', !(edit.d || edit.h || edit.m));
  };
  const field = (label, key, max, wrap) => stepper(label, { get: () => edit[key], set: (v) => { edit[key] = v; paint(); }, max, wrap });
  paint();
  return ['TIMER DURATION',
    h('p', { class: 'lede' }, 'HOW LONG EACH TIMED PRESCRIPT RUNS.'),
    h('div', { class: 'steppers' }, field('DAYS', 'd', 99, false), field('HOURS', 'h', 23, true), field('MINUTES', 'm', 59, true)),
    total,
    h('div', { class: 'row' },
      h('button', {
        class: 'btn btn-primary', type: 'button',
        onclick: () => {
          if (!edit.d && !edit.h && !edit.m) edit.m = 1;
          s.timer = edit; app.save(); app.emit('timed'); app.back('/settings');
        },
      }, 'SAVE'),
      h('button', { class: 'btn', type: 'button', onclick: () => app.back('/settings') }, 'CANCEL'))];
}

function idlePage(app) {
  const s = app.state.settings;
  return ['IDLE TIMEOUT',
    h('p', { class: 'lede' }, 'TIME ON A PRESCRIPT BEFORE THE IDLE ANIMATION.'),
    choices('IDLE TIMEOUT', [['30 SEC', 30000], ['1 MIN', 60000], ['2 MIN', 120000], ['5 MIN', 300000], ['NEVER', 0]],
      { get: () => s.idleMs, set: (v) => { s.idleMs = v; app.save(); } })];
}

function speedPage(app) {
  const s = app.state.settings;
  const row = (label, key, values) => choices(label, [['SLOW', values[0]], ['MED', values[1]], ['FAST', values[2]]],
    { get: () => s[key], set: (v) => { s[key] = v; app.save(); } });
  return ['ANIM SPEED',
    row('GLYPH SCRAMBLE', 'glyphMs', [270, 90, 30]),
    row('TEXT REVEAL', 'revealChars', [1, 2, 4]),
    row('TRANSITIONS', 'transitionPx', [5, 10, 25])];
}

function difficultyPage(app) {
  const s = app.state.settings;
  const status = h('ul', { class: 'status-list', role: 'list' });
  const paint = () => status.replaceChildren(...[0, 1, 2, 3, 4].map((i) => {
    const d = E.difficulty(app.state.menus[i].pass, s);
    return h('li', null,
      h('span', { class: 'status-name' }, E.MENUS[i]),
      h('span', { class: `tier-${d}` }, E.TIER_NAMES[d]),
      h('span', { class: 'menu-note' }, `${app.state.menus[i].pass} PASSED`));
  }));
  paint();
  // Hard must stay above Normal: raising Normal pushes Hard up with it.
  const hard = stepper('HARD AFTER', {
    get: () => s.diffHard, min: 2, max: 999,
    set: (v) => { s.diffHard = Math.max(v, s.diffMed + 1); app.save(); paint(); },
  });
  const med = stepper('NORMAL AFTER', {
    get: () => s.diffMed, min: 1, max: 998,
    set: (v) => { s.diffMed = v; if (s.diffHard <= v) s.diffHard = v + 1; app.save(); paint(); hard.sync(); },
  });
  return ['DIFFICULTY', h('p', { class: 'lede' }, 'PASSES NEEDED TO UNLOCK EACH TIER.'), med, hard,
    h('h2', { class: 'section-title' }, 'CURRENT STATUS'), status];
}

function poolsPage(app) {
  const s = app.state.settings;
  const states = [0, 1, 2, 3].map(() => h('span'));
  const buttons = [0, 1, 2, 3].map((i) => h('button', {
    class: 'toggle', type: 'button', role: 'switch',
    onclick: () => { s.pools ^= 1 << i; app.save(); paint(); },
  }, h('span', null, E.MENUS[i]), states[i]));
  const group = h('div', { class: 'toggles' }, buttons);
  const paint = () => buttons.forEach((b, i) => {
    const on = !!((s.pools >> i) & 1);
    b.setAttribute('aria-checked', String(on));
    states[i].className = on ? 'on' : 'off';
    states[i].textContent = on ? 'ACTIVE' : 'INACTIVE';
  });
  paint();
  return ['TIMED POOLS',
    h('p', { class: 'lede' }, 'WHICH MENUS FEED THE TIMED SCREEN. WITH NONE ACTIVE, TIMED USES GAME.'),
    group];
}

function resetPage(app) {
  const reset = async (label, idxs) => {
    const ok = await confirmDialog(`RESET ${label}? PASSES, FAILS, STREAKS AND HISTORY ARE CLEARED. THIS CAN’T BE UNDONE.`, 'RESET');
    if (!ok) return;
    for (const i of idxs) E.resetMenu(app.state, i);
    app.save();
    app.emit('timed');
    app.back('/settings');
  };
  return ['RESET DECKS',
    h('p', { class: 'lede' }, 'RESET WHICH DECK?'),
    h('div', { class: 'grid2' },
      E.MENUS.map((name, i) => h('button', { class: 'btn', type: 'button', onclick: () => reset(name, [i]) }, name)),
      h('button', { class: 'btn btn-danger', type: 'button', onclick: () => reset('ALL DECKS', [0, 1, 2, 3, 4]) }, 'ALL'))];
}

export function historyScreen(root, app) {
  let view = 0;
  const buttons = E.MENUS.map((name, i) => h('button', {
    role: 'tab', type: 'button', class: 'seg-btn', onclick: () => { view = i; paint(); },
  }, name));
  const tabs = h('div', { class: 'seg', role: 'tablist', 'aria-label': 'Menu' }, buttons);
  const panel = h('div', { role: 'tabpanel' });
  const when = new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' });
  const paint = () => {
    buttons.forEach((b, i) => b.setAttribute('aria-selected', String(i === view)));
    const items = [...app.state.menus[view].history].reverse();
    panel.replaceChildren(items.length
      ? h('ol', { class: 'history', role: 'list' }, items.map((it) => h('li', { class: it.rare ? 'rare' : '' },
        h('p', null, it.rare ? `[INDEX] ${it.text}` : it.text),
        it.at ? h('time', { datetime: new Date(it.at).toISOString() }, when.format(it.at)) : null)))
      : h('p', { class: 'empty' }, 'NO HISTORY YET'));
  };
  paint();
  root.replaceChildren(h('section', { class: 'screen' },
    header('HISTORY', { back: () => app.back('/settings') }),
    h('div', { class: 'content' }, tabs, h('p', { class: 'lede' }, 'LAST 10 CARDS, NEWEST FIRST.'), panel)));
}
