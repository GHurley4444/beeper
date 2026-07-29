#pragma once

// =================================================
// cyber.h — Network analysis tools
// Tools: WiFi Scanner, BLE Scanner,
//         Bluetooth Classic Scanner, QR Display
// All tools are passive / read-only.
// =================================================

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BluetoothSerial.h>

extern TFT_eSPI tft;
extern void drawButton(int x, int y, int w, int h, const char* label, bool highlight);
extern bool hit(int tx, int ty, int bx, int by, int bw, int bh);

// ── Colours ──────────────────────────────────────
#define C_BG   tft.color565(15,  15,  15)
#define C_TEXT tft.color565(153, 202, 255)
#define C_DIM  tft.color565(60,  70,  90)
#define C_GOOD tft.color565(0,   180, 80)
#define C_WARN tft.color565(210, 160, 20)
#define C_BAD  tft.color565(200, 30,  30)
#define C_HEAD tft.color565(20,  30,  55)

static int  cyberScroll = 0;

// =================================================
// SHARED HELPERS
// =================================================
static void cyberHeader(const char* title, const char* hint = "") {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 16, C_HEAD);
  tft.setTextSize(1);
  tft.setTextColor(C_TEXT, C_HEAD);
  tft.setCursor(4, 4); tft.print(title);
  if (hint && hint[0]) {
    tft.setTextColor(C_DIM, C_HEAD);
    tft.setCursor(320 - strlen(hint) * 6 - 4, 4);
    tft.print(hint);
  }
}

static void cyberRow(int y, uint16_t col, const char* left, const char* right = "") {
  tft.setTextSize(1); tft.setTextColor(col, C_BG);
  int maxL = (right && right[0]) ? 26 : 52;
  char buf[54]; strncpy(buf, left, maxL); buf[maxL] = '\0';
  tft.setCursor(4, y); tft.print(buf);
  if (right && right[0]) {
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(320 - strlen(right) * 6 - 4, y);
    tft.print(right);
  }
}

static uint16_t rssiCol(int32_t r) {
  return r >= -60 ? C_GOOD : r >= -75 ? C_WARN : C_BAD;
}

static const char* encStr(wifi_auth_mode_t e) {
  switch (e) {
    case WIFI_AUTH_OPEN:         return "OPEN";
    case WIFI_AUTH_WEP:          return "WEP";
    case WIFI_AUTH_WPA_PSK:      return "WPA";
    case WIFI_AUTH_WPA2_PSK:     return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:     return "WPA3";
    default:                     return "?";
  }
}

// =================================================
// 1.  WIFI SCANNER
// Active scan for 802.11 beacons.
// Displays SSID, channel, encryption, signal strength.
// No association or packet injection.
// =================================================
#define MAX_APS 32
struct APEntry {
  char             ssid[33];
  int32_t          rssi;
  uint8_t          channel;
  wifi_auth_mode_t enc;
};
static APEntry apList[MAX_APS];
static int     apCount = 0;

static void wifiScanRun() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false, true);   // sync, show hidden
  apCount = constrain(n < 0 ? 0 : n, 0, MAX_APS);
  for (int i = 0; i < apCount; i++) {
    strncpy(apList[i].ssid, WiFi.SSID(i).c_str(), 32);
    apList[i].ssid[32] = '\0';
    apList[i].rssi    = WiFi.RSSI(i);
    apList[i].channel = WiFi.channel(i);
    apList[i].enc     = (wifi_auth_mode_t)WiFi.encryptionType(i);
  }
  WiFi.scanDelete();
  // Sort by RSSI descending
  for (int i = 0; i < apCount - 1; i++)
    for (int j = i + 1; j < apCount; j++)
      if (apList[j].rssi > apList[i].rssi) {
        APEntry t = apList[i]; apList[i] = apList[j]; apList[j] = t;
      }
  WiFi.mode(WIFI_OFF);
}

