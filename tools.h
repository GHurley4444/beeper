#pragma once

// =================================================
// tools.h — Fun tools: coin flip, tarot, oracle,
//            countdown timer, name/word generator,
//            cipher wheel
// =================================================

extern TFT_eSPI tft;
extern void drawButton(int x, int y, int w, int h, const char* label, bool highlight);
extern bool hit(int tx, int ty, int bx, int by, int bw, int bh);
extern Preferences prefs;
extern int wrapText(const char* src, char out[][32], int maxCols);
extern uint8_t cfg_glyphSpinMs;

// Colours (reuse main sketch palette via extern or redefine locally)
#define T_BG   tft.color565(15,  15,  15)
#define T_TEXT tft.color565(153, 202, 255)
#define T_DIM  tft.color565(60,  70,  90)
#define T_GOLD tft.color565(210, 160, 20)
#define T_GOOD tft.color565(0,   180, 80)
#define T_BAD  tft.color565(200, 30,  30)
#define T_HEAD tft.color565(20,  30,  55)

// =================================================
// SHARED UTIL — scramble-reveal text renderer
// Draws `text` with characters 0..revealIdx resolved,
// rest randomised. Returns true when fully revealed.
// Call repeatedly each frame; caller manages timing.
// =================================================
static unsigned long toolLastReveal = 0;

static bool toolReveal(const char* text, int& revealIdx,
                       int x, int y, int cw, int size,
                       uint16_t col, uint16_t bg,
                       unsigned long revealMs = 80) {
  tft.setTextSize(size);
  tft.setTextColor(col, bg);
  int len = strlen(text);
  for (int i = 0; i < len; i++) {
    char c = (i < revealIdx) ? text[i] : (char)random(33, 126);
    tft.setCursor(x + i * cw, y);
    tft.print(c);
  }
  if (millis() - toolLastReveal > revealMs) {
    if (revealIdx < len) revealIdx++;
    toolLastReveal = millis();
  }
  return revealIdx >= len;
}

// Word-wrap a string into lines[], return count.
static int toolWrap(const char* text, char lines[][32], int maxCols) {
  int lc = 0;
  char word[32] = "", line[32] = "";
  int tlen = strlen(text);
  for (int i = 0; i <= tlen; i++) {
    if (i == tlen || text[i] == ' ') {
      char test[32];
      if (line[0]) snprintf(test, 32, "%s %s", line, word);
      else         strncpy(test, word, 31);
      test[31] = '\0';
      if ((int)strlen(test) > maxCols) {
        strncpy(lines[lc++], line, 31); lines[lc-1][31]='\0';
        strncpy(line, word, 31);
      } else {
        strncpy(line, test, 31);
      }
      word[0] = '\0';
    } else {
      int wl = strlen(word);
      if (wl < 31) { word[wl] = text[i]; word[wl+1] = '\0'; }
    }
  }
  if (line[0]) { strncpy(lines[lc++], line, 31); lines[lc-1][31]='\0'; }
  return lc;
}

// Wide variant — 54-char buffers for full-width size-1 text (320px / 6px = 53 chars)
static int toolWrapWide(const char* text, char lines[][54], int maxCols) {
  int lc = 0;
  char word[54] = "", line[54] = "";
  int tlen = strlen(text);
  for (int i = 0; i <= tlen; i++) {
    if (i == tlen || text[i] == ' ') {
      char test[54];
      if (line[0]) snprintf(test, 54, "%s %s", line, word);
      else         strncpy(test, word, 53);
      test[53] = '\0';
      if ((int)strlen(test) > maxCols) {
        strncpy(lines[lc++], line, 53); lines[lc-1][53]='\0';
        strncpy(line, word, 53);
      } else {
        strncpy(line, test, 53);
      }
      word[0] = '\0';
    } else {
      int wl = strlen(word);
      if (wl < 53) { word[wl] = text[i]; word[wl+1] = '\0'; }
    }
  }
  if (line[0]) { strncpy(lines[lc++], line, 53); lines[lc-1][53]='\0'; }
  return lc;
}

// =================================================
// 1.  COIN FLIP
// =================================================
enum CoinState { COIN_IDLE, COIN_SPIN, COIN_RESULT };
static CoinState   coinState     = COIN_IDLE;
static bool        coinResult    = false;   // true=HEADS
static int         coinFrame     = 0;
static int         coinReveal    = 0;
static unsigned long coinLastF   = 0;

static const char* COIN_FACES[] = { "H", "T", "H", "T", "H", "T" };

void toolCoinDraw() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320 - 9*6)/2, 4); tft.print("COIN FLIP");

  if (coinState == COIN_IDLE) {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor((320 - 14*6)/2, 80); tft.print("TAP TO FLIP");

    // Draw a large coin outline
    tft.drawCircle(160, 130, 60, T_DIM);
    tft.drawCircle(160, 130, 56, tft.color565(40,50,70));
    tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor(160 - 12, 118); tft.print("?");
  }
  else if (coinState == COIN_SPIN) {
    // Animated coin — oscillate width to simulate tumble
    int phase = coinFrame % 8;
    int w = (phase < 4) ? (phase * 15 + 5) : ((8 - phase) * 15 + 5);
    tft.fillRect(0, 90, 320, 90, T_BG);
    tft.drawEllipse(160, 135, w, 55, T_DIM);
    tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
    const char* face = COIN_FACES[phase % 2];
    tft.setCursor(160 - 12, 122); tft.print(face);
  }
  else if (coinState == COIN_RESULT) {
    uint16_t col = coinResult ? T_GOOD : T_GOLD;
    tft.drawCircle(160, 130, 60, col);
    tft.drawCircle(160, 130, 56, tft.color565(40,50,70));
    tft.setTextSize(4); tft.setTextColor(col, T_BG);
    const char* label = coinResult ? "H" : "T";
    tft.setCursor(160 - 12, 118); tft.print(label);
    tft.setTextSize(1);
    const char* word = coinResult ? "HEADS" : "TAILS";
    tft.setTextColor(col, T_BG);
    tft.setCursor((320 - strlen(word)*6)/2, 200);
    tft.print(word);
  }

  drawButton(95, 210, 130, 24, "BACK", false);
}

