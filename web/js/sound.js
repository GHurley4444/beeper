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

let lastTick = 0;
export const beep = {
  // One quiet blip per revealed character, capped so fast speeds don't buzz.
  tick() {
    const now = performance.now();
    if (now - lastTick < 45) return;
    lastTick = now;
    play([[2400 + Math.random() * 300, 0, 0.012]], 0.018);
  },
  pass() { play([[880, 0, 0.09], [1320, 0.11, 0.16]]); },
  fail() { play([[220, 0, 0.32]], 0.05, 'sawtooth'); },
  best() { play([[1320, 0, 0.07], [1760, 0.09, 0.07], [2640, 0.18, 0.14]]); },
  reward() { play([[1046, 0, 0.06], [1568, 0.08, 0.12]], 0.04); },
  alarm() { play([0, 0.25, 0.5, 1, 1.25, 1.5].map((at) => [2000, at, 0.14]), 0.06); },
  newCard() { play([[1600, 0, 0.05], [1600, 0.1, 0.05]], 0.035); },
};