static void wifiScanDraw() {
  cyberHeader("WIFI SCAN", "TAP=RESCAN");

  if (apCount == 0) {
    tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    tft.setCursor((320 - 12 * 6) / 2, 110); tft.print("NO APS FOUND");
    drawButton(95, 207, 130, 24, "BACK", false);
    return;
  }

  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 18);   tft.print("SSID");
  tft.setCursor(208, 18); tft.print("CH  ENC   RSSI");

  const int visRows = 8, rowH = 20;
  cyberScroll = constrain(cyberScroll, 0, max(0, apCount - visRows));

  for (int i = 0; i < visRows && (i + cyberScroll) < apCount; i++) {
    APEntry& a = apList[i + cyberScroll];
    int y = 30 + i * rowH;
    char ssid[22]; strncpy(ssid, a.ssid[0] ? a.ssid : "[HIDDEN]", 21); ssid[21] = '\0';
    tft.setTextColor(rssiCol(a.rssi), C_BG);
    tft.setCursor(4, y); tft.print(ssid);
    char right[22];
    snprintf(right, sizeof(right), "%2u %-5s%4d", a.channel, encStr(a.enc), a.rssi);
    tft.setTextColor(C_DIM, C_BG); tft.setCursor(204, y); tft.print(right);
  }

  if (apCount > visRows) {
    char sc[12]; snprintf(sc, sizeof(sc), "%d / %d", cyberScroll + 1, apCount);
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor((320 - strlen(sc) * 6) / 2, 194); tft.print(sc);
  }

  drawButton(95, 207, 130, 24, "BACK", false);
}

void cyberWifiScan() {
  cyberHeader("WIFI SCAN", "SCANNING...");
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor((320 - 15 * 6) / 2, 110); tft.print("PLEASE WAIT...");
  cyberScroll = 0;
  wifiScanRun();
  wifiScanDraw();
}

// Tap handler — call when screen == CYBER_WIFI
// Caller handles BACK button.
void cyberWifiTap(int x, int y) {
  if (y < 200) {
    // Tap on list area: scroll or rescan
    if (y < 120) { cyberScroll = max(0, cyberScroll - 1); wifiScanDraw(); }
    else          { cyberScroll++;                          wifiScanDraw(); }
  }
}

void cyberWifiRescan() { cyberWifiScan(); }

// =================================================
// 2.  BLE SCANNER
// Listens for BLE advertisement packets.
// Identifies known tracker signatures (AirTag, Tile)
// to help users detect unwanted tracking devices.
// No connection or data exchange.
// =================================================
#define MAX_BLE 28
struct BLEEntry {
  char name[28];
  char mac[18];
  int  rssi;
  bool isTracker;
  char trackerType[12];
};
static BLEEntry bleList[MAX_BLE];
static int      bleCount    = 0;
static bool     bleScanning = false;
static BLEScan* bleScanPtr  = nullptr;

class CyberBLECB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (bleCount >= MAX_BLE) return;

    BLEEntry e;
    strncpy(e.name, dev.getName().c_str(), 27); e.name[27] = '\0';
    strncpy(e.mac,  dev.getAddress().toString().c_str(), 17); e.mac[17] = '\0';
    e.rssi = dev.getRSSI();
    e.isTracker = false;
    e.trackerType[0] = '\0';

    if (dev.haveManufacturerData()) {
      String md = dev.getManufacturerData();
      uint8_t b0 = md.length() > 0 ? (uint8_t)md[0] : 0;
      uint8_t b1 = md.length() > 1 ? (uint8_t)md[1] : 0;
      uint8_t b2 = md.length() > 2 ? (uint8_t)md[2] : 0;
      // Apple (0x004C): type 0x12 = AirTag, 0x0F = FindMy
      if (b0 == 0x4C && b1 == 0x00) {
        if (b2 == 0x12) { e.isTracker = true; strncpy(e.trackerType, "AIRTAG", 11); }
        else if (b2 == 0x0F) { e.isTracker = true; strncpy(e.trackerType, "FINDMY", 11); }
      }
      // Tile (0x00D4)
      if (b0 == 0xD4 && b1 == 0x00) { e.isTracker = true; strncpy(e.trackerType, "TILE", 11); }
    }

    // Dedup by MAC; update RSSI if already present
    for (int i = 0; i < bleCount; i++) {
      if (strcmp(bleList[i].mac, e.mac) == 0) { bleList[i] = e; return; }
    }
    bleList[bleCount++] = e;
  }
};
static CyberBLECB bleCB;