void toolCoinStart() {
  coinState  = COIN_SPIN;
  coinResult = random(2);
  coinFrame  = 0;
  coinReveal = 0;
  coinLastF  = millis();
}

void toolCoinTick() {
  if (coinState != COIN_SPIN) return;
  if (millis() - coinLastF < 60) return;
  coinLastF = millis();
  coinFrame++;
  if (coinFrame >= 24) {
    coinState = COIN_RESULT;
    toolCoinDraw();
    return;
  }
  // Clear the full coin area including top arc overhang
  tft.fillRect(60, 75, 200, 125, T_BG);
  int phase = coinFrame % 8;
  int w = (phase < 4) ? (phase * 15 + 5) : ((8 - phase) * 15 + 5);
  tft.drawEllipse(160, 135, w, 55, T_DIM);
  tft.setTextSize(4); tft.setTextColor(T_DIM, T_BG);
  const char* face = COIN_FACES[phase % 2];
  tft.setCursor(160 - 12, 114); tft.print(face);
}

void toolCoinTap(int x, int y) {
  if (coinState != COIN_SPIN) {
    toolCoinStart();
    toolCoinDraw();
  }
}

// =================================================
// =================================================
// 2.  TAROT — 22 MAJOR ARCANA
// Redesigned for zero flicker and reliable reveal.
// Reveal mirrors regular prescript menus exactly:
//   - index advances once per GLYPH_SPIN_MS
//   - draw happens every tick (overwrite, no erase)
//   - totals pre-computed on tap, no circular deps
// =================================================
struct TarotCard { const char* name; const char* desc; };

static const TarotCard TAROT[22] = {
  { "0 THE FOOL",         "BEGINNINGS, INNOCENCE, A LEAP INTO THE UNKNOWN" },
  { "I THE MAGICIAN",     "WILLPOWER, SKILL, THE ABILITY TO MANIFEST" },
  { "II HIGH PRIESTESS",  "INTUITION, MYSTERY, INNER KNOWLEDGE" },
  { "III THE EMPRESS",    "ABUNDANCE, NATURE, CREATIVE FERTILITY" },
  { "IV THE EMPEROR",     "AUTHORITY, STRUCTURE, STABILITY" },
  { "V THE HIEROPHANT",   "TRADITION, CONFORMITY, MORAL LAW" },
  { "VI THE LOVERS",      "UNION, DUALITY, A MEANINGFUL CHOICE" },
  { "VII THE CHARIOT",    "CONTROL, WILLPOWER, VICTORY THROUGH EFFORT" },
  { "VIII STRENGTH",      "COURAGE, PATIENCE, INNER STRENGTH" },
  { "IX THE HERMIT",      "SOLITUDE, INTROSPECTION, GUIDED WISDOM" },
  { "X WHEEL OF FORTUNE", "CYCLES, FATE, A TURNING POINT" },
  { "XI JUSTICE",         "TRUTH, FAIRNESS, CAUSE AND EFFECT" },
  { "XII THE HANGED MAN", "SUSPENSION, SURRENDER, NEW PERSPECTIVE" },
  { "XIII DEATH",         "ENDINGS, TRANSFORMATION, TRANSITION" },
  { "XIV TEMPERANCE",     "BALANCE, MODERATION, PATIENCE" },
  { "XV THE DEVIL",       "BONDAGE, MATERIALISM, SHADOW SELF" },
  { "XVI THE TOWER",      "SUDDEN CHANGE, UPHEAVAL, REVELATION" },
  { "XVII THE STAR",      "HOPE, RENEWAL, CALM AFTER STORM" },
  { "XVIII THE MOON",     "ILLUSION, FEAR, THE UNCONSCIOUS MIND" },
  { "XIX THE SUN",        "JOY, SUCCESS, CLARITY AND VITALITY" },
  { "XX JUDGEMENT",       "REFLECTION, RECKONING, AWAKENING" },
  { "XXI THE WORLD",      "COMPLETION, INTEGRATION, WHOLENESS" }
};

static int  tarotDeck[22];
static int  tarotPos     = 22;   // force shuffle on first draw
static int  tarotCurrent = -1;

// Reveal state — pre-computed on tap, no circular deps
static int  tarotRevealIdx   = 0;  // unified index across name+desc
static int  tarotRevealTotal = 0;  // total chars to reveal (name + sep + desc)
static bool tarotRevealing   = false;
static unsigned long tarotLastRev = 0;

// Wrapped lines — computed once on tap, reused every draw
static char tarotNameLines[4][32];
static int  tarotNLC = 0;
static char tarotDescLines[8][32];
static int  tarotDLC = 0;
static int  tarotTotalNameChars = 0;

// History
static int  tarotHistory[10];
static int  tarotHistCount  = 0;
static int  tarotHistScroll = 0;   // 0=card view, >0=history page

static void tarotShuffle() {
  for (int i = 0; i < 22; i++) tarotDeck[i] = i;
  for (int i = 21; i > 0; i--) {
    int j = random(i + 1);
    int t = tarotDeck[i]; tarotDeck[i] = tarotDeck[j]; tarotDeck[j] = t;
  }
  tarotPos = 0;
  prefs.begin("pscript", false);
  prefs.putBytes("trtd", tarotDeck, 22 * sizeof(int));
  prefs.putInt("trtp", tarotPos);
  prefs.end();
}

