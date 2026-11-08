// =================================================
// cyber.js — CYBER TOOLS menu (cyber.h), adapted for iPhone.
//
// iOS gives web apps (and even App Store apps) no way to list WiFi
// networks, no Web Bluetooth in Safari, and no Bluetooth Classic access at
// all. So the three scanners become phone-native, read-only substitutes in
// the same look, and the QR display is ported (and extended) as-is:
//   WIFI SCANNER   -> NETWORK STATUS  (online state, connection info, latency)
//   BLE SCANNER    -> BLE / TRACKERS  (Web Bluetooth if the browser has it,
//                                       otherwise iOS's built-in tracker alerts)
//   BLUETOOTH SCAN -> DEVICE SCAN     (what this phone exposes to a web page)
//   QR DISPLAY     -> QR DISPLAY      (versions 1-4, text editable)
// =================================================

const C_BG = tft.color565(15, 15, 15), C_TEXT = tft.color565(153, 202, 255), C_DIM = tft.color565(60, 70, 90),
  C_GOOD = tft.color565(0, 180, 80), C_WARN = tft.color565(210, 160, 20), C_BAD = tft.color565(200, 30, 30),
  C_HEAD = tft.color565(20, 30, 55);
let cyberScroll = 0;

function cyberHeader(title, hint = '') {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 16, C_HEAD);
  tft.setTextSize(1); tft.setTextColor(C_TEXT, C_HEAD);
  tft.setCursor(4, 4); tft.print(title);
  if (hint) { tft.setTextColor(C_DIM, C_HEAD); tft.setCursor(320 - hint.length * 6 - 4, 4); tft.print(hint); }
}
function cyberRow(y, col, left, right = '') {
  tft.setTextSize(1); tft.setTextColor(col, C_BG);
  tft.setCursor(4, y); tft.print(left.slice(0, right ? 26 : 52));
  if (right) { tft.setTextColor(C_DIM, C_BG); tft.setCursor(320 - right.length * 6 - 4, y); tft.print(right); }
}
function cyberText(y, col, text, x = 4) {
  tft.setTextSize(1); tft.setTextColor(col, C_BG);
  for (const l of wrapText(text, 52)) { tft.setCursor(x, y); tft.print(l); y += 10; }
  return y;
}
function cyberCleanup() { netPingRunning = false; }

// ── 1. NETWORK STATUS (replaces WIFI SCAN) ────────
let netPings = [], netPingRunning = false;

function latencyCol(ms) { return ms < 0 ? C_BAD : ms <= 150 ? C_GOOD : ms <= 400 ? C_WARN : C_BAD; }

async function netPingOnce() {
  const t0 = performance.now();
  try {
    await fetch('manifest.webmanifest?ping=' + Date.now(), { cache: 'no-store' });
    return Math.round(performance.now() - t0);
  } catch (e) { return -1; }
}

async function netRunPings() {
  if (netPingRunning) return;
  netPingRunning = true; netPings = [];
  wifiDraw();
  for (let i = 0; i < 6 && netPingRunning && screen === CYBER_WIFI; i++) {
    netPings.push(await netPingOnce());
    if (screen === CYBER_WIFI) wifiDraw();
  }
  netPingRunning = false;
  if (screen === CYBER_WIFI) wifiDraw();
}