void cyberBLEStart() {
  bleCount = 0; cyberScroll = 0;
  BLEDevice::init("");
  bleScanPtr = BLEDevice::getScan();
  bleScanPtr->setAdvertisedDeviceCallbacks(&bleCB, true);
  bleScanPtr->setActiveScan(true);
  bleScanPtr->setInterval(100);
  bleScanPtr->setWindow(99);
  bleScanPtr->start(0, nullptr, false);   // continuous async
  bleScanning = true;
}

void cyberBLEStop() {
  if (bleScanPtr) bleScanPtr->stop();
  bleScanning = false;
}

void cyberBLEDraw() {
  cyberHeader("BLE SCAN", bleScanning ? "LIVE" : "STOPPED");

  // Tracker alert bar if any found
  int trackers = 0;
  for (int i = 0; i < bleCount; i++) if (bleList[i].isTracker) trackers++;
  if (trackers > 0) {
    tft.fillRect(0, 16, 320, 12, C_BAD);
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, C_BAD);
    char alert[32]; snprintf(alert, sizeof(alert), "! %d TRACKER(S) DETECTED !", trackers);
    tft.setCursor((320 - strlen(alert) * 6) / 2, 18); tft.print(alert);
  }

  int listY = trackers > 0 ? 30 : 20;
  char cnt[24]; snprintf(cnt, sizeof(cnt), "DEVICES: %d", bleCount);
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, listY); tft.print(cnt);

  if (bleCount == 0) {
    tft.setCursor(4, listY + 14); tft.print("SCANNING FOR BLE DEVICES...");
  } else {
    const int visRows = 7, rowH = 20;
    cyberScroll = constrain(cyberScroll, 0, max(0, bleCount - visRows));
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(4, listY + 12); tft.print("NAME / MAC             RSSI");
    for (int i = 0; i < visRows && (i + cyberScroll) < bleCount; i++) {
      BLEEntry& e = bleList[i + cyberScroll];
      int y = listY + 22 + i * rowH;
      uint16_t col = e.isTracker ? C_BAD : rssiCol(e.rssi);
      char nd[28];
      if (e.isTracker)
        snprintf(nd, sizeof(nd), "[%s] %s", e.trackerType, e.name[0] ? e.name : "");
      else
        strncpy(nd, e.name[0] ? e.name : e.mac, 27);
      nd[27] = '\0';
      char rs[6]; snprintf(rs, sizeof(rs), "%d", e.rssi);
      cyberRow(y, col, nd, rs);
      if (e.name[0]) {   // show MAC as sub-line
        tft.setTextColor(C_DIM, C_BG); tft.setCursor(4, y + 9); tft.print(e.mac);
      }
    }
  }

  drawButton(4,   182, 60, 20, bleScanning ? "STOP" : "START", false);
  drawButton(70,  182, 60, 20, "CLEAR",  false);
  drawButton(200, 182, 60, 20, "SCROLL", false);
  drawButton(95,  207, 130, 24, "BACK",  false);
}

void cyberBLETap(int x, int y) {
  if (hit(x, y, 4,   182, 60, 20)) {
    bleScanning ? cyberBLEStop() : cyberBLEStart(); cyberBLEDraw();
  }
  if (hit(x, y, 70,  182, 60, 20)) { bleCount = 0; cyberScroll = 0; cyberBLEDraw(); }
  if (hit(x, y, 200, 182, 60, 20)) {
    cyberScroll = (cyberScroll + 1) % max(1, bleCount); cyberBLEDraw();
  }
}

// =================================================
// 3.  BLUETOOTH CLASSIC SCANNER
// Performs a 5-second BR/EDR inquiry.
// Reports device name and class of device.
// No pairing or data exchange.
// =================================================
#define MAX_BT 12
struct BTEntry { char name[28]; char addr[18]; uint32_t cod; };
static BTEntry btList[MAX_BT];
static int     btCount = 0;

