// Piezo-style beeps synthesized with Web Audio: no sound files.
// iOS only allows audio after a tap, so the context is created on the first one.
let ctx = null;
let enabled = true;

export function setSound(on) { enabled = on; }

export function unlockAudio() {
  if (ctx || !window.AudioContext) return;
  // "ambient" follows the iPhone silent switch and mixes with music (iOS 17+).
  try { if (navigator.audioSession) navigator.audioSession.type = 'ambient'; } catch { /* unsupported */ }
  ctx = new AudioContext();
}

// Square-wave notes: [frequency Hz, start offset s, length s]
function play(notes, volume = 0.05, type = 'square') {
  if (!enabled || !ctx) return;
  if (ctx.state === 'suspended') ctx.resume();
  const t0 = ctx.currentTime + 0.01;
  for (const [freq, at, len] of notes) {
    const osc = ctx.createOscillator(), gain = ctx.createGain();
    osc.type = type;
    osc.frequency.value = freq;
    gain.gain.setValueAtTime(volume, t0 + at);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + at + len); // no click at the end
    osc.connect(gain).connect(ctx.destination);
    osc.start(t0 + at);
    osc.stop(t0 + at + len + 0.02);
  }
}

// Decode voice: start chirp, a stepping "data" warble, then a lock-in beep.
// The warble stops after WARBLE_S so long, slow reveals drop to quiet ticks.
const WARBLE_S = 1.6;
const WARBLE_NOTES = [880, 1175, 1319, 1568, 1760, 2093, 2349, 2637]; // pentatonic-ish, reads as "machine"
const SILENT = { step() {}, done() {}, cancel() {} };
let lastTick = 0;

function tick() {
  const now = performance.now();
  if (now - lastTick < 45) return; // fast speeds would otherwise buzz
  lastTick = now;
  play([[2400 + Math.random() * 300, 0, 0.012]], 0.018);
}

function decode() {
  if (!enabled || !ctx) return SILENT;
  if (ctx.state === 'suspended') ctx.resume();
  const t0 = ctx.currentTime + 0.01;

  // Chirp: a fast upward sweep.
  const chirp = ctx.createOscillator(), chirpGain = ctx.createGain();
  chirp.type = 'square';
  chirp.frequency.setValueAtTime(500, t0);
  chirp.frequency.exponentialRampToValueAtTime(2600, t0 + 0.08);
  chirpGain.gain.setValueAtTime(0.035, t0);
  chirpGain.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.1);
  chirp.connect(chirpGain).connect(ctx.destination);
  chirp.start(t0);
  chirp.stop(t0 + 0.12);

  // Warble: a square wave hopping between notes every 35ms, softened by a low-pass
  // filter and a slight tremolo so it sits under the visuals instead of on top.
  const osc = ctx.createOscillator(), lp = ctx.createBiquadFilter(), gain = ctx.createGain();
  osc.type = 'square';
  lp.type = 'lowpass';
  lp.frequency.value = 3200;
  const start = t0 + 0.09, end = start + WARBLE_S;
  for (let t = start; t < end; t += 0.035) {
    osc.frequency.setValueAtTime(WARBLE_NOTES[Math.floor(Math.random() * WARBLE_NOTES.length)] * (0.99 + Math.random() * 0.02), t);
  }
  gain.gain.setValueAtTime(0, start);
  gain.gain.linearRampToValueAtTime(0.016, start + 0.03);
  let high = false;
  for (let t = start + 0.1; t < end - 0.2; t += 0.14) gain.gain.linearRampToValueAtTime((high = !high) ? 0.011 : 0.016, t);
  gain.gain.linearRampToValueAtTime(0.0001, end);
  osc.connect(lp).connect(gain).connect(ctx.destination);
  osc.start(start);
  osc.stop(end + 0.02);

  const began = performance.now();
  let live = true;
  const cut = () => {
    if (!live) return;
    live = false;
    const now = ctx.currentTime;
    gain.gain.cancelScheduledValues(now);
    gain.gain.setValueAtTime(gain.gain.value, now);
    gain.gain.linearRampToValueAtTime(0.0001, now + 0.03);
    try { osc.stop(now + 0.05); } catch { /* already stopped */ }
  };
  return {
    step() { if (performance.now() - began > WARBLE_S * 1000) tick(); },
    done() { cut(); play([[2093, 0.02, 0.05], [3136, 0.08, 0.09]], 0.035); },
    cancel: cut,
  };
}

export const beep = {
  decode,
  tick,
  pass() { play([[880, 0, 0.09], [1320, 0.11, 0.16]]); },
  fail() { play([[220, 0, 0.32]], 0.05, 'sawtooth'); },
  best() { play([[1320, 0, 0.07], [1760, 0.09, 0.07], [2640, 0.18, 0.14]]); },
  alarm() { play([0, 0.25, 0.5, 1, 1.25, 1.5].map((at) => [2000, at, 0.14]), 0.06); },
  newCard() { play([[1600, 0, 0.05], [1600, 0.1, 0.05]], 0.035); },
};