function wifiDraw() {
  cyberHeader('NET STATUS', netPingRunning ? 'PINGING...' : 'TAP=RESCAN');
  const online = navigator.onLine;
  const c = navigator.connection || navigator.mozConnection || navigator.webkitConnection;
  let y = 22;
  cyberRow(y, online ? C_GOOD : C_BAD, 'LINK', online ? 'ONLINE' : 'OFFLINE'); y += 12;
  cyberRow(y, C_TEXT, 'CONNECTION TYPE', c && c.type ? c.type.toUpperCase() : 'HIDDEN BY IOS'); y += 12;
  cyberRow(y, C_TEXT, 'EFFECTIVE SPEED', c && c.effectiveType ? c.effectiveType.toUpperCase() : 'HIDDEN BY IOS'); y += 12;
  if (c && c.downlink) { cyberRow(y, C_TEXT, 'DOWNLINK', c.downlink + ' MBPS'); y += 12; }
  cyberRow(y, C_TEXT, 'HOST', location.host.toUpperCase() || 'LOCAL FILE'); y += 16;

  tft.setTextColor(C_DIM, C_BG); tft.setCursor(4, y); tft.print('ROUND TRIP TO HOST');
  tft.setCursor(260, y); tft.print('MS'); y += 12;
  for (let i = 0; i < netPings.length; i++) {
    const ms = netPings[i];
    cyberRow(y, latencyCol(ms), 'PING ' + (i + 1), ms < 0 ? 'FAIL' : String(ms)); y += 10;
  }
  if (!netPings.length) { cyberRow(y, C_DIM, netPingRunning ? 'MEASURING...' : 'TAP THE LIST TO MEASURE'); }
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 186); tft.print('IOS DOES NOT ALLOW APPS TO LIST WIFI');
  tft.setCursor(4, 196); tft.print('NETWORKS, SO THIS SHOWS YOUR OWN LINK.');
  drawButton(95, 207, 130, 24, 'BACK');
}
function cyberWifiScan() { cyberScroll = 0; netPings = []; wifiDraw(); netRunPings(); }
function cyberWifiTap(x, y) { if (y < 200) netRunPings(); }

// ── 2. BLE / TRACKERS (replaces BLE SCAN) ─────────
// Web Bluetooth (where a browser offers it, e.g. Bluefy on iOS) only lets a
// page pick one device at a time from a system chooser: there is no passive
// scan. Safari has none at all. iOS itself scans for AirTags and other
// Find My-compatible trackers and alerts you, so this screen points there.
const bleList = [];
let bleMsg = '';

function cyberBLEDraw() {
  const has = !!(navigator.bluetooth && navigator.bluetooth.requestDevice);
  cyberHeader('BLE / TRACKERS', has ? 'WEB BLUETOOTH' : 'NOT IN SAFARI');
  let y = 22;
  if (!has) {
    tft.fillRect(0, 18, 320, 12, C_WARN);
    tft.setTextSize(1); tft.setTextColor(TFT_BLACK, C_WARN);
    const t = 'IPHONE BLOCKS BLE SCANNING IN WEB APPS';
    tft.setCursor(((320 - t.length * 6) / 2) | 0, 20); tft.print(t);
    y = 36;
    y = cyberText(y, C_TEXT, 'YOUR IPHONE ALREADY LISTENS FOR AIRTAGS AND FIND MY TRACKERS TRAVELLING WITH YOU, AND ALERTS YOU.') + 6;
    tft.setTextColor(C_DIM, C_BG); tft.setCursor(4, y); tft.print('TO CHECK:'); y += 12;
    y = cyberText(y, C_GOOD, '> SETTINGS > NOTIFICATIONS > TRACKING NOTIFICATIONS: ON') + 2;
    y = cyberText(y, C_GOOD, '> FIND MY APP > ITEMS > IDENTIFY FOUND ITEM') + 2;
    y = cyberText(y, C_GOOD, '> ANDROID TRACKERS (TILE ETC): ALERTS ARRIVE THE SAME WAY ON RECENT IOS') + 6;
    cyberText(y, C_DIM, 'THE BEEPER HARDWARE CAN STILL DO A FULL PASSIVE BLE SWEEP.');
  } else {
    y = cyberText(y, C_DIM, 'THIS BROWSER CAN PICK ONE NEARBY DEVICE AT A TIME (NO PASSIVE SCAN).') + 4;
    cyberRow(y, C_DIM, 'DEVICES: ' + bleList.length); y += 12;
    for (let i = 0; i < min(5, bleList.length); i++) {
      const d = bleList[bleList.length - 1 - i];
      cyberRow(y, C_TEXT, d.name || 'UNNAMED', 'ID'); tft.setTextColor(C_DIM, C_BG);
      tft.setCursor(4, y + 9); tft.print(d.id.slice(0, 50)); y += 20;
    }
    if (bleMsg) cyberRow(170, C_WARN, bleMsg);
    drawButton(4, 182, 60, 20, 'PICK');
    drawButton(70, 182, 60, 20, 'CLEAR');
  }
  drawButton(95, 207, 130, 24, 'BACK');
}
async function cyberBLETap(x, y) {
  if (!(navigator.bluetooth && navigator.bluetooth.requestDevice)) return;
  if (hit(x, y, 70, 182, 60, 20)) { bleList.length = 0; bleMsg = ''; cyberBLEDraw(); return; }
  if (hit(x, y, 4, 182, 60, 20)) {
    try {
      const d = await navigator.bluetooth.requestDevice({ acceptAllDevices: true });
      if (!bleList.some(e => e.id === d.id)) bleList.push({ name: (d.name || '').toUpperCase(), id: d.id });
      bleMsg = '';
    } catch (e) { bleMsg = 'NO DEVICE PICKED'; }
    if (screen === CYBER_BLE) cyberBLEDraw();
  }
}