static void tarotSave() {
  prefs.begin("pscript", false);
  prefs.putBytes("trtd", tarotDeck, 22 * sizeof(int));
  prefs.putInt("trtp", tarotPos);
  prefs.end();
}

static void tarotLoad() {
  prefs.begin("pscript", true);
  if (prefs.isKey("trtp")) {
    prefs.getBytes("trtd", tarotDeck, 22 * sizeof(int));
    tarotPos = prefs.getInt("trtp", 22);
  } else {
    tarotPos = 22;
  }
  prefs.end();
}

// Pre-compute wrapped lines and totals for the current card.
// Called once on tap so the draw loop never needs to wrap text.
static void tarotPrecompute() {
  if (tarotCurrent < 0) return;
  const TarotCard& c = TAROT[tarotCurrent];
  tarotNLC = wrapText(c.name, tarotNameLines, 24);
  tarotDLC = wrapText(c.desc, tarotDescLines, 24);
  tarotTotalNameChars = 0;
  for (int l = 0; l < tarotNLC; l++) tarotTotalNameChars += strlen(tarotNameLines[l]);
  int totalDesc = 0;
  for (int l = 0; l < tarotDLC; l++) totalDesc += strlen(tarotDescLines[l]) + 1;
  tarotRevealTotal = tarotTotalNameChars + totalDesc;
}

// Draw the card text WITHOUT clearing first.
// Each character is drawn with bg colour so it overwrites the previous glyph
// in-place — no erase step, no flicker.
static void tarotDrawText() {
  if (tarotCurrent < 0) return;
  uint16_t bg = T_BG;
  int cw = 12, ch = 18;

  // Name lines
  int gi = 0;
  tft.setTextSize(2);
  for (int l = 0; l < tarotNLC; l++) {
    int len = strlen(tarotNameLines[l]);
    int x = (320 - len * cw) / 2;
    int y = 20 + l * ch;
    for (int i = 0; i < len; i++) {
      char c = (gi < tarotRevealIdx) ? tarotNameLines[l][i] : (char)random(33, 126);
      tft.setTextColor(T_GOLD, bg);
      tft.setCursor(x + i * cw, y); tft.print(c);
      gi++;
    }
  }

  // Separator — only draw once name is fully revealed (no partial flash)
  int sepY = 20 + tarotNLC * ch + 3;
  if (gi <= tarotRevealIdx) {
    tft.drawFastHLine(20, sepY, 280, T_GOLD);
  }

  // Description lines
  int di = 0;
  tft.setTextSize(2);
  for (int l = 0; l < tarotDLC; l++) {
    int len = strlen(tarotDescLines[l]);
    int y = sepY + 6 + l * ch;
    if (y + ch > 218) break;
    int x = (320 - len * cw) / 2;
    for (int i = 0; i < len; i++) {
      int descGi = tarotTotalNameChars + di + i;
      char c = (descGi < tarotRevealIdx) ? tarotDescLines[l][i] : (char)random(33, 126);
      tft.setTextColor(T_TEXT, bg);
      tft.setCursor(x + i * cw, y); tft.print(c);
    }
    di += len + 1;
  }
}

// Draw the history sub-screen
static void tarotDrawHistory() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_GOLD, T_HEAD);
  tft.setCursor((320 - 14*6)/2, 4); tft.print("CARD HISTORY");
  char cnt[16]; snprintf(cnt, sizeof(cnt), "%d CARDS", tarotHistCount);
  tft.setTextColor(T_DIM, T_HEAD);
  tft.setCursor(320 - strlen(cnt)*6 - 4, 4); tft.print(cnt);

  // Button bar at top of content area — BACK left, CLEAR centre, UP/DOWN right
  // y=18 so they sit just below the header
  drawButton(4,   18, 70, 20, "BACK",  false);
  drawButton(88,  18, 144, 20, "CLEAR HISTORY", false);
  drawButton(246, 18, 36, 20, "UP",   false);
  drawButton(284, 18, 36, 20, "DN",   false);

  tft.drawFastHLine(0, 40, 320, tft.color565(30,30,50));

  if (tarotHistCount == 0) {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor((320-16*6)/2, 120); tft.print("NO CARDS DRAWN YET");
    return;
  }

  // Content area: y=42 to y=235 = 193px
  // Each entry: name (8px) + desc lines (8px each) + gap
  // Use 3 entries per page to guarantee nothing is cut off
  const int maxRows  = 4;
  const int rowH     = 46;   // 46px per entry — name + 3 desc lines comfortably
  const int contentY = 42;

  int pageStart = tarotHistScroll - 1;

  for (int i = 0; i < maxRows; i++) {
    int entryIdx = tarotHistCount - 1 - (pageStart + i);
    if (entryIdx < 0) break;
    int ci = tarotHistory[entryIdx];
    int hy = contentY + i * rowH;
    if (hy + rowH > 234) break;

    if (i > 0) tft.drawFastHLine(4, hy - 2, 312, tft.color565(25,25,40));

    // Name — always fits (max ~22 chars for longest arcana name)
    tft.setTextSize(1); tft.setTextColor(T_GOLD, T_BG);
    tft.setCursor(6, hy + 1); tft.print(TAROT[ci].name);

    // Description — wide buffer so full text shows (size-1 text, 53 chars max per line)
    char dl[4][54]; int n = toolWrapWide(TAROT[ci].desc, dl, 50);
    tft.setTextColor(T_DIM, T_BG);
    for (int d = 0; d < n; d++) {
      int ly = hy + 12 + d * 9;
      if (ly + 8 > hy + rowH) break;
      tft.setCursor(6, ly); tft.print(dl[d]);
    }
  }

  // Page indicator
  int shown = min(maxRows, tarotHistCount - pageStart);
  char pg[20];
  snprintf(pg, sizeof(pg), "%d-%d of %d",
    pageStart + 1,
    min(pageStart + shown, tarotHistCount),
    tarotHistCount);
  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor((320 - strlen(pg)*6)/2, 228); tft.print(pg);
}