static const char* btCOD(uint32_t cod) {
  switch ((cod >> 8) & 0x1F) {
    case 1: return "PC";     case 2: return "PHONE";
    case 3: return "NET";    case 4: return "A/V";
    case 5: return "INPUT";  case 6: return "IMAGING";
    case 7: return "WEAR";   case 8: return "TOY";
    default: return "OTHER";
  }
}

static BluetoothSerial btSerial;

void cyberBTRun() {
  cyberHeader("BT CLASSIC", "SCANNING...");
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 30); tft.print("BLUETOOTH INQUIRY");
  tft.setCursor(4, 42); tft.print("UP TO 5 SECONDS...");
  btCount = 0; cyberScroll = 0;

  btSerial.begin("ESP32", true);   // master mode
  BTScanResults* res = btSerial.discover(5000);
  if (res) {
    for (int i = 0; i < res->getCount() && i < MAX_BT; i++) {
      BTAdvertisedDevice* d = res->getDevice(i);
      strncpy(btList[btCount].name, d->getName().c_str(), 27);
      btList[btCount].name[27] = '\0';
      strncpy(btList[btCount].addr, d->getAddress().toString().c_str(), 17);
      btList[btCount].addr[17] = '\0';
      btList[btCount].cod = d->getCOD();
      btCount++;
    }
  }
  btSerial.end();

  // Draw results
  cyberHeader("BT CLASSIC", "DONE");
  char cnt[20]; snprintf(cnt, sizeof(cnt), "FOUND: %d", btCount);
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 20); tft.print(cnt);

  if (btCount == 0) {
    tft.setCursor(4, 36); tft.print("NO DEVICES FOUND");
    tft.setCursor(4, 48); tft.print("ENSURE DEVICES ARE DISCOVERABLE");
  } else {
    tft.setCursor(4, 32); tft.print("NAME                  TYPE");
    int show = min(btCount, 7);
    for (int i = 0; i < show; i++) {
      int y = 42 + i * 20;
      cyberRow(y, C_TEXT,
        btList[i].name[0] ? btList[i].name : btList[i].addr,
        btCOD(btList[i].cod));
      tft.setTextColor(C_DIM, C_BG);
      tft.setCursor(4, y + 9); tft.print(btList[i].addr);
    }
  }

  drawButton(60,  182, 100, 24, "RESCAN", false);
  drawButton(95,  207, 130, 24, "BACK",   false);
}

void cyberBTTap(int x, int y) {
  if (hit(x, y, 60, 182, 100, 24)) cyberBTRun();
}

// =================================================
// 4.  QR CODE DISPLAY
// Encodes up to 17 ASCII characters as a Version 1
// ECC-L QR code and renders it on the TFT.
// The stored string is set once via cyberQRInit().
// =================================================
static char   qrText[128] = "https://example.com";
static bool   qrReady     = false;

#define QR_SZ 21
static uint8_t qrMat[QR_SZ][QR_SZ];