// ── 3. DEVICE SCAN (replaces BT CLASSIC) ──────────
let devStorage = '';
function cyberBTRun() {
  devStorage = '';
  btDraw();
  if (navigator.storage && navigator.storage.estimate) {
    navigator.storage.estimate().then(e => {
      devStorage = `${((e.usage || 0) / 1024) | 0} KB / ${((e.quota || 0) / 1048576) | 0} MB`;
      if (screen === CYBER_BT) btDraw();
    }).catch(() => {});
  }
}
function btDraw() {
  cyberHeader('DEVICE SCAN', 'THIS PHONE');
  const ua = navigator.userAgent;
  const ios = (ua.match(/OS (\d+)_(\d+)/) || [])[0];
  const standalone = window.matchMedia('(display-mode: standalone)').matches || navigator.standalone;
  const rows = [
    ['PLATFORM', (/iPhone/.test(ua) ? 'IPHONE' : /iPad/.test(ua) ? 'IPAD' : (navigator.platform || '?').toUpperCase())],
    ['SYSTEM', ios ? ('IOS ' + ios.slice(3).replace('_', '.')) : (navigator.platform || '?').toUpperCase()],
    ['SCREEN', `${screen_w()}x${screen_h()} @${window.devicePixelRatio || 1}X`],
    ['TOUCH POINTS', String(navigator.maxTouchPoints || 0)],
    ['CPU CORES', navigator.hardwareConcurrency ? String(navigator.hardwareConcurrency) : 'HIDDEN'],
    ['LANGUAGE', (navigator.language || '?').toUpperCase()],
    ['TIME ZONE', (Intl.DateTimeFormat().resolvedOptions().timeZone || '?').toUpperCase().slice(0, 22)],
    ['APP MODE', standalone ? 'HOME SCREEN APP' : 'BROWSER TAB'],
    ['STORAGE', devStorage || 'CHECKING...'],
    ['SAVED STATE', ((localStorage.getItem(STORE_KEY) || '').length / 1024).toFixed(1) + ' KB'],
  ];
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 20); tft.print('PROPERTY                 VALUE');
  rows.forEach(([k, v], i) => cyberRow(32 + i * 14, C_TEXT, k, v));
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 182); tft.print('IOS HAS NO BLUETOOTH CLASSIC ACCESS FOR');
  tft.setCursor(4, 192); tft.print('APPS. THIS LISTS WHAT THE PHONE REVEALS.');
  drawButton(95, 207, 130, 24, 'BACK');
}
function screen_w() { return Math.round(window.screen.width); }
function screen_h() { return Math.round(window.screen.height); }
function cyberBTTap() {}