void toolTarotDraw() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_GOLD, T_HEAD);
  tft.setCursor((320 - 11*6)/2, 4); tft.print("TAROT DRAW");
  char rem[16]; snprintf(rem, sizeof(rem), "%d/22", 22 - tarotPos);
  tft.setTextColor(T_DIM, T_HEAD);
  tft.setCursor(320 - strlen(rem)*6 - 4, 4); tft.print(rem);

  if (tarotHistScroll > 0) {
    tarotDrawHistory();
    return;
  }

  if (tarotCurrent < 0) {
    // Placeholder card back
    tft.drawRect(30, 22, 260, 178, T_GOLD);
    tft.drawRect(32, 24, 256, 174, tft.color565(40,50,70));
    tft.fillRect(33, 25, 254, 172, tft.color565(8,10,24));
    tft.setTextSize(1); tft.setTextColor(tft.color565(40,50,70), T_BG);
    tft.setCursor((320-12*6)/2, 107); tft.print("MAJOR ARCANA");
    tft.setTextColor(T_DIM, T_BG);
    tft.setCursor((320-16*6)/2, 200); tft.print("TAP TO DRAW A CARD");
  } else {
    tarotDrawText();
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor((320-20*6)/2, 200); tft.print("TAP TO DRAW NEXT CARD");
  }

  // BACK left, HISTORY right — moved up from bottom edge
  drawButton(4,   208, 80, 22, "BACK",    false);
  drawButton(236, 208, 80, 22, "HISTORY", false);
}

void toolTarotInit() {
  tarotLoad();
  tarotCurrent     = -1;
  tarotRevealIdx   = 0;
  tarotRevealTotal = 0;
  tarotRevealing   = false;
  tarotHistScroll  = 0;
  tarotNLC = 0; tarotDLC = 0;
}

void toolTarotTap(int x, int y) {
  if (tarotHistScroll > 0) {
    // History screen taps — buttons are at y=18 (just below header)
    const int maxRows = 4;
    int maxScroll = max(1, tarotHistCount - maxRows + 1);

    if (hit(x, y, 4, 18, 70, 20)) {
      // BACK
      tarotHistScroll = 0; toolTarotDraw();
    } else if (hit(x, y, 88, 18, 144, 20)) {
      // CLEAR HISTORY
      tarotHistCount = 0; tarotHistScroll = 0; toolTarotDraw();
    } else if (hit(x, y, 246, 18, 36, 20)) {
      // UP — scroll toward newer entries
      if (tarotHistScroll > 1) { tarotHistScroll--; tarotDrawHistory(); }
    } else if (hit(x, y, 284, 18, 36, 20)) {
      // DN — scroll toward older entries
      if (tarotHistScroll < maxScroll) { tarotHistScroll++; tarotDrawHistory(); }
    }
    return;
  }

  // Card view taps
  if (hit(x, y, 4, 208, 80, 22)) return;        // BACK — handled by .ino
  if (hit(x, y, 236, 208, 80, 22)) {
    // HISTORY button
    tarotHistScroll = 1; tarotDrawHistory(); return;
  }

  // Tap on card area — draw next card
  if (tarotPos >= 22) tarotShuffle();
  tarotCurrent = tarotDeck[tarotPos++];

  if (tarotHistCount < 10) {
    tarotHistory[tarotHistCount++] = tarotCurrent;
  } else {
    memmove(tarotHistory, tarotHistory + 1, 9 * sizeof(int));
    tarotHistory[9] = tarotCurrent;
  }

  tarotPrecompute();
  tarotRevealIdx = 0;
  tarotRevealing = true;
  tarotLastRev   = millis();
  tarotSave();
  toolTarotDraw();
}

// Tick — mirrors regular prescript reveal exactly:
// advance reveal index once per cfg_glyphSpinMs,
// then draw every call (overwrite glyphs in-place, no erase).
void toolTarotTick() {
  if (tarotCurrent < 0 || tarotHistScroll > 0) return;  // no tick during history view

  // Advance reveal index on the same timer as regular prescript menus
  if (tarotRevealing && millis() - tarotLastRev >= cfg_glyphSpinMs) {
    tarotLastRev = millis();
    if (tarotRevealIdx < tarotRevealTotal) {
      tarotRevealIdx++;
    } else {
      tarotRevealing = false;
    }
  }

  // Draw every tick — overwrite chars in place, no fillRect erase = no flicker
  tarotDrawText();
}

// History scroll helper (called from .ino for button presses)
void tarotSwipeHistory(bool up) {
  if (tarotHistScroll > 0) {
    if (up)  tarotHistScroll = min(tarotHistScroll + 1, max(1, tarotHistCount - 4));
    else     tarotHistScroll = max(1, tarotHistScroll - 1);
  } else {
    if (up && tarotHistCount > 0) tarotHistScroll = 1;
  }
  toolTarotDraw();
}

bool tarotInHistory() { return tarotHistScroll > 0; }