// GF(256) tables
static const uint8_t GF_EXP[256] = {
  1,2,4,8,16,32,64,128,29,58,116,232,205,135,19,38,76,152,45,90,180,117,234,201,143,3,6,12,24,
  48,96,192,157,39,78,156,37,74,148,53,106,212,181,119,238,193,159,35,70,140,5,10,20,40,80,160,
  93,186,105,210,185,111,222,161,95,190,97,194,153,47,94,188,101,202,137,15,30,60,120,240,253,
  231,211,187,107,214,177,127,254,225,223,163,91,182,113,226,217,175,67,134,17,34,68,136,13,26,
  52,104,208,189,103,206,129,31,62,124,248,237,199,147,59,118,236,197,151,51,102,204,133,23,46,
  92,184,109,218,169,79,158,33,66,132,21,42,84,168,77,154,41,82,164,85,170,73,146,57,114,228,
  213,183,115,230,209,191,99,198,145,63,126,252,229,215,179,123,246,241,255,227,219,171,75,150,
  49,98,196,149,55,110,220,165,87,174,65,130,25,50,100,200,141,7,14,28,56,112,224,221,167,83,
  166,81,162,89,178,121,242,249,239,195,155,43,86,172,69,138,9,18,36,72,144,61,122,244,245,247,
  243,251,235,203,139,11,22,44,88,176,125,250,233,207,131,27,54,108,216,173,71,142,0
};
static const uint8_t GF_LOG[256] = {
  0,0,1,25,2,50,26,198,3,223,51,238,27,104,199,75,4,100,224,14,52,141,239,129,28,193,105,248,
  200,8,76,113,5,138,101,47,225,36,15,33,53,147,142,218,240,18,130,69,29,181,194,125,106,39,
  249,185,201,154,9,120,77,228,114,166,6,191,139,98,102,221,48,253,226,152,37,179,16,145,34,
  136,54,208,148,206,143,150,219,189,241,210,19,92,131,56,70,64,30,66,182,163,195,72,126,110,
  107,58,40,84,250,133,186,61,202,94,155,159,10,21,121,43,78,212,229,172,115,243,167,87,7,112,
  192,247,140,128,99,13,103,74,222,237,49,197,254,24,227,165,153,119,38,184,180,124,17,68,146,
  217,35,32,137,46,55,63,209,91,149,188,207,205,144,135,151,178,220,252,190,97,242,86,211,171,
  20,42,93,158,132,60,57,83,71,109,65,162,31,45,67,216,183,123,164,118,196,23,73,236,127,12,
  111,246,108,161,59,82,41,157,85,170,251,96,134,177,187,204,62,90,203,89,95,176,156,169,160,
  81,11,245,22,235,122,117,44,215,79,174,213,233,230,231,173,232,116,214,244,234,168,80,88,175
};

static uint8_t gfMul(uint8_t a, uint8_t b) {
  return (!a || !b) ? 0 : GF_EXP[(GF_LOG[a] + GF_LOG[b]) % 255];
}

// Generator poly for 7 ECC codewords
static const uint8_t GEN7[8] = {0x87,0xF0,0x2A,0xB8,0xCF,0xAB,0xD1,0x01};

static void qrRSDivide(uint8_t* data, int dLen, uint8_t* ecc) {
  memset(ecc, 0, 7);
  for (int i = 0; i < dLen; i++) {
    uint8_t c = data[i] ^ ecc[0];
    memmove(ecc, ecc + 1, 6); ecc[6] = 0;
    if (c) for (int j = 0; j < 7; j++) ecc[j] ^= gfMul(GEN7[j], c);
  }
}

static bool qrIsFn(int r, int c) {
  return (r <= 8 && (c <= 8 || c >= QR_SZ - 8)) ||
         (r >= QR_SZ - 8 && c <= 8) ||
         r == 6 || c == 6;
}

static void qrFinder(int tr, int tc) {
  for (int r = 0; r <= 6; r++)
    for (int c = 0; c <= 6; c++) {
      if (tr+r < 0 || tr+r >= QR_SZ || tc+c < 0 || tc+c >= QR_SZ) continue;
      bool d = (r==0||r==6||c==0||c==6) || (r>=2&&r<=4&&c>=2&&c<=4);
      qrMat[tr+r][tc+c] = d ? 1 : 0;
    }
}

