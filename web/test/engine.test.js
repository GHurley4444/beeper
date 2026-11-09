// node --test test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as E from '../js/engine.js';
import { defaults } from '../js/store.js';
import D from '../data.js';

test('ordinal suffixes', () => {
  const cases = { 1: '1st', 2: '2nd', 3: '3rd', 4: '4th', 11: '11th', 12: '12th', 13: '13th', 21: '21st', 22: '22nd', 100: '100th', 101: '101st', 111: '111th' };
  for (const [n, want] of Object.entries(cases)) assert.equal(E.ordinal(Number(n)), want);
});

test('difficulty follows the pass thresholds', () => {
  const s = { diffMed: 10, diffHard: 25 };
  assert.equal(E.difficulty(0, s), 0);
  assert.equal(E.difficulty(9, s), 0);
  assert.equal(E.difficulty(10, s), 1);
  assert.equal(E.difficulty(24, s), 1);
  assert.equal(E.difficulty(25, s), 2);
});

test('every generator template fills every slot at every tier', () => {
  for (let pool = 0; pool < 4; pool++) {
    for (let diff = 0; diff < 3; diff++) {
      for (let i = 0; i < 300; i++) {
        const s = E.generate(pool, diff);
        assert.ok(s && !s.includes('undefined'), `pool ${pool} diff ${diff}: ${s}`);
        if (pool === 1 && diff === 0) assert.match(s, / the (\d+)(st|nd|rd|th) /);
      }
    }
  }
});

test('easy never draws a rare card; rare cards come from the rare pool', () => {
  for (let i = 0; i < 2000; i++) assert.equal(E.drawCard(0, 0).rare, false);
  let rare = 0;
  for (let i = 0; i < 4000; i++) {
    const c = E.drawCard(2, 1);
    if (c.rare) { rare++; assert.ok(D.rare[2].includes(c.text)); }
  }
  assert.ok(rare > 100 && rare < 320, `about 1 in 20 expected, got ${rare}/4000`);
});

test('streaks, best streak and the every-5th reward', () => {
  const st = defaults();
  for (let i = 1; i <= 4; i++) assert.equal(E.record(st, 1, true).reward, null);
  const fifth = E.record(st, 1, true);
  assert.equal(fifth.reward, D.rewards[1][0][0]);
  assert.equal(st.menus[1].streak, 5);
  assert.equal(st.menus[1].best, 5);
  E.record(st, 1, false);
  assert.equal(st.menus[1].streak, 0);
  assert.equal(st.menus[1].best, 5);
  assert.equal(E.record(st, 1, true).newBest, false);
  // TIMED counts but never shows rewards.
  for (let i = 0; i < 5; i++) assert.equal(E.record(st, E.TIMED, true).reward, null);
});

test('currentCard keeps the same card until cleared, and logs history', () => {
  const st = defaults();
  const a = E.currentCard(st, 0, 1);
  assert.equal(E.currentCard(st, 0, 2), a);
  assert.equal(st.menus[0].history.length, 1);
  for (let i = 0; i < 15; i++) { st.menus[0].current = null; E.currentCard(st, 0, i); }
  assert.equal(st.menus[0].history.length, 10);
});

test('timed: expiry without a pass is a fail and starts a new card', () => {
  const st = defaults();
  st.settings.timer = { d: 0, h: 0, m: 1 };
  E.timedAssign(st, 0);
  assert.equal(E.timedTick(st, 59_000), false);
  assert.equal(E.timedRemaining(st, 59_000), 1000);
  assert.equal(E.timedTick(st, 60_000), true);
  assert.equal(st.menus[E.TIMED].fail, 1);
  assert.equal(st.timed.start, 60_000);
});

test('timed: a pass waits out the interval and is not also failed', () => {
  const st = defaults();
  st.settings.timer = { d: 0, h: 0, m: 1 };
  E.timedAssign(st, 0);
  E.timedPass(st);
  assert.equal(st.menus[E.TIMED].pass, 1);
  assert.equal(E.timedTick(st, 60_000), true);
  assert.equal(st.menus[E.TIMED].fail, 0);
  assert.equal(st.timed.passed, false);
});

test('timed: pause freezes the clock, resume continues it', () => {
  const st = defaults();
  st.settings.timer = { d: 0, h: 0, m: 10 };
  E.timedAssign(st, 0);
  E.timedTogglePause(st, 120_000);              // 8 min left
  assert.equal(E.timedRemaining(st, 999_999), 480_000);
  assert.equal(E.timedTick(st, 10_000_000), false);
  E.timedTogglePause(st, 1_000_000);
  assert.equal(E.timedRemaining(st, 1_000_000), 480_000);
  assert.equal(E.timedRemaining(st, 1_060_000), 420_000);
});

test('timed: fail restarts the clock on the same card', () => {
  const st = defaults();
  E.timedAssign(st, 0);
  const text = st.timed.text;
  E.timedFail(st, 5000);
  assert.equal(st.timed.text, text);
  assert.equal(st.timed.start, 5000);
  assert.equal(st.menus[E.TIMED].fail, 1);
});

test('timed draws only from enabled pools', () => {
  const st = defaults();
  st.settings.pools = 0b0100; // CON only
  for (let i = 0; i < 50; i++) { E.timedAssign(st, 0); assert.equal(st.timed.pool, 2); }
  st.settings.pools = 0; // none: falls back to GAME
  E.timedAssign(st, 0);
  assert.equal(st.timed.pool, 0);
});

test('clock formatting', () => {
  assert.equal(E.clock(0), '0:00');
  assert.equal(E.clock(59_001), '1:00');
  assert.equal(E.clock(65_000), '1:05');
  assert.equal(E.clock(3_725_000), '1:02:05');
});

test('reset clears a menu and stops TIMED', () => {
  const st = defaults();
  E.record(st, E.TIMED, true);
  E.timedAssign(st, 0);
  E.resetMenu(st, E.TIMED);
  assert.equal(st.menus[E.TIMED].pass, 0);
  assert.equal(st.timed.active, false);
});