// =================================================
// 3.  ORACLE
// 20 cryptic responses in the spirit of a mystical
// 8-ball. Tap to ask; result scramble-reveals.
// =================================================
static const char* ORACLE_RESPONSES[20] = {
  "IT IS CERTAIN",
  "THE SIGNS ALIGN IN YOUR FAVOUR",
  "WITHOUT DOUBT",
  "ASK AGAIN WHEN THE HOUR IS QUIETER",
  "THE ANSWER LIES ALREADY IN YOUR HANDS",
  "DO NOT COUNT ON IT",
  "CONCENTRATE AND ASK AGAIN",
  "THE CURRENT DOES NOT RUN THAT WAY",
  "ALL SIGNS POINT TO YES",
  "UNCLEAR — THE SIGNAL IS WEAK",
  "YES — BUT NOT IN THE WAY YOU EXPECT",
  "THE NEEDLE REFUSES TO SETTLE",
  "BETTER NOT REVEAL THIS NOW",
  "A DOOR OPENS THAT YOU DID NOT NOTICE",
  "VERY DOUBTFUL",
  "SOMEONE ALREADY KNOWS THE ANSWER",
  "TRUST THE SILENCE",
  "DO NOT MISTAKE THE REFLECTION FOR THE THING ITSELF",
  "THE PATTERN SUGGESTS YES",
  "LOOK CLOSER BEFORE YOU DECIDE"
};

static int  oracleCurrent  = -1;
static int  oracleReveal   = 0;
static bool oracleRevealing = false;

static int oracleLastResponse = -1;  // track when response changes

static void oracleDrawOrb() {
  int cx = 160, cy = 115, r = 75;
  uint16_t orbBG = tft.color565(5,8,20);

  // Only do a full clear when the response has changed
  if (oracleCurrent != oracleLastResponse) {
    tft.fillCircle(cx, cy, r-9, orbBG);
    oracleLastResponse = oracleCurrent;
  }

  if (oracleCurrent < 0) {
    tft.setTextSize(1); tft.setTextColor(tft.color565(40,60,100), orbBG);
    tft.setCursor((320-7*6)/2, cy-4); tft.print("TAP ORB");
    return;
  }

  char lines[4][32]; int lc = toolWrap(ORACLE_RESPONSES[oracleCurrent], lines, 20);
  int startY = cy - (lc*10)/2;

  for (int l = 0; l < lc; l++) {
    int len = strlen(lines[l]);
    int lx = (320 - len*6)/2;
    int di = 0;
    for (int p = 0; p < l; p++) di += strlen(lines[p]) + 1;
    for (int i = 0; i < len; i++) {
      int gi = di + i;
      // Use orb background as text background so overwriting is clean with no flicker
      char ch = (gi < oracleReveal) ? lines[l][i] : (char)random(33,126);
      tft.setTextColor(tft.color565(80,120,200), orbBG);
      tft.setCursor(lx + i*6, startY + l*12); tft.print(ch);
    }
  }

  if (oracleRevealing && millis() - toolLastReveal > 80) {
    toolLastReveal = millis();
    int total = 0;
    for (int l = 0; l < lc; l++) total += strlen(lines[l]) + 1;
    if (oracleReveal < total) oracleReveal++;
    else oracleRevealing = false;
  }
}

void toolOracleDraw() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320-6*6)/2, 4); tft.print("ORACLE");

  int cx = 160, cy = 115, r = 75;
  tft.fillCircle(cx, cy, r, tft.color565(5,8,20));
  tft.drawCircle(cx, cy, r,     tft.color565(40,60,100));
  tft.drawCircle(cx, cy, r-1,   tft.color565(20,35,70));
  tft.drawCircle(cx, cy, r-8,   tft.color565(10,20,50));

  oracleDrawOrb();

  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor((320-20*6)/2, 200); tft.print("TAP THE ORB TO CONSULT");
  drawButton(95, 210, 130, 24, "BACK", false);
}

void toolOracleInit() {
  oracleCurrent     = -1;
  oracleReveal      = 0;
  oracleRevealing   = false;
  oracleLastResponse = -1;
}

void toolOracleTap(int x, int y) {
  if (hit(x, y, 95, 210, 130, 24)) return;
  oracleCurrent     = random(20);
  oracleReveal      = 0;
  oracleRevealing   = true;
  oracleLastResponse = -2;   // force clear on next draw
  toolLastReveal    = millis();
  toolOracleDraw();   // draw static parts once (draws orb frame + calls oracleDrawOrb)
}

void toolOracleTick() {
  if (oracleRevealing) {
    oracleDrawOrb();   // only update orb interior — no fillScreen
  }
}

// =================================================
// 4.  COUNTDOWN TIMER
// Separate from the prescript timed menu.
// Set H/M/S, start, watch it count down.
// Flashes display at zero.
// =================================================
enum CDState { CD_SET, CD_RUNNING, CD_PAUSED, CD_DONE };
static CDState     cdState    = CD_SET;
static uint8_t     cdSetH     = 0, cdSetM = 5, cdSetS = 0;
static unsigned long cdTarget = 0;   // millis() when it hits zero
static unsigned long cdPauseRem = 0; // remaining ms when paused
static unsigned long cdLastFlash = 0;
static bool        cdFlashOn  = false;

static void cdDrawSet() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320-17*6)/2, 4); tft.print("COUNTDOWN TIMER");

  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(22, 34); tft.print("HH");
  tft.setCursor(130, 34); tft.print("MM");
  tft.setCursor(238, 34); tft.print("SS");

  // +/- buttons and values for H / M / S
  auto drawField = [&](int x, uint8_t val, uint8_t maxV) {
    drawButton(x, 44, 56, 20, "+", false);
    tft.setTextSize(3); tft.setTextColor(T_TEXT, T_BG);
    char vb[4]; snprintf(vb, sizeof(vb), "%02u", val);
    tft.setCursor(x + (56 - 36)/2, 70); tft.print(vb);
    drawButton(x, 94, 56, 20, "-", false);
  };

  drawField(10,  cdSetH, 23);
  drawField(118, cdSetM, 59);
  drawField(226, cdSetS, 59);

  tft.setTextSize(2); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(102, 70); tft.print(":");
  tft.setCursor(210, 70); tft.print(":");

  drawButton(60,  130, 90, 28, "START", false);
  drawButton(170, 130, 90, 28, "BACK",  false);
}

