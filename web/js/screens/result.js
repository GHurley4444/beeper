// PASS/FAIL result: colour wipe in, scramble the verdict, show streak and any
// reward/punishment, then wipe back out. Resolves when the user dismisses it.
import { h, scramble, reducedMotion } from '../ui.js';
import { beep } from '../sound.js';

export function showResult(app, { pass, newBest, reward, streak, best, timed = false }) {
  return new Promise((resolve) => {
    // Firmware wipes 240px at `transitionPx` per 30ms frame; keep the same timing.
    const wipeMs = reducedMotion.matches ? 0 : Math.round((240 / app.state.settings.transitionPx) * 30);
    const word = h('p', { class: 'result-word' });
    const extra = h('div', { class: 'result-extra', 'aria-live': 'polite' });
    const hint = h('p', { class: 'hint result-hint', hidden: true }, 'TAP TO CONTINUE');
    const panel = h('div', {
      class: `result ${pass ? 'is-pass' : 'is-fail'}`, role: 'dialog', 'aria-modal': 'true',
      'aria-label': pass ? 'Passed' : 'Failed', tabindex: -1, style: `--wipe:${wipeMs}ms`,
    }, word, extra, hint);
    const overlay = app.openOverlay(panel);
    let stage = 'fill', stop = () => {};

    const hold = () => {
      stage = 'hold';
      if (pass) {
        extra.append(h('p', { class: 'result-streak' }, `STREAK ${streak}  ·  BEST ${best}`));
        if (newBest) { extra.append(h('p', { class: 'result-best' }, 'NEW BEST!')); beep.best(); }
      }
      hint.hidden = false;
    };
    const drain = () => {
      stage = 'drain';
      panel.classList.remove('in');
      setTimeout(close, wipeMs);
    };
    const close = () => { stop(); overlay.close(); resolve(); };

    const advance = () => {
      if (stage === 'hold') {
        if (timed && pass) close(); // timed pass returns straight to the countdown
        else if (reward) {
          stage = 'reward';
          hint.hidden = true;
          const line = h('p', { class: 'result-reward' });
          extra.replaceChildren(line);
          beep.reward();
          stop = scramble(line, reward, { stepMs: 45, onDone: () => { stage = 'rewardHold'; hint.hidden = false; } });
        } else drain();
      } else if (stage === 'rewardHold') drain();
    };
    panel.addEventListener('click', advance);
    panel.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ' || e.key === 'Escape') { e.preventDefault(); advance(); }
    });

    requestAnimationFrame(() => requestAnimationFrame(() => panel.classList.add('in')));
    (pass ? beep.pass : beep.fail)();
    panel.focus({ preventScroll: true });
    setTimeout(() => {
      stage = 'reveal';
      stop = scramble(word, pass ? '_CleAr_' : '_FaIL_', { stepMs: 120, onDone: hold });
    }, wipeMs);
  });
}