// =================================================
// 4. QR DISPLAY
// The firmware's encoder is version 1 only (17 chars), so its own URL is
// too long and the device shows "TEXT TOO LONG". This port keeps the same
// approach (byte mode, ECC-L, mask 0, GF(256) Reed-Solomon) and extends it
// to versions 1-4 (up to 78 chars), which fits that URL.
// =================================================
const QR_DEFAULT = 'https://user.xmission.com/~emailbox/catstuff.htm';
const QR_CAP = [0, 17, 32, 53, 78];          // byte-mode capacity, ECC-L
const QR_DATA_CW = [0, 19, 34, 55, 80];      // data codewords
const QR_ECC_CW = [0, 7, 10, 15, 20];        // ECC codewords (single block)
const QR_ALIGN = [0, 0, 18, 22, 26];         // alignment centre (v2-4)
let qrText = QR_DEFAULT, qrReady = false, qrVer = 1, qrSize = 21, qrMat = [], qrFn = [];

const GF_EXP = new Uint8Array(512), GF_LOG = new Uint8Array(256);
(function () { let x = 1; for (let i = 0; i < 255; i++) { GF_EXP[i] = x; GF_LOG[x] = i; x <<= 1; if (x & 0x100) x ^= 0x11D; } for (let i = 255; i < 512; i++) GF_EXP[i] = GF_EXP[i - 255]; })();
function gfMul(a, b) { return (!a || !b) ? 0 : GF_EXP[GF_LOG[a] + GF_LOG[b]]; }
function rsGenerator(n) {           // coefficients after the leading 1
  let g = [1];
  for (let i = 0; i < n; i++) {
    const next = new Array(g.length + 1).fill(0);
    for (let j = 0; j < g.length; j++) { next[j] ^= g[j]; next[j + 1] ^= gfMul(g[j], GF_EXP[i]); }
    g = next;
  }
  return g.slice(1);
}
function rsRemainder(data, n) {
  const gen = rsGenerator(n), ecc = new Array(n).fill(0);
  for (const b of data) {
    const c = b ^ ecc.shift(); ecc.push(0);
    if (c) for (let j = 0; j < n; j++) ecc[j] ^= gfMul(gen[j], c);
  }
  return ecc;
}