static void cdDrawRunning() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320-17*6)/2, 4); tft.print("COUNTDOWN TIMER");

  unsigned long remMs = 0;
  if (cdState == CD_RUNNING) {
    unsigned long now = millis();
    remMs = (now >= cdTarget) ? 0 : (cdTarget - now);
  } else if (cdState == CD_PAUSED) {
    remMs = cdPauseRem;
  }

  unsigned long totalSet = ((unsigned long)cdSetH*3600 +
                             (unsigned long)cdSetM*60  + cdSetS) * 1000UL;

  // Progress bar
  int barW = 280, barH = 8, barX = 20, barY = 24;
  float frac = (totalSet > 0) ? (float)remMs / totalSet : 0;
  if (frac > 1) frac = 1;
  uint16_t barCol = frac > 0.25f ? T_TEXT : T_BAD;
  tft.fillRect(barX, barY, barW, barH, tft.color565(25,30,40));
  tft.fillRect(barX, barY, (int)(barW * frac), barH, barCol);

  // Big time display
  unsigned int rh = remMs / 3600000UL;
  unsigned int rm = (remMs % 3600000UL) / 60000UL;
  unsigned int rs = (remMs % 60000UL) / 1000UL;
  char tbuf[12]; snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", rh, rm, rs);

  uint16_t timeCol = (cdState == CD_DONE) ? T_BAD : T_TEXT;
  tft.setTextSize(4); tft.setTextColor(timeCol, T_BG);
  tft.setCursor((320 - 8*24)/2, 60); tft.print(tbuf);

  if (cdState == CD_DONE) {
    tft.setTextSize(2); tft.setTextColor(T_BAD, T_BG);
    tft.setCursor((320 - 8*12)/2, 110); tft.print("TIME IS UP");
  } else {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    const char* hint = (cdState == CD_PAUSED) ? "PAUSED" : "RUNNING";
    tft.setCursor((320 - strlen(hint)*6)/2, 112); tft.print(hint);
  }

  drawButton(10,  170, 80, 28, cdState==CD_PAUSED ? "RESUME" : "PAUSE", false);
  drawButton(100, 170, 80, 28, "RESET", false);
  drawButton(210, 170, 80, 28, "BACK",  false);
}

void toolCDDraw() {
  if (cdState == CD_SET) cdDrawSet();
  else cdDrawRunning();
}

void toolCDTick() {
  if (cdState == CD_RUNNING) {
    if (millis() >= cdTarget) {
      cdState = CD_DONE;
      cdDrawRunning();
      return;
    }
    // Flash at under 10 seconds
    unsigned long rem = cdTarget - millis();
    if (rem < 10000) {
      if (millis() - cdLastFlash > 400) {
        cdLastFlash = millis();
        cdFlashOn = !cdFlashOn;
        // Just redraw the time in alternating colour
        unsigned int rh2 = rem / 3600000UL;
        unsigned int rm2 = (rem % 3600000UL) / 60000UL;
        unsigned int rs2 = (rem % 60000UL) / 1000UL;
        char tbuf[12]; snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", rh2, rm2, rs2);
        tft.setTextSize(4);
        tft.setTextColor(cdFlashOn ? T_BAD : T_BG, T_BG);
        tft.setCursor((320 - 8*24)/2, 60); tft.print(tbuf);
      }
    }
    // Redraw progress bar every second
    static unsigned long cdLastBarDraw = 0;
    if (millis() - cdLastBarDraw > 1000) {
      cdLastBarDraw = millis();
      cdDrawRunning();
    }
  }
}

void toolCDTap(int x, int y) {
  if (cdState == CD_SET) {
    // H +/-
    if (hit(x,y,10, 44,56,20)) { cdSetH=(cdSetH+1)%24; cdDrawSet(); }
    if (hit(x,y,10, 94,56,20)) { cdSetH=(cdSetH+23)%24; cdDrawSet(); }
    // M +/-
    if (hit(x,y,118,44,56,20)) { cdSetM=(cdSetM+1)%60; cdDrawSet(); }
    if (hit(x,y,118,94,56,20)) { cdSetM=(cdSetM+59)%60; cdDrawSet(); }
    // S +/-
    if (hit(x,y,226,44,56,20)) { cdSetS=(cdSetS+1)%60; cdDrawSet(); }
    if (hit(x,y,226,94,56,20)) { cdSetS=(cdSetS+59)%60; cdDrawSet(); }
    // START
    if (hit(x,y,60,130,90,28)) {
      unsigned long dur = ((unsigned long)cdSetH*3600 +
                           (unsigned long)cdSetM*60  + cdSetS) * 1000UL;
      if (dur > 0) {
        cdTarget = millis() + dur;
        cdState  = CD_RUNNING;
        cdDrawRunning();
      }
    }
    // BACK handled by caller
  } else {
    // PAUSE/RESUME
    if (hit(x,y,10,170,80,28)) {
      if (cdState == CD_RUNNING) {
        cdPauseRem = (millis() < cdTarget) ? (cdTarget - millis()) : 0;
        cdState = CD_PAUSED;
      } else if (cdState == CD_PAUSED) {
        cdTarget = millis() + cdPauseRem;
        cdState  = CD_RUNNING;
      }
      cdDrawRunning();
    }
    // RESET
    if (hit(x,y,100,170,80,28)) {
      cdState = CD_SET; cdDrawSet();
    }
    // BACK handled by caller
  }
}