static bool qrEncode(const char* text) {
  int len = strlen(text);
  if (len > 17) return false;   // V1 ECC-L byte-mode capacity = 17

  memset(qrMat, 0, sizeof(qrMat));

  qrFinder(0, 0); qrFinder(0, QR_SZ-7); qrFinder(QR_SZ-7, 0);

  // Timing patterns
  for (int i = 8; i < QR_SZ - 8; i++) {
    qrMat[6][i] = (i % 2 == 0);
    qrMat[i][6] = (i % 2 == 0);
  }
  qrMat[QR_SZ-8][8] = 1;   // dark module

  // Format string (ECC-L, mask 0) — pre-computed = 0x77C4
  uint16_t fmt = 0x77C4;
  for (int i = 0; i < 6; i++) { bool b=(fmt>>(14-i))&1; qrMat[8][i]=b; qrMat[QR_SZ-1-i][8]=b; }
  { bool b=(fmt>>8)&1; qrMat[8][7]=b; }
  { bool b=(fmt>>7)&1; qrMat[8][8]=b; qrMat[7][8]=b; }
  for (int i=8; i<15; i++) {
    bool b=(fmt>>(14-i))&1;
    qrMat[8][QR_SZ-15+i]=b;
    if (i<9) qrMat[QR_SZ-7+(i-8)][8]=b;
  }

  // Build data codewords
  uint8_t data[19]; memset(data, 0, 19);
  auto setBit = [&](int bit, bool v) {
    if (bit/8 < 19 && v) data[bit/8] |= (0x80 >> (bit%8));
  };
  // Mode = 0100 (byte)
  setBit(2,1);
  // Length (8 bits)
  for (int i=0; i<8; i++) setBit(4+i, (len>>(7-i))&1);
  // Data bytes
  for (int b=0; b<len; b++)
    for (int i=0; i<8; i++) setBit(12+b*8+i, ((uint8_t)text[b]>>(7-i))&1);
  // Padding
  const uint8_t pads[] = {0xEC,0x11};
  int fp = (12 + len*8 + 7) / 8;
  for (int i=fp; i<19; i++) data[i] = pads[(i-fp)%2];

  uint8_t ecc[7]; qrRSDivide(data, 19, ecc);
  uint8_t stream[26]; memcpy(stream, data, 19); memcpy(stream+19, ecc, 7);

  // Place bits
  int bitIdx = 0;
  for (int right = QR_SZ-1; right >= 1; right -= 2) {
    if (right == 6) right--;
    bool upward = (((right+1)/2) % 2 == 1);
    for (int vert = 0; vert < QR_SZ; vert++) {
      for (int jj = 0; jj < 2; jj++) {
        int r = upward ? (QR_SZ-1-vert) : vert;
        int c = right - jj;
        if (qrIsFn(r,c)) continue;
        bool bit = (bitIdx < 208) ? ((stream[bitIdx/8] >> (7-(bitIdx%8))) & 1) : false;
        if ((r+c)%2==0) bit=!bit;   // mask pattern 0
        qrMat[r][c] = bit ? 1 : 0;
        bitIdx++;
      }
    }
  }
  return true;
}

void cyberQRInit(const char* text) {
  strncpy(qrText, text, 127); qrText[127] = '\0';
  qrReady = qrEncode(qrText);
}

void cyberQRDraw() {
  cyberHeader("QR DISPLAY", "");
  if (!qrReady) {
    tft.setTextSize(1); tft.setTextColor(C_BAD, C_BG);
    tft.setCursor(4, 30); tft.print("TEXT TOO LONG (MAX 17 CHARS)");
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(4, 44); tft.print(qrText);
    drawButton(95, 207, 130, 24, "BACK", false);
    return;
  }

  // Each module = 9px → 21*9 = 189px. Centre horizontally, top at y=18.
  const int mod = 9, margin = 4;
  int ox = (320 - QR_SZ * mod) / 2;
  int oy = 18;

  // White quiet zone
  tft.fillRect(ox - margin, oy - margin,
               QR_SZ * mod + margin*2,
               QR_SZ * mod + margin*2, TFT_WHITE);

  for (int r = 0; r < QR_SZ; r++)
    for (int c = 0; c < QR_SZ; c++)
      if (qrMat[r][c])
        tft.fillRect(ox + c*mod, oy + r*mod, mod, mod, TFT_BLACK);

  // Text label below
  char disp[24]; strncpy(disp, qrText, 23); disp[23] = '\0';
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor((320 - strlen(disp)*6) / 2, oy + QR_SZ*mod + margin + 2);
  tft.print(disp);

  drawButton(95, 207, 130, 24, "BACK", false);
}

// =================================================
// CLEANUP — call when navigating away from any
// cyber screen to release radio resources.
// =================================================
void cyberCleanup() {
  if (bleScanning && bleScanPtr) { bleScanPtr->stop(); bleScanning = false; }
  WiFi.mode(WIFI_OFF);
}

bool cyberBLEIsScanning() { return bleScanning; }