function qrEncode(text) {
  const bytes = Array.from(new TextEncoder().encode(text));
  const ver = QR_CAP.findIndex((c, v) => v > 0 && bytes.length <= c);
  if (ver < 1) return false;
  const size = 17 + 4 * ver;
  qrVer = ver; qrSize = size;
  qrMat = Array.from({ length: size }, () => new Array(size).fill(0));
  qrFn = Array.from({ length: size }, () => new Array(size).fill(false));
  const setFn = (r, c, v) => { if (r >= 0 && r < size && c >= 0 && c < size) { qrMat[r][c] = v ? 1 : 0; qrFn[r][c] = true; } };

  // Finders + separators
  for (const [tr, tc] of [[0, 0], [0, size - 7], [size - 7, 0]])
    for (let r = -1; r <= 7; r++) for (let c = -1; c <= 7; c++) {
      const inside = r >= 0 && r <= 6 && c >= 0 && c <= 6;
      setFn(tr + r, tc + c, inside && ((r === 0 || r === 6 || c === 0 || c === 6) || (r >= 2 && r <= 4 && c >= 2 && c <= 4)));
    }
  // Timing
  for (let i = 8; i < size - 8; i++) { setFn(6, i, i % 2 === 0); setFn(i, 6, i % 2 === 0); }
  // Alignment
  if (QR_ALIGN[ver]) {
    const a = QR_ALIGN[ver];
    for (let r = -2; r <= 2; r++) for (let c = -2; c <= 2; c++) setFn(a + r, a + c, Math.max(abs(r), abs(c)) !== 1);
  }
  // Format info (ECC-L, mask 0) = 0x77C4, both copies, plus the dark module
  const fmt = 0x77C4, bit = i => (fmt >> i) & 1;
  for (let i = 0; i <= 5; i++) setFn(i, 8, bit(i));
  setFn(7, 8, bit(6)); setFn(8, 8, bit(7)); setFn(8, 7, bit(8));
  for (let i = 9; i < 15; i++) setFn(8, 14 - i, bit(i));
  for (let i = 0; i < 8; i++) setFn(8, size - 1 - i, bit(i));
  for (let i = 8; i < 15; i++) setFn(size - 15 + i, 8, bit(i));
  setFn(size - 8, 8, 1);

  // Data codewords: mode 0100, 8-bit length, bytes, terminator, pad
  const dataCw = QR_DATA_CW[ver], bits = [];
  const put = (v, n) => { for (let i = n - 1; i >= 0; i--) bits.push((v >> i) & 1); };
  put(4, 4); put(bytes.length, 8); bytes.forEach(b => put(b, 8));
  put(0, Math.min(4, dataCw * 8 - bits.length));
  while (bits.length % 8) bits.push(0);
  const data = [];
  for (let i = 0; i < bits.length; i += 8) data.push(parseInt(bits.slice(i, i + 8).join(''), 2));
  for (let p = 0; data.length < dataCw; p++) data.push(p % 2 ? 0x11 : 0xEC);
  const stream = data.concat(rsRemainder(data, QR_ECC_CW[ver]));

  // Zig-zag placement from bottom-right, then mask 0 on all data modules
  let k = 0;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    const upward = ((right + 1) & 2) === 0;
    for (let v = 0; v < size; v++) for (let j = 0; j < 2; j++) {
      const c = right - j, r = upward ? size - 1 - v : v;
      if (qrFn[r][c]) continue;
      let b = k < stream.length * 8 ? (stream[k >> 3] >> (7 - (k & 7))) & 1 : 0;
      k++;
      if ((r + c) % 2 === 0) b ^= 1;
      qrMat[r][c] = b;
    }
  }
  return true;
}

function cyberQRInit(text) { qrText = text; qrReady = qrEncode(qrText); }

function cyberQRDraw() {
  cyberHeader('QR DISPLAY', 'V' + qrVer + ' ' + qrSize + 'X' + qrSize);
  if (!qrReady) {
    tft.setTextSize(1); tft.setTextColor(C_BAD, C_BG);
    tft.setCursor(4, 30); tft.print('TEXT TOO LONG (MAX 78 CHARS)');
    tft.setTextColor(C_DIM, C_BG); cyberText(44, C_DIM, qrText);
  } else {
    const mod = Math.min(9, Math.floor(176 / qrSize)), margin = 4;
    const ox = ((320 - qrSize * mod) / 2) | 0, oy = 20;
    tft.fillRect(ox - margin, oy - margin, qrSize * mod + margin * 2, qrSize * mod + margin * 2, TFT_WHITE);
    for (let r = 0; r < qrSize; r++) for (let c = 0; c < qrSize; c++)
      if (qrMat[r][c]) tft.fillRect(ox + c * mod, oy + r * mod, mod, mod, TFT_BLACK);
    const disp = qrText.length > 52 ? qrText.slice(0, 49) + '...' : qrText;
    tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(((320 - disp.length * 6) / 2) | 0, oy + qrSize * mod + margin + 2); tft.print(disp);
  }
  drawButton(95, 207, 130, 24, 'BACK');
  drawButton(236, 207, 80, 24, 'EDIT');
}

function cyberQRTap(x, y) {
  if (!hit(x, y, 236, 207, 80, 24)) return;
  // iOS shows its own keyboard here; the device has no text entry for this.
  const t = window.prompt('Text or link for the QR code (max 78 characters):', qrText);
  if (t === null) return;
  const v = t.trim() || QR_DEFAULT;
  cyberQRInit(v);
  prefs.qrtx = v; prefsCommit();
  if (screen === CYBER_QR) cyberQRDraw();
}