// =================================================
// 5.  NAME / WORD GENERATOR
// Four categories; tap category then GENERATE.
// Shows result in scramble-reveal style.
// =================================================
static const char* GEN_NOUNS[] = {
  "SHADOW","RELIC","CIPHER","SIGNAL","REMNANT","FRACTURE","VESSEL",
  "ARCHIVE","WITNESS","THRESHOLD","MARGIN","CIRCUIT","STATIC",
  "FRAGMENT","INTERVAL","CURRENT","WEIGHT","ECHO","MARK","DUSK"
};
static const char* GEN_ADJECTIVES[] = {
  "HOLLOW","FRACTURED","DISTANT","SILENT","ERRANT","PALE","DEEP",
  "NARROW","REVERSED","FORGOTTEN","BRIGHT","COLD","STILL","VAST",
  "LOST","SHARP","QUIET","BROKEN","OLD","LAST"
};
static const char* GEN_VERBS[] = {
  "DRIFT","PERSIST","UNRAVEL","TRACE","REMAIN","COLLAPSE","SCATTER",
  "HOLD","RETURN","FADE","CLIMB","FALL","REACH","BURN","WATCH",
  "CROSS","BREAK","FOLLOW","WAIT","TURN"
};
static const char* GEN_NAMES[] = {
  "VAEL","CORVIN","MIRA","TASSE","ORREN","LYTH","SABLE","CAEN",
  "DREITH","FENNE","QUILL","ARLEN","NEVA","THORN","SEREN",
  "CASIMIR","ROOK","WYNN","ELARA","DUSK"
};

static const char** GEN_SETS[4] = {
  GEN_NOUNS, GEN_ADJECTIVES, GEN_VERBS, GEN_NAMES
};
static const char* GEN_LABELS[4] = { "NOUN","ADJ","VERB","NAME" };
#define GEN_COUNT 20

static int  genCategory = 0;
static char genResult[32] = "";
static int  genReveal = 0;
static bool genRevealing = false;

static bool genNeedsRedraw = false;

static void genDrawResult() {
  if (!genResult[0]) return;
  int cw = 18, rl = strlen(genResult);
  int rx = (320 - rl*cw)/2;
  tft.fillRect(0, 75, 320, 50, T_BG);
  tft.setTextSize(3);
  for (int i = 0; i < rl; i++) {
    char ch = (i < genReveal) ? genResult[i] : (char)random(33,126);
    tft.setTextColor(T_GOLD, T_BG);
    tft.setCursor(rx + i*cw, 90); tft.print(ch);
  }
  if (genRevealing && millis() - toolLastReveal > 60) {
    toolLastReveal = millis();
    if (genReveal < rl) genReveal++;
    else genRevealing = false;
  }
}

void toolGenDraw() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320-13*6)/2, 4); tft.print("WORD GENERATOR");

  for (int i = 0; i < 4; i++) {
    bool sel = (genCategory == i);
    int bx = 4 + i * 78;
    tft.drawRect(bx, 20, 74, 20, sel ? T_TEXT : T_DIM);
    tft.fillRect(bx+1, 21, 72, 18, sel ? tft.color565(20,40,80) : T_BG);
    tft.setTextSize(1);
    tft.setTextColor(sel ? T_TEXT : T_DIM, T_BG);
    tft.setCursor(bx + (74-strlen(GEN_LABELS[i])*6)/2, 27);
    tft.print(GEN_LABELS[i]);
  }

  if (genResult[0]) {
    genDrawResult();
  } else {
    tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
    tft.setCursor((320-18*6)/2, 100); tft.print("TAP GENERATE TO DRAW");
  }

  drawButton(85, 150, 150, 28, "GENERATE", false);
  drawButton(95, 210, 130, 24, "BACK", false);
}

void toolGenInit() {
  genResult[0] = '\0'; genReveal = 0; genRevealing = false;
  genCategory = 0; genNeedsRedraw = false;
}

void toolGenTap(int x, int y) {
  if (hit(x, y, 95, 210, 130, 24)) return;
  for (int i = 0; i < 4; i++) {
    if (hit(x, y, 4+i*78, 20, 74, 20)) {
      genCategory = i; genResult[0]='\0'; genReveal=0; genRevealing=false;
      toolGenDraw(); return;
    }
  }
  if (hit(x, y, 85, 150, 150, 28)) {
    const char* word = GEN_SETS[genCategory][random(GEN_COUNT)];
    strncpy(genResult, word, 31); genResult[31]='\0';
    genReveal = 0; genRevealing = true; genNeedsRedraw = true;
    toolLastReveal = millis();
    toolGenDraw();
  }
}

void toolGenTick() {
  if (genRevealing) genDrawResult();   // only update result area
}

// =================================================
// 6.  CIPHER WHEEL
// Caesar shift cipher: encode or decode text.
// On-screen character picker + shift selector.
// =================================================
static char  cipherInput[33]  = "";
static char  cipherOutput[33] = "";
static int   cipherShift      = 13;   // default ROT13
static bool  cipherEncoding   = true; // true=encode, false=decode
static int   cipherCursor     = 0;    // current position in input
static bool  cipherShowPicker = false;

// The character set for the picker (uppercase alpha + space + clear)
static const char CIPHER_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ _<";
// < = backspace

static void cipherCompute() {
  int len = strlen(cipherInput);
  for (int i = 0; i < len; i++) {
    char c = cipherInput[i];
    if (c == ' ') { cipherOutput[i] = ' '; continue; }
    if (c < 'A' || c > 'Z') { cipherOutput[i] = c; continue; }
    int shift = cipherEncoding ? cipherShift : (26 - cipherShift);
    cipherOutput[i] = 'A' + ((c - 'A' + shift) % 26);
  }
  cipherOutput[len] = '\0';
}

void toolCipherDraw() {
  tft.fillScreen(T_BG);
  tft.fillRect(0, 0, 320, 16, T_HEAD);
  tft.setTextSize(1); tft.setTextColor(T_TEXT, T_HEAD);
  tft.setCursor((320-12*6)/2, 4); tft.print("CIPHER WHEEL");

  drawButton(10,  20, 80, 18, "ENCODE",  cipherEncoding);
  drawButton(100, 20, 80, 18, "DECODE", !cipherEncoding);

  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(190, 23); tft.print("SHIFT:");
  drawButton(246, 20, 20, 18, "-", false);
  char sb[4]; snprintf(sb, sizeof(sb), "%2d", cipherShift);
  tft.setTextSize(1); tft.setTextColor(T_GOLD, T_BG);
  tft.setCursor(268, 23); tft.print(sb);
  drawButton(290, 20, 20, 18, "+", false);

  tft.setTextSize(1); tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(4, 44); tft.print("INPUT:");
  int inLen = strlen(cipherInput);
  char inDisp[34]; strncpy(inDisp, cipherInput, 32);
  if (inLen < 32) { inDisp[inLen]='_'; inDisp[inLen+1]='\0'; }
  tft.setTextColor(T_TEXT, T_BG);
  const char* inStart = inDisp;
  if (strlen(inDisp) > 26) inStart = inDisp + strlen(inDisp) - 26;
  tft.setCursor(4, 54); tft.print(inStart);

  tft.setTextColor(T_DIM, T_BG);
  tft.setCursor(4, 68); tft.print("OUTPUT:");
  tft.setTextColor(T_GOLD, T_BG);
  const char* outStart = cipherOutput;
  if (strlen(cipherOutput) > 26) outStart = cipherOutput + strlen(cipherOutput) - 26;
  tft.setCursor(4, 78); tft.print(outStart);

  tft.drawFastHLine(0, 90, 320, T_DIM);

  // Character grid: 6 cols × 5 rows, cell size 52×22 = fits in 312px × 110px
  // A-Z (26) + SP + DEL = 28 chars, 5 rows of 6 = 30 slots
  // Grid starts at y=93, ends at y=93+5*22=203 — BACK at y=208 clears it
  for (int i = 0; i < 29; i++) {
    char c = CIPHER_CHARS[i];
    int col = i % 6;
    int row = i / 6;
    int bx = 1 + col * 53;
    int by = 93 + row * 22;
    tft.drawRect(bx, by, 51, 20, T_DIM);
    tft.fillRect(bx+1, by+1, 49, 18, T_BG);
    tft.setTextSize(1); tft.setTextColor(c == '<' ? T_BAD : T_TEXT, T_BG);
    char label[3];
    if (c == ' ') { label[0]='S'; label[1]='P'; label[2]='\0'; }
    else if (c == '<') { label[0]='<'; label[1]='\0'; }
    else { label[0]=c; label[1]='\0'; }
    tft.setCursor(bx + (51 - strlen(label)*6)/2, by + 6);
    tft.print(label);
  }

  drawButton(95, 208, 130, 24, "BACK", false);
}

void toolCipherInit() {
  cipherInput[0] = cipherOutput[0] = '\0';
  cipherShift = 13; cipherEncoding = true;
}

void toolCipherTap(int x, int y) {
  if (hit(x, y, 95, 208, 130, 24)) return;

  if (hit(x, y, 10, 20, 80, 18)) { cipherEncoding = true;  cipherCompute(); toolCipherDraw(); return; }
  if (hit(x, y, 100,20, 80, 18)) { cipherEncoding = false; cipherCompute(); toolCipherDraw(); return; }

  if (hit(x, y, 246, 20, 20, 18)) {
    if (cipherShift > 1) cipherShift--; cipherCompute(); toolCipherDraw(); return;
  }
  if (hit(x, y, 290, 20, 20, 18)) {
    if (cipherShift < 25) cipherShift++; cipherCompute(); toolCipherDraw(); return;
  }

  for (int i = 0; i < 29; i++) {
    int col = i % 6;
    int row = i / 6;
    int bx = 1 + col * 53;
    int by = 93 + row * 22;
    if (hit(x, y, bx, by, 51, 20)) {
      char c = CIPHER_CHARS[i];
      int len = strlen(cipherInput);
      if (c == '<') {
        if (len > 0) { cipherInput[len-1] = '\0'; }
      } else if (len < 32) {
        cipherInput[len] = c; cipherInput[len+1] = '\0';
      }
      cipherCompute(); toolCipherDraw(); return;
    }
  }
}

// =================================================
// TOOLS PERSISTENCE  (tarot deck only needs saving)
// Call from main saveState / loadState.
// =================================================
void toolsSave() {
  prefs.begin("pscript", false);
  prefs.putBytes("trtd", tarotDeck, 22 * sizeof(int));
  prefs.putInt("trtp", tarotPos);
  prefs.end();
}

void toolsLoad() {
  prefs.begin("pscript", true);
  if (prefs.isKey("trtp")) {
    prefs.getBytes("trtd", tarotDeck, 22 * sizeof(int));
    tarotPos = prefs.getInt("trtp", 22);
  } else {
    tarotPos = 22;
  }
  prefs.end();
}