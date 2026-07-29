#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <math.h>
#include "index.h"
#include "cyber.h"
#include "tools.h"

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

// Lyrics screen structs — declared early so they're visible throughout the file
struct LyricsDir  { int dx; int dy; };
struct LyricsSlot {
  char wrapped[8][54]; int rowCount, totalChars, baseX, baseY, cw, ch;
  LyricsDir dir; bool active, holding, erasing, isEmphasis, isTitle;
  int reveal, holdTimer;
  unsigned long eraseMs;
};

// Effect structs declared here so they're visible throughout the file
struct EffectParticle {
  int x, y, vx, vy;
  uint16_t col;
  uint8_t life, type;
};
struct GlassCrack {
  int ox, oy, x0,y0,x1,y1,x2,y2;
  bool hasBranch; unsigned long born; int lifetime; bool active;
};
struct SwordSlash {
  int x0,y0,x1,y1;
  unsigned long born; int drawMs, holdMs;
  bool active, drawn;
};

// Logo pixel data copied to DRAM at boot.
// index_565 is PROGMEM (flash). Reading flash via pushImage while NVS writes
// flash simultaneously causes a flash cache stall = indefinite freeze.
// Copying to DRAM ensures pushImage never touches flash during runtime.
static uint16_t* logoBuf = nullptr;

// =================================================
// TOUCH
// =================================================
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// =================================================
// DISPLAY / BACKLIGHT
// =================================================
#define TFT_BL 21

// PWM for backlight (ESP32 core 3.x API)
#define BL_PWM_FREQ  5000
#define BL_PWM_RES   8        // 8-bit: 0-255

// ESP32-2432S028R (Cheap Yellow Display) note:
// GPIO 21 drives the backlight via an inverting NPN
// transistor  -  duty 0 = full brightness, 255 = off.
// BL_LEVELS are therefore inverted vs intuition.
//                         LOW   MID   HIGH
const uint8_t BL_LEVELS[3] = { 180, 80, 0 };
uint8_t brightnessLevel = 2;   // default: HIGH

// =================================================
// SPEED / ANIMATION TUNING
// These are runtime variables  -  adjustable in
// Settings → Animation Speed. Defaults below match
// the original hardcoded values.
// =================================================

// Glyph scramble: ms between scramble redraws.
// Lower = faster chaos. Range 20–200.
uint8_t  cfg_glyphSpinMs     = 90;   // default: 50% slower than before

// Reveal: characters unlocked per loop tick.
// 1 = slowest, 4 = fastest. Range 1–4.
uint8_t  cfg_revealChars     = 1;

// Transition fill/drain: pixels per 30ms frame.
// Higher = faster wipe. Range 5–40.
uint8_t  cfg_transitionPx    = 10;

// Derived  -  used the same way the old #defines were:
#define GLYPH_SPIN_MS        cfg_glyphSpinMs
#define REVEAL_CHARS_PER_TICK cfg_revealChars
#define RESULT_FILL_PX       cfg_transitionPx
#define RESULT_DRAIN_PX      cfg_transitionPx

// These are fixed (not exposed in UI  -  less need to tune):
#define RESULT_REVEAL_MS  120
#define DICE_SPIN_FRAMES   40
#define DICE_SPIN_MS       30
#define DICE_SETTLE_FRAMES 20

// Screen-sleep timeout  -  backlight off, separate from idle animation
// BL sleep fires after idle animation + extra time
unsigned long sleepMs  = 120000UL;   // idle animation trigger (configurable)
unsigned long blSleepMs = 300000UL;  // backlight off = idle + 3 min extra
bool sleeping = false;
unsigned long lastTouchTime = 0;

// =================================================
// DIFFICULTY  0=EASY  1=NORMAL  2=HARD  (per menu)
// =================================================
// =================================================
// PROGRESSIVE DIFFICULTY
// Difficulty auto-advances per menu as gamePrescripts are passed.
// diffMedThreshold[i] = passes needed to unlock Normal (was Easy)
// diffHardThreshold[i] = passes needed to unlock Hard
// Both are global (same threshold for all menus, configurable in settings)
// =================================================
int diffMedThreshold  = 10;   // passes to reach Normal difficulty
int diffHardThreshold = 25;   // passes to reach Hard difficulty

// Forward declaration  -  defined after passCount[] below
uint8_t menuDiffFor(int menuIdx);

// =================================================
// CATEGORY TOGGLE
// Controls which pools the TIMED menu draws from.
// The swipe rotation always shows all 5 screens.
// Bit 0=GAME 1=CITY 2=CON 3=TASK
// (Bit 4 TIMED not used here  -  timed screen is
//  always available in the swipe rotation.)
// =================================================
uint8_t timedPoolEnabled = 0b00001111;
bool    poolEnabled(int i) { return (timedPoolEnabled >> i) & 1; }
void    poolToggle(int i)  { timedPoolEnabled ^= (1 << i); }

// loopScreens declared after enum Screen below

// =================================================
// IDLE ANIMATION STATE
// =================================================
#define IDLE_TRIGGER_MS  30000UL
bool         idleActive           = false;
unsigned long lastInteractionTime = 0;
static bool _swallowNextRelease = false;
static unsigned long lastNavTime = 0;
#define NAV_COOLDOWN_MS 600             // ignore swipes for 600ms after navigation

struct IdlePart {
  int x, y;
  char text[28];
  uint16_t colour;     // particle colour — randomised per spawn
  uint8_t revIdx;
  uint8_t phase;       // 0=reveal 1=hold 2=erase
  unsigned long phaseStart;
};
#define IDLE_N 4
IdlePart idleParts[IDLE_N];
unsigned long idleLastSpawn = 0;

// Scan line state
static int   idleScanY    = 0;
static unsigned long idleScanLast = 0;
#define IDLE_SCAN_INTERVAL 25   // ms per step — fast enough to look alive

static const char* idleTexts[] = {
  // Mindfulness / grounding
  "OBSERVE","BREATHE","CORRECT POSTURE","DRINK WATER","LOOK UP",
  "ONE TASK","BE STILL","CLOSE A TAB","STAND UP","CHECK THE TIME",
  "LOOK AWAY","CLEAR ONE THING","WRITE IT DOWN","FIVE MINUTES","STEP OUTSIDE",
  "SLOW DOWN","EAT SOMETHING","REST YOUR EYES","STRETCH","NOTICE THE ROOM",
  "OPEN A WINDOW","TAKE A WALK","PUT THE PHONE DOWN","CLOSE YOUR EYES",
  "THREE DEEP BREATHS","RESET","REFOCUS","GROUND YOURSELF",
  // City / Index flavour
  "AWAIT INSTRUCTION","THE FLOOR KNOWS","INDEX ACTIVE","STANDBY",
  "MAINTAIN COVER","CONFIRM THE ROUTE","OBSERVE FIRST","STAY QUIET",
  "THE CONTACT WAITS","CHECK YOUR EXIT","DO NOT IMPROVISE","TRUST THE PLAN",
  "ONE TARGET ONLY","THE WORK IS NOT DONE","STAY CALM","THE NEST IS WATCHING",
  "MOVE WHEN READY","CONFIRM RECEIPT","NO TRACE","THE DOOR IS OPEN",
  "SOMETHING IS WATCHING","THE LIBRARY KNOWS","STAY IN SHADOW",
  "THE SIGNAL WILL COME","VERIFY TWICE","THE ASSIGNMENT IS CLEAR",
  "ENDURE","WAIT","PATIENCE","THE CLOCK IS RUNNING",
  "REMAIN UNSEEN","HOLD POSITION","ASSET SECURED","PROCEED WITH CAUTION",
  "EYES FORWARD","DEVIATION NOTED","COMPLY","THE CITY DOES NOT SLEEP",
  "FOLLOW THE RIBBON","LISTEN FIRST","ADAPT","MARK THE TARGET",
  "LEAVE NO EVIDENCE","THE GRID IS ACTIVE","REPORT WHEN DONE",
  "MOVE QUIETLY","TRUST NO ONE","SECURE THE PERIMETER",
  "THE MISSION CONTINUES","CALCULATE THE RISK","STAY THE COURSE",
  // System / abstract
  "PROCESSING","IDLE","MONITORING","SCANNING","CALIBRATING",
  "RUNNING DIAGNOSTICS","SIGNAL NOMINAL","NO ANOMALIES","UPTIME NOMINAL",
  "MEMORY INTACT","CORE STABLE","AWAITING INPUT","TASK PENDING",
  "QUEUE EMPTY","SYSTEMS NOMINAL","BUFFER CLEAR","SYNC COMPLETE",
  "PROTOCOL ACTIVE","STREAM INTACT","NODE CONNECTED",
};
#define IDLE_TEXT_COUNT 99

// Particle colour palette — dim, distinct, readable against dark BG
static uint16_t idleParticleColour() {
  switch (random(6)) {
    case 0: return tft.color565(40,  90,  140);  // steel blue
    case 1: return tft.color565(0,   110, 90);   // dark teal
    case 2: return tft.color565(80,  60,  130);  // slate purple
    case 3: return tft.color565(100, 90,  30);   // muted gold
    case 4: return tft.color565(30,  100, 60);   // sage green
    default:return tft.color565(100, 40,  60);   // muted crimson
  }
}

// =================================================
// TIMED MENU PAUSE STATE
// =================================================
bool timedPaused = false;
unsigned long timedPauseRem = 0;

#define BG_COLOR   tft.color565(15, 15, 15)
#define TEXT_COLOR tft.color565(153, 202, 255)
#define PASS_COLOR tft.color565(0, 45, 110)
#define FAIL_COLOR tft.color565(200, 30, 30)
#define RARE_COLOR tft.color565(210, 160, 20)
#define GOOD_COLOR tft.color565(0, 180, 80)   // streak personal-best

// =================================================
// ROUTER GAME
// =================================================
enum Screen {
  MENU_GAME,   // Game prescript menu
  MENU_1,      // CITY
  MENU_2,      // CON
  MENU_3,      // TASK
  MENU_4,      // TIMED
  MAIN_MENU,
  SETTINGS,
  SETTINGS_TIMER,
  SETTINGS_IDLE,
  SETTINGS_BRIGHT,
  SETTINGS_SPEED,
  SETTINGS_DIFFICULTY,
  SETTINGS_CATEGORIES,
  RESET_CONFIRM,
  HISTORY_SCREEN,
  DICE_ROLL,
  RESULT,
  IDLE_ANIM,
  GAME_PICKER,
  // ── Cyber tools ──────────────
  CYBER_MENU,
  CYBER_WIFI,
  CYBER_BLE,
  CYBER_BT,
  CYBER_QR,
  // ── Fun tools ────────────────
  TOOLS_MENU,
  TOOL_COIN,
  TOOL_TAROT,
  TOOL_ORACLE,
  TOOL_TIMER,
  TOOL_NAMES,
  TOOL_CIPHER,
  LYRICS_SCREEN,
  SONG_SELECT
};

Screen screen         = MENU_GAME;
Screen previousScreen = MENU_GAME;

// Always 5 screens in the swipe loop
Screen loopScreens[5] = {MENU_GAME,MENU_1,MENU_2,MENU_3,MENU_4};
int    loopLen        = 5;
int    loopIdx        = 0;

// =================================================
// GLOBAL INPUT STATE
// =================================================
bool lastTouch = false;
int  sx = 0, sy = 0;
unsigned long frameCount = 0;

// =================================================
// SESSION TIMER
// =================================================
unsigned long sessionStart = 0;   // millis() at boot / reset

// =================================================
// PROGRESS TRACKING PER MENU  (indices 0-4)
// 0=GAME 1=CITY 2=CON 3=TASK 4=TIMED
// =================================================
int passCount[5]   = {0,0,0,0,0};
int failCount[5]   = {0,0,0,0,0};
int streak[5]      = {0,0,0,0,0};   // current streak
int bestStreak[5]  = {0,0,0,0,0};   // all-time best streak
bool newBest       = false;          // set when a best is broken

// Definition here  -  forward declared above near diffMedThreshold
uint8_t menuDiffFor(int menuIdx) {
  int p = passCount[menuIdx];
  if (p >= diffHardThreshold) return 2;
  if (p >= diffMedThreshold)  return 1;
  return 0;
}

bool rewardActive = false;
char rewardText[128] = "";
unsigned long rewardStart = 0;

// =================================================
// HISTORY LOG  (last 10 per menu, stored as indices)
// index 0-3: normal prescript indices
// index = -1: was a rare event
// =================================================
#define HIST_LEN 10
int history[5][HIST_LEN];
int histHead[5] = {0,0,0,0,0};   // ring-buffer write head

void histPush(int menuIndex, int cardIndex) {
  history[menuIndex][histHead[menuIndex]] = cardIndex;
  histHead[menuIndex] = (histHead[menuIndex] + 1) % HIST_LEN;
}

// =================================================
// DICE STATE
// =================================================
enum DicePhase { DICE_SPIN, DICE_SETTLE, DICE_HOLD };
DicePhase dicePhase      = DICE_SPIN;
int       diceFinal      = 1;
int       diceCurrent    = 1;
int       diceFrame      = 0;
int       diceSpinFrames = DICE_SPIN_FRAMES;
int       diceSettleStep = 0;
int       diceSettleMax  = DICE_SETTLE_FRAMES;
unsigned long diceLastFrame = 0;

// =================================================
// TIMED MENU_GAME STATE
// =================================================
// Timer duration (default 20 min, 0 h, 0 d)
uint16_t timerMinutes = 20;
uint8_t  timerHours   = 0;
uint8_t  timerDays    = 0;

// Returns total timer duration in milliseconds
unsigned long timerDurationMs() {
  return ((unsigned long)timerDays    * 86400000UL)
       + ((unsigned long)timerHours   * 3600000UL)
       + ((unsigned long)timerMinutes * 60000UL);
}

// Timed-menu runtime state
bool     timedActive      = false;   // true while a timed prescript is live
bool     timedPassed      = false;   // true after user hits PASS within time
char     timedCurrent[256] = "";    // active timed prescript — fixed buffer, no heap
bool     timedIsRare      = false;
int      timedPool        = 0;       // 0=GAME 1=CITY 2=CON 3=TASK
int      timedRevealIndex = 0;       // scramble-reveal progress
char     timedLines[20][32];         // word-wrapped lines for timed text
int      timedLineCount   = 0;
unsigned long timedLastReveal = 0;
unsigned long timedStart  = 0;       // millis() when prescript was assigned

// Timer blink state  -  fires for 3 blinks when timer resets
int           timerBlinkCount = 0;       // blinks remaining (0 = not blinking)
unsigned long timerBlinkLast  = 0;       // millis of last blink toggle
bool          timerBlinkOn    = false;   // current blink visible state
int      timedCardIndex   = -1;      // deck card drawn (-1 = rare)

// Settings sub-screen edit fields
// We store the proposed new values while the user edits them
uint16_t editMinutes = 20;
uint8_t  editHours   = 0;
uint8_t  editDays    = 0;

// History view state
int histViewMenu = 0;   // which menu's history we're showing

// =================================================
// PRESCRIPTS  -  GAME (100)
// Each works across 2+ games in the list.
// =================================================

// =================================================
// ██████╗  █████╗ ███╗   ███╗███████╗
// ██╔════╝ ██╔══██╗████╗ ████║██╔════╝
// ██║  ███╗███████║██╔████╔██║█████╗  
// ██║   ██║██╔══██║██║╚██╔╝██║██╔══╝  
// ╚██████╔╝██║  ██║██║ ╚═╝ ██║███████╗
//  ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝
//
// PRESCRIPT POOLS  -  ordered by menu then difficulty
// Each menu has Easy → Normal → Hard → Rare in sequence.
// =================================================

// ─────────────────────────────────────────────────
// MENU 0: GAME
// ─────────────────────────────────────────────────
// DIFFICULTY  -  EASY (100 per menu)
// =================================================
const char* easyGame[] = {
"KILL 5 ENEMIES","PICK UP ONE ITEM","EXPLORE ONE NEW ROOM",
"RELOAD YOUR WEAPON","USE A HEALING ITEM","OPEN A CHEST OR CRATE",
"REACH THE NEXT CHECKPOINT","READ ONE PIECE OF LORE",
"CRAFT OR BUILD ONE THING","COMPLETE ONE OBJECTIVE",
"FIND AN AMMO CACHE","EQUIP A NEW ITEM","SPEND ONE CURRENCY UNIT",
"WALK TO THE EDGE OF THE MAP","PRESS A BUTTON OR SWITCH",
"KILL ONE ENEMY WITH MELEE","USE AN ABILITY ONCE",
"FIND THE EXIT OF THE CURRENT AREA","TALK TO AN NPC OR ALLY",
"PICK UP THE FIRST WEAPON YOU SEE","IDENTIFY ONE ENEMY TYPE",
"REACH HIGHER GROUND","BREAK ONE DESTRUCTIBLE OBJECT",
"USE A DOOR OR HATCH","THROW AN OBJECT AT AN ENEMY",
"CROUCH AND MOVE SLOWLY FOR 30 SECONDS","FIND A SAFE ZONE",
"COLLECT ONE CURRENCY UNIT","OBSERVE AN ENEMY BEFORE ENGAGING",
"SWITCH TO A DIFFERENT WEAPON","USE A VEHICLE OR MOUNT",
"JUMP OVER AN OBSTACLE","COMPLETE ONE WAVE OR ROUND",
"REVIVE A TEAMMATE ONCE","PICK UP DROPPED ENEMY LOOT",
"FIND A LIGHT SOURCE","USE COVER DURING A FIREFIGHT",
"REACH FULL HEALTH USING ANY METHOD","COMPLETE THE TUTORIAL PROMPT",
"FIRE YOUR WEAPON ONCE","OPEN THE MAP AND MARK ONE POINT",
"FIND A LADDER OR VENT AND USE IT","PUSH A PHYSICAL OBJECT",
"COMPLETE AN ACTION IN UNDER TEN SECONDS","FIND A HIGH POINT TO LOOK FROM",
"STAND STILL AND OBSERVE FOR 30 SECONDS","DROP ONE ITEM ON PURPOSE",
"LET AN ENEMY PASS WITHOUT ENGAGING","FIND AND ENTER A NEW BUILDING",
"USE THE SPRINT ABILITY","COMPLETE A QUICK ACTION PROMPT",
"OPEN THE INVENTORY AND REVIEW IT","LISTEN FOR AN AUDIO CUE",
"COMPLETE ONE SMALL SIDE OBJECTIVE","USE A SPECIAL ATTACK ONCE",
"FIND A RESOURCE NODE AND COLLECT IT","DESTROY ONE ENEMY PROP",
"REACH A WAYPOINT WITHOUT SPRINTING","FIRE AT A TARGET AND MISS ON PURPOSE",
"FIND AN EXIT SIGN OR INDICATOR","COLLECT MAXIMUM BASIC AMMO",
"EQUIP YOUR SECONDARY WEAPON","USE A DEFENCE ITEM ONCE",
"KILL ONE ENEMY FROM BEHIND","FIND WATER OR A LIQUID AREA",
"WALK TO THE CENTRE OF THE MAP","USE A SKILL TREE UPGRADE",
"FIND THE LOWEST POINT IN THE AREA","COMPLETE A RELOAD FROM EMPTY",
"PICK UP AND DROP AN ITEM TWICE","USE YOUR FLASHLIGHT OR TORCH",
"FIND THE LARGEST ENEMY IN THE AREA","OBSERVE THE SKYBOX OR CEILING",
"SWITCH AMMO TYPE OR FIRE MODE","USE A COMPANION OR PET ABILITY",
"REACH A LOCKED AREA AND FIND THE KEY","IDENTIFY THE BOSS ENEMY",
"COMPLETE ONE TASK FROM YOUR QUEST LOG","FIND AN EASTER EGG OR SECRET",
"TAKE DAMAGE AND SURVIVE","USE AN EXPLOSIVE ITEM ONCE",
"REACH A POINT WITH NO ENEMIES NEARBY","FIND AND EQUIP ARMOUR",
"KILL AN AIRBORNE OR FLYING ENEMY","USE A TURRET OR MOUNTED WEAPON",
"NAVIGATE TO THE QUEST MARKER","COMPLETE THE CURRENT WAVE",
"FIND A MEDKIT OR RECOVERY ITEM","SHOOT AN ENEMY IN THE HEAD",
"USE A GRENADE OR THROWN ITEM","REACH MAXIMUM CARRY WEIGHT",
"COMPLETE AN OBJECTIVE ALONE","OPEN A LOCKED CONTAINER",
"FIND THE HIGHEST ELEVATION IN THE AREA","COMPLETE ONE GAME LOOP",
"KILL A SINGLE ENEMY TWICE WITH DIFFERENT WEAPONS","FIND A FRIENDLY NPC",
"REACH A PREVIOUSLY VISITED AREA","LOCATE THE ENEMY SPAWN POINT"
};
const int EASY_GAME_COUNT = 100;



// PRESCRIPTS  -  GAME (100)
// Each works across 2+ games in the list.
// =================================================
const char* gamePrescripts[] = {
"SURVIVE 10 MINUTES WITHOUT TAKING DAMAGE",
"COMPLETE A MISSION USING ONLY YOUR STARTING LOADOUT",
"FIND AND READ EVERY PIECE OF LORE IN THE CURRENT AREA",
"KILL 20 ENEMIES WITHOUT USING YOUR PRIMARY WEAPON",
"REACH THE NEXT CHECKPOINT WITHOUT SPENDING ANY RESOURCES",
"COMPLETE A FULL RUN WITHOUT DYING",
"HELP A TEAMMATE OR ALLY COMPLETE THEIR OBJECTIVE",
"EXPLORE AN AREA YOU HAVE NEVER VISITED BEFORE",
"DEFEAT AN ENEMY USING ONLY MELEE ATTACKS",
"COLLECT 100 OF ANY SINGLE RESOURCE IN ONE SESSION",
"COMPLETE A CHALLENGE WITHOUT USING YOUR MOST POWERFUL ABILITY",
"FIND A SECRET OR HIDDEN AREA AND DOCUMENT WHAT IS INSIDE",
"SURVIVE A WAVE OR ENCOUNTER WHILE AT LOW HEALTH",
"BUILD OR CRAFT SOMETHING YOU HAVE NEVER MADE BEFORE",
"COMPLETE AN OBJECTIVE WITH EXACTLY ONE RESOURCE REMAINING",
"PLAY FOR 30 MINUTES WITHOUT PAUSING OR OPENING A MENU",
"DEFEAT A BOSS OR ELITE ENEMY WITHOUT TAKING A HIT",
"TRADE OR TRANSFER AN ITEM TO ANOTHER PLAYER OR CHARACTER",
"FIND THE HIGHEST ACCESSIBLE POINT IN THE CURRENT LEVEL",
"COMPLETE A MISSION OR RUN WITHOUT PICKING UP ANY LOOT",
"DEFEND A POSITION FOR AS LONG AS POSSIBLE BEFORE RETREATING",
"REACH MAX AMMO OR RESOURCES BEFORE ENGAGING THE NEXT ENCOUNTER",
"COMPLETE A LEVEL USING ONLY ONE SPECIFIC WEAPON TYPE",
"FIND AND ELIMINATE ALL ENEMIES IN AN AREA BEFORE ADVANCING",
"EARN OR COLLECT ENOUGH CURRENCY FOR A MAJOR UPGRADE",
"COMPLETE AN OBJECTIVE IN UNDER TWO MINUTES",
"SURVIVE AN ENCOUNTER WHILE SUPPORTING OTHERS RATHER THAN ATTACKING",
"FIND A PIECE OF EQUIPMENT WITH A HIGHER RARITY THAN YOUR CURRENT",
"REACH A LOCATION MARKED ON THE MAP WITHOUT FAST TRAVEL",
"COMPLETE THE CURRENT OBJECTIVE WITHOUT USING ANY CONSUMABLES",
"KILL AN ENEMY WITH THEIR OWN MECHANIC OR ENVIRONMENTAL HAZARD",
"REACH A NEW AREA AND IDENTIFY THREE THREATS BEFORE ENGAGING",
"COMPLETE A FULL LOADOUT CHANGE AND CLEAR AN ENCOUNTER WITH IT",
"FIND THE EXIT OR OBJECTIVE WHILE AVOIDING ALL COMBAT",
"UNLOCK OR ACCESS AN AREA THAT REQUIRES A SPECIFIC KEY OR SKILL",
"COMPLETE A BONUS OBJECTIVE OR SIDE MISSION IN THE CURRENT SESSION",
"EARN A SCORE OR RANK ABOVE YOUR PREVIOUS BEST",
"DEFEAT THREE DIFFERENT ENEMY TYPES IN THE SAME ENCOUNTER",
"COMPLETE AN ENCOUNTER WITH ALL ORIGINAL TEAMMATES ALIVE",
"IDENTIFY THE WEAKNESSES OF A NEW ENEMY TYPE AND EXPLOIT THEM",
"FIND AND PICK UP AN ITEM YOU HAVE NEVER COLLECTED BEFORE",
"COMPLETE A MISSION AT HIGHER DIFFICULTY THAN YOUR DEFAULT",
"REACH A SAFE ZONE OR EXTRACTION POINT WITH FULL HEALTH",
"ACHIEVE THE FASTEST COMPLETION TIME FOR A SECTION YOU KNOW WELL",
"USE AN ABILITY OR ITEM YOU NORMALLY AVOID AND COMPLETE AN OBJECTIVE",
"CLEAR AN ENCOUNTER WITHOUT THE ENEMY REACHING ITS GOAL",
"FIND AND DESTROY EVERY BREAKABLE OR DESTRUCTIBLE IN AN AREA",
"COMPLETE A FULL INVENTORY REORGANISATION BEFORE THE NEXT ENCOUNTER",
"HELP ANOTHER PLAYER SURVIVE AN ENCOUNTER THEY WOULD HAVE FAILED",
"REACH THE END OF A LEVEL WITHOUT BEING SPOTTED OR DETECTED",
"KILL AN ENEMY FROM MAXIMUM POSSIBLE RANGE",
"COMPLETE A WAVE OR ROUND WHILE ALL PLAYERS SURVIVE",
"FIND THE WEAKEST ENEMY IN THE CURRENT AREA AND IGNORE ALL OTHERS",
"COMPLETE AN OBJECTIVE WHILE CARRYING THE HEAVIEST OR WORST LOADOUT",
"SURVIVE FIVE CONSECUTIVE ENCOUNTERS WITHOUT RETURNING TO BASE",
"FIND AND ACTIVATE EVERY INTERACTABLE OBJECT IN THE CURRENT AREA",
"DEFEAT AN ENEMY TYPE THAT HAS PREVIOUSLY KILLED YOU",
"COMPLETE A MISSION AND RETURN ALL LOOT TO BASE WITHOUT LOSING ANY",
"REACH A MILESTONE OR LEVEL THRESHOLD IN THIS SESSION",
"FIND AN ALTERNATE OR HIDDEN ROUTE THROUGH A LEVEL",
"COMPLETE AN ENCOUNTER USING ONLY ABILITIES WITH COOLDOWNS",
"KILL FIVE ENEMIES IN UNDER TEN SECONDS",
"REACH AN AREA AND MAP OR MARK EVERY POINT OF INTEREST",
"COMPLETE A MISSION WITH ZERO FRIENDLY FIRE INCIDENTS",
"SURVIVE AN ENCOUNTER WITHOUT MOVING FROM YOUR STARTING POSITION",
"FIND THE RAREST DROP OR ITEM AVAILABLE IN THE CURRENT AREA",
"COMPLETE A LEVEL SEGMENT USING ONLY ENVIRONMENTAL KILLS",
"FIND AND COMPLETE AN OPTIONAL OR HIDDEN CHALLENGE",
"REACH A NEW CHECKPOINT AND SPEND NO RESOURCES UNTIL THE NEXT ONE",
"DEFEAT AN ELITE OR NAMED ENEMY USING ONLY BASIC ATTACKS",
"COMPLETE AN OBJECTIVE WITHOUT TRIGGERING AN ALERT OR ALARM",
"RETURN TO AN EARLIER AREA AND CLEAR ENEMIES YOU LEFT BEHIND",
"COMPLETE A FULL SESSION AS A ROLE YOU DO NOT NORMALLY PLAY",
"FIND THE SAFEST PATH FROM START TO FINISH IN A LEVEL",
"KILL AN ENEMY USING A WEAPON FROM THEIR FACTION OR TYPE",
"COMPLETE AN ENCOUNTER WITH ONLY THE FIRST WEAPON YOU PICKED UP",
"FIND EVERY COLLECTABLE IN THE CURRENT LEVEL OR ZONE",
"SURVIVE AN ENCOUNTER WHILE CARRYING THE MAXIMUM POSSIBLE LOAD",
"COMPLETE A MISSION AND LOSE NO MORE THAN TEN PERCENT OF RESOURCES",
"DEFEAT A BOSS USING ONLY NON-META OR RARELY USED TOOLS",
"REACH THE NEXT AREA USING ONLY SKILLS UNLOCKED IN THE LAST SESSION",
"COMPLETE AN ENCOUNTER WITHOUT EVER RELOADING YOUR WEAPON",
"FIND A LOCATION THAT PROVIDES LINE OF SIGHT ON THE ENTIRE AREA",
"COMPLETE A MISSION IN COMPLETE SILENCE - NO ABILITIES THAT MAKE NOISE",
"EARN THREE COMMENDATIONS OR MEDALS IN A SINGLE SESSION",
"FIND AN ENEMY PATROL ROUTE AND ELIMINATE THEM IN ORDER",
"COMPLETE AN ENCOUNTER USING ONLY THE LAST ITEM IN YOUR INVENTORY",
"REACH A HIGH-VALUE TARGET LOCATION WITHOUT ANY SUPPORT ABILITIES",
"SURVIVE A FULL SESSION WITH THE GAME'S HARDEST MODIFIER ACTIVE",
"FIND THE EXACT LOCATION DESCRIBED BY A PIECE OF IN-GAME LORE",
"COMPLETE AN ENCOUNTER WHILE PROTECTING A FRAGILE OBJECTIVE",
"DEFEAT AN ENEMY AT ITS MAXIMUM HEALTH IN UNDER THIRTY SECONDS",
"FIND AND USE EVERY WEAPON OR ABILITY AVAILABLE IN ONE ENCOUNTER",
"SURVIVE AN AMBUSH OR SURPRISE ENCOUNTER WITHOUT PREPARATION",
"FIND THE HIGHEST THREAT ENEMY IN THE AREA AND DEFEAT IT FIRST",
"COMPLETE A MISSION AND END WITH MORE RESOURCES THAN YOU STARTED",
"ACHIEVE A PERFECT OR FLAWLESS RATING ON ANY SCORED SECTION",
"FIND THREE PIECES OF LORE THAT CONNECT TO THE SAME STORY THREAD",
"WIN A MATCH WITHOUT YOUR TEAM LOSING THE OBJECTIVE",
"PLAY A SUPPORT OR HEALER ROLE AND KEEP EVERY ALLY ALIVE FOR A FULL ROUND",
"ELIMINATE THREE ENEMIES IN A SINGLE PUSH WITHOUT RETREATING",
"COMPLETE AN OBJECTIVE WHILE UNDER FIRE WITHOUT BREAKING LINE",
"HOLD A POSITION UNTIL THE TIMER RUNS OUT WITHOUT GIVING GROUND",
"WIN A MATCH SWITCHING ROLES AT LEAST ONCE BASED ON WHAT THE TEAM NEEDS",
"FINISH WITH THE HIGHEST SCORE ON YOUR TEAM WITHOUT BEING THE PRIMARY ATTACKER"
};
const int GAME_POOL_COUNT = 100;


// =================================================
// =================================================

// DIFFICULTY  -  HARD (100 per menu)
// =================================================
const char* hardGame[] = {
"COMPLETE A FULL RUN ON THE HIGHEST DIFFICULTY WITHOUT DYING ONCE",
"DEFEAT THE FINAL BOSS USING ONLY THE STARTING WEAPON OR LOADOUT",
"FIND AND COLLECT EVERY COLLECTIBLE IN A SINGLE LEVEL OR ZONE",
"COMPLETE A NO-HIT RUN OF AN ENTIRE LEVEL OR ENCOUNTER",
"REACH THE FINAL CHECKPOINT WITHOUT USING ANY HEALING ITEMS",
"COMPLETE A SPEEDRUN OF A LEVEL UNDER A SPECIFIC TARGET TIME",
"DEFEAT EVERY ELITE ENEMY IN A LEVEL WITHOUT TAKING DAMAGE",
"COMPLETE A PERMADEATH OR IRONMAN RUN THROUGH AN ENTIRE SEGMENT",
"REACH A NEW PERSONAL BEST IN A SCORED OR TIMED CHALLENGE",
"COMPLETE A RUN USING THE MOST UNDERPOWERED LOADOUT AVAILABLE",
"DEFEAT A BOSS BLINDFOLDED OR WITHOUT SEEING THE SCREEN FOR 30 SECONDS",
"COMPLETE A SOLO RUN OF CONTENT DESIGNED FOR A FULL TEAM",
"REACH AND DEFEAT EVERY OPTIONAL OR SECRET BOSS IN THE CURRENT AREA",
"COMPLETE THREE CONSECUTIVE RUNS WITHOUT A SINGLE FAILURE",
"DEFEAT AN ENEMY USING ONLY MECHANICS YOU LEARNED IN THIS SESSION",
"COMPLETE A LEVEL WHILE INTENTIONALLY HANDICAPPED BY GAME MODIFIERS",
"REACH 100 PERCENT COMPLETION OF A SINGLE AREA OR ZONE",
"DEFEAT A BOSS WITHOUT EVER ATTACKING  -  ONLY THROUGH ENVIRONMENT",
"COMPLETE A FULL RAID OR MISSION WITHOUT COMMUNICATING WITH TEAMMATES",
"REACH THE HIGHEST POSSIBLE LEVEL OR RANK IN THE CURRENT SESSION",
"COMPLETE A PACIFIST RUN OF A LEVEL THAT NORMALLY REQUIRES COMBAT",
"DEFEAT FIVE BOSSES IN A ROW WITHOUT RETURNING TO BASE",
"COMPLETE EVERY OPTIONAL CHALLENGE IN THE CURRENT MISSION",
"REACH THE END OF A LEVEL WITH MAXIMUM POSSIBLE RESOURCES",
"DEFEAT A NORMALLY DIFFICULT BOSS IN UNDER THE PAR TIME",
"COMPLETE A RUN COLLECTING ONLY ONE TYPE OF ITEM",
"REACH A HIDDEN AREA THAT REQUIRES ADVANCED MOVEMENT TECHNIQUES",
"COMPLETE A LEVEL USING ONLY CONSUMABLES AND THROWN ITEMS",
"DEFEAT A BOSS WHILE PROTECTING ONE OR MORE FRAGILE ALLIES",
"COMPLETE THE HARDEST ACHIEVEMENT YOU HAVE NEVER UNLOCKED",
"REACH THE END OF AN ENDLESS MODE FURTHER THAN YOUR PERSONAL BEST",
"DEFEAT AN ENEMY THAT REQUIRES PRECISE TIMING OF MULTIPLE MECHANICS",
"COMPLETE A FULL RUN WITHOUT EVER OPENING THE PAUSE MENU",
"REACH A PROVABLY OPTIMAL BUILD OR LOADOUT FROM SCRATCH",
"DEFEAT EVERY ENEMY IN A LEVEL WITHIN A STRICT TIME WINDOW",
"COMPLETE A CHALLENGE NORMALLY REQUIRING A PARTY USING SOLO TACTICS",
"REACH THE NEXT MILESTONE TIER ENTIRELY THROUGH SKILL NOT GRINDING",
"DEFEAT AN ENEMY USING A STRATEGY DOCUMENTED NOWHERE IN THE GAME",
"COMPLETE A LEVEL USING ONLY ENVIRONMENTAL HAZARDS AS WEAPONS",
"REACH FULL MASTERY OF A SINGLE WEAPON OR ABILITY IN ONE SESSION",
"DEFEAT THREE BOSSES BACK-TO-BACK WITHOUT BREAKS OR HEALING",
"COMPLETE A FULL RUN WITHOUT EVER MISSING A SHOT OR ATTACK",
"REACH AN ENDGAME ENCOUNTER UNDER-LEVELLED AND WIN ANYWAY",
"DEFEAT AN ENEMY USING THE MECHANIC IT IS DESIGNED TO COUNTER",
"COMPLETE A LEVEL WITHOUT EVER USING A FAST TRAVEL OR SHORTCUT",
"REACH 100 PERCENT MAP COMPLETION INCLUDING SECRET AREAS",
"DEFEAT A BOSS IN UNDER 30 SECONDS USING ANY MEANS NECESSARY",
"COMPLETE A FULL SESSION WITHOUT REPEATING A SINGLE TACTIC TWICE",
"REACH THE TOP OF A LEADERBOARD OR RANKING SYSTEM",
"DEFEAT AN ENEMY THAT HAS KILLED YOU MORE THAN TEN TIMES BEFORE",
"COMPLETE A RUN WHERE EVERY DECISION FOLLOWS A SPECIFIC RULE",
"REACH AND DEFEAT THE HARDEST ENEMY VARIANT AT FULL POWER",
"COMPLETE A LEVEL USING ONLY ABILITIES FROM ONE SKILL TREE BRANCH",
"DEFEAT AN ELITE ENEMY ON YOUR FIRST ATTEMPT WITH NO PRIOR KNOWLEDGE",
"COMPLETE A FULL RUN WHILE NARRATING EVERY ACTION OUT LOUD",
"REACH THE EXIT OF AN AREA WITHOUT TAKING ANY OFFENSIVE ACTIONS",
"DEFEAT A BOSS WHILE BELOW 25 PERCENT HEALTH FOR THE ENTIRE FIGHT",
"COMPLETE A LEVEL DURING WHICH YOU CHANGE LOADOUTS THREE TIMES",
"REACH AN ACHIEVEMENT WITH UNDER 5 PERCENT GLOBAL COMPLETION",
"DEFEAT EVERY ENEMY OF A SPECIFIC TYPE IN A LEVEL WITH ONE HIT EACH",
"COMPLETE A FULL RUN OF A ROGUELIKE WITHOUT TAKING ANY ITEMS",
"REACH AND ACTIVATE A SECRET MECHANIC NOT DOCUMENTED IN THE GAME",
"DEFEAT A BOSS USING ITS OWN ATTACKS REFLECTED BACK AT IT",
"COMPLETE A LEVEL WITHOUT EVER SETTING FOOT ON THE GROUND",
"REACH A LEGENDARY OR MYTHIC TIER ITEM THROUGH FAIR PLAY ONLY",
"DEFEAT EVERY OPTIONAL ENEMY IN AN AREA BEFORE THE PRIMARY OBJECTIVE",
"COMPLETE A FULL RUN AT THE LOWEST POSSIBLE FRAME RATE AND WIN",
"REACH THE END OF A SECRET PATH NORMAL PLAYERS NEVER ENCOUNTER",
"DEFEAT A BOSS USING THE WEAKEST WEAPON IN YOUR INVENTORY",
"COMPLETE A LEVEL WHERE EVERY ENEMY KILL MUST BE A CRITICAL HIT",
"REACH MAXIMUM POWER LEVEL OR PRESTIGE IN A SINGLE SESSION",
"DEFEAT AN ENEMY THAT REQUIRES ABILITIES YOU HAVE NEVER USED",
"COMPLETE A FULL RUN WHERE EVERY ENEMY DROP IS USED OR EQUIPPED",
"REACH AN AREA THAT REQUIRES SEQUENCE BREAKING OR EXPLOIT KNOWLEDGE",
"DEFEAT THREE BOSSES USING THREE DIFFERENT STRATEGIES IN A ROW",
"COMPLETE A LEVEL ENTIRELY ON ONE HEALTH POINT",
"REACH THE END OF A LEVEL HAVING TOUCHED EVERY INTERACTABLE OBJECT",
"DEFEAT A BOSS WHOSE MECHANICS YOU LEARN PURELY THROUGH OBSERVATION",
"COMPLETE A CHALLENGE THAT REQUIRES KNOWLEDGE FROM ANOTHER GAME",
"REACH MASTERY OF AN ENTIRE WEAPON CLASS IN ONE SESSION",
"DEFEAT EVERY POSSIBLE VARIATION OF ONE BOSS TYPE",
"COMPLETE A FULL RUN WHERE YOUR LOADOUT IS DETERMINED BY DICE ROLLS",
"REACH AN ENDING THAT REQUIRES SPECIFIC HIDDEN CONDITIONS",
"DEFEAT A BOSS WITHIN A WINDOW OF ITS WEAKNESS PHASE ONLY",
"COMPLETE A LEVEL USING ONLY MOVEMENT ABILITIES  -  NO ATTACKS",
"REACH THE HIGHEST WAVE OR ROUND POSSIBLE IN AN ENDLESS MODE",
"DEFEAT AN ENEMY AT THE EXACT FRAME ITS MECHANIC SHOULD WIN",
"COMPLETE A FULL SESSION WITHOUT EVER RESPAWNING OR RELOADING A SAVE",
"REACH A NORMALLY INACCESSIBLE AREA THROUGH MOVEMENT TECH",
"DEFEAT AN ENTIRE WAVE OR ROUND USING ONLY THROWN OBJECTS",
"COMPLETE A LEVEL WITH ZERO RESOURCES SPENT AT THE END",
"REACH A SCORE THAT EXCEEDS THE PUBLISHED MAXIMUM EXPECTATION",
"DEFEAT EVERY BOSS IN THE GAME IN ONE CONTINUOUS SESSION",
"COMPLETE A FULL RUN WHILE OPERATING ONE-HANDED",
"REACH AN AREA AND COMPLETE IT WHILE A TEAMMATE OBSERVES",
"DEFEAT A BOSS USING A METHOD YOU DISCOVERED YOURSELF",
"COMPLETE A LEVEL WHERE EVERY DECISION IS A COIN FLIP",
"REACH THE END OF A LEVEL WHILE TEACHING SOMEONE ELSE THE MECHANICS",
"DEFEAT EVERY ENEMY IN AN AREA WITHOUT THEM EVER SEEING YOU",
"COMPLETE A FULL RUN PROVING A STRATEGY OTHERS BELIEVED IMPOSSIBLE"
};
const int HARD_GAME_COUNT = 100;



const char* rareGame[] = {
"THE SYSTEM HAS FLAGGED AN ANOMALY IN YOUR ROUTINE. IDENTIFY IT",
"A THREAD HAS COME LOOSE. FIND IT BEFORE IT UNRAVELS FURTHER",
"SOMETHING IN YOUR ENVIRONMENT IS WATCHING YOU WAIT. STOP WAITING",
"THE RECORD SHOWS YOU HAVE DONE THIS BEFORE. DO IT DIFFERENTLY THIS TIME",
"AN UNRESOLVED ITEM IS ACCUMULATING WEIGHT. NAME IT OUT LOUD",
"LOCATE THE TASK YOU ARE MOST AFRAID TO START AND OPEN IT"
};
const int RARE_GAME_COUNT = 6;


// ─────────────────────────────────────────────────
// MENU 1: CITY (Index / Library of Ruina themed)
// ─────────────────────────────────────────────────
const char* easyCity[] = {
"Walk to the nearest window and look outside for 40 seconds.",
"Sit somewhere you have never sat before.",
"Touch something cold.",
"Walk one block further than you planned to.",
"Change which pocket you keep your phone in for the rest of the day.",
"Stand up before you are ready.",
"Drink water before leaving.",
"Count how many people are wearing blue within your view.",
"Relocate to a different seat.",
"Walk to a door and open it without going through.",
"Notice the tallest thing visible to you right now.",
"Leave the room for exactly 2 minutes.",
"Find something red and stand next to it.",
"Do not check your phone for the next 20 minutes.",
"Tie your shoe even if it does not need it.",
"Wait at a crosswalk for the full cycle without crossing.",
"Carry something in your non-dominant hand for one hour.",
"Look at the floor for 30 seconds.",
"Stand outside for 3 minutes without looking at anything in particular.",
"Take the next available seat.",
"Nod at the next person who makes eye contact.",
"Take the long way back.",
"Place one object from your right pocket into your left.",
"Face a different direction than you are currently facing.",
"Pause before responding to the next person who speaks to you.",
"Remove one item from what you are carrying.",
"Step outside and come back in.",
"Look up at the ceiling.",
"Read the next sign you see, twice.",
"Hold still for 90 seconds.",
"Move three steps to your left.",
"Stand up straight.",
"Notice something you have walked past before without seeing.",
"Finish whatever you are drinking before moving on.",
"Close your eyes for 15 seconds.",
"Go to the ground floor.",
"Put something down that you are holding.",
"Eat something small.",
"Pick a number between 1 and 10 and walk that many steps in any direction.",
"Identify the nearest exit.",
"Sit with your back against a wall.",
"Look out a window you rarely look out of.",
"Walk slowly for the next two minutes.",
"Check the time, then do not check it again for one hour.",
"Find a place where no one can see your face and stand there for 30 seconds.",
"Count your steps until you reach 100.",
"Hold your breath for 20 seconds.",
"Move to a quieter area.",
"Find something symmetrical.",
"Write one word on paper and keep it until tonight.",
"Go somewhere you have not been today.",
"Remain standing for the next 15 minutes.",
"Walk to the farthest visible point from where you are.",
"Notice the temperature of the air on your skin.",
"Avoid talking for 10 minutes.",
"Stand in the middle of a room.",
"Turn off a light.",
"Identify what is directly behind you.",
"Find the oldest object visible to you.",
"Walk to a window and close it if it is open, open it if it is closed.",
"Leave nothing on the surface nearest to you.",
"Put your hands in your pockets and keep them there for 5 minutes.",
"Find somewhere to stand where you are not blocking anyone.",
"Look directly at the sun for 0 seconds.",
"Place your feet flat on the floor.",
"Take three slow breaths.",
"Stand near water.",
"Find the smallest thing you can see.",
"Remove your shoes for one minute.",
"Find a chair that faces the door.",
"Go to the highest point you can reach without climbing.",
"Do not sit down for 30 minutes.",
"Find a flat surface and place your hand on it.",
"Walk backwards for 10 steps.",
"Carry nothing for 5 minutes.",
"Spend 2 minutes not moving.",
"Read something you would not normally read.",
"Look at your hands.",
"Find the thickest object within reach.",
"Stand near someone you do not know and say nothing.",
"Locate a clock and watch it for one minute.",
"Find a shadow and stand in it.",
"Go somewhere bright.",
"Face the direction of north.",
"Find a way out of this room that is not the main entrance.",
"Wait until the next person walks past, then move.",
"Find three things that are the same colour.",
"Stay in one place until someone asks if you are alright.",
"Walk at half your normal speed for 5 minutes.",
"Choose an object nearby and carry it with you for one hour.",
"Look at your reflection.",
"Find something that makes a sound and make it make that sound.",
"Sit as close to a wall as possible.",
"Open something that was closed.",
"Go somewhere you can smell food.",
"Find an object older than you and hold it.",
"Do nothing for 60 seconds and call it deliberate.",
"Wait."
};
const int EASY_CITY_COUNT = 100;





// PRESCRIPTS  -  CITY (100)
// Index-style directives, pre-Star-of-City only.
// Mix of operational (direct) and philosophical (cryptic).
// =================================================
const char* cityPrescripts[] = {
"If you get wet, pretend everything is normal and do not use the bathroom for 3 hours.",
"When preparing to leave your home, walk to a hotel without taking the shortest route.",
"After finishing a meal, wait 1059 seconds, then walk up to someone in a sweater.",
"Take the train.",
"After changing clothes, gesture for a shy person to follow you, then buy them 3 tan objects.",
"Do not speak for 2 hours. If asked why, write the word PROTOCOL on your hand and show it.",
"Walk to the nearest body of water. Touch it. Leave without looking back.",
"Remember the next prescript you receive for the next 4 months and act on it at least once.",
"Spend 77 minutes doing something a stranger suggested without asking why they suggested it.",
"Find a phone booth or any enclosed standing space. Stand in it for exactly 9 minutes.",
"Between now and midnight, refer to all money as paperweight.",
"At your next meal, eat in reverse order of what you would normally choose.",
"Carry a folded piece of blank paper in your left hand for one full hour.",
"Walk to a hotel lobby and sit for 11 minutes as though you are waiting for someone.",
"Deliver this instruction to someone else: do not tell them where it came from.",
"Between sunrise and sunset, travel a distance of exactly 3 stops by any form of transit.",
"If it rains before 6pm, stay outside for at least 7 minutes of it.",
"Call a number you have not dialled in over a year. Do not leave a message.",
"Spend 20 minutes in a building you have never entered.",
"Walk past the place you were going and continue for one more block before turning back.",
"Between now and sleep, eat something you have never eaten before.",
"Find something you lost. If you cannot, lose something on purpose.",
"At the next opportunity, take the stairs instead of the lift. Do not acknowledge this choice.",
"Introduce yourself to someone using a name that is not yours. Correct it after 3 minutes.",
"Before leaving any room today, check if you have left something behind. Leave nothing.",
"Walk with your hands clasped behind your back for the duration of one city block.",
"Next time you are asked how you are, give a factual response about the weather instead.",
"Before your next meal, name three things you can currently hear. Write them down.",
"Take a photograph of the floor in every room you enter for the next 2 hours.",
"Do not look at any screens for 41 minutes. If this is impossible, reduce it to 22.",
"At the next opportunity to sit, stand instead. Continue standing until someone comments.",
"Find a pay phone, a fountain, or an automatic door. Use it twice for no practical reason.",
"For the next hour, walk only on one side of the pavement.",
"At your next drink, leave exactly one sip remaining in the vessel.",
"Spend 14 minutes somewhere that is not your home, your workplace, or a shop.",
"The next item you purchase must be something you do not need.",
"Do not use your dominant hand for 30 minutes. Do not tell anyone why.",
"Read something posted publicly and memorise one sentence from it.",
"Before sleeping tonight, place your shoes somewhere unexpected.",
"Acknowledge the next piece of infrastructure you use. Do this silently.",
"Stand on a bridge or overpass for at least 5 minutes. Look in both directions.",
"Between now and the end of the day, change your direction of travel at least once for no reason.",
"Walk to a street corner and wait for the light to change twice before crossing.",
"Find a shop window. Study what is displayed for 3 minutes without entering.",
"Next time you are underground, count the stops silently.",
"Carry an umbrella even if no rain is forecast.",
"At your next meal, eat one item using utensils you would not normally use for it.",
"Walk to the edge of the nearest park and observe it from outside for 4 minutes.",
"For 90 minutes, respond to all questions with answers that are technically true but unhelpful.",
"Find a coin on the ground. If you do not, leave one.",
"Between now and midnight, visit three separate floors of the same building.",
"Say thank you to the next automated system you interact with.",
"Spend 6 minutes sitting somewhere that has a view of a door you did not enter through.",
"Next time you check the time, add 17 minutes to what you see and treat that as the real time.",
"For the next 2 hours, do not step on any metal grates in the pavement.",
"Walk to the nearest hospital, library, or church and stand outside it for 4 minutes.",
"The next time you board any form of transport, sit in a different position than usual.",
"Leave a folded note somewhere semi-public. The note may contain anything or nothing.",
"For the next 3 hours, keep at least one item from outside on your person at all times.",
"When you next eat with others, be the last to begin.",
"Take a photograph of something that will not exist tomorrow.",
"Walk to a place with a notable smell. Remain there for 8 minutes.",
"Before noon, be in at least two different buildings.",
"At your next convenience, buy the second cheapest item from a category you rarely buy from.",
"For the next hour, do not walk faster than the slowest person within your view.",
"Sit on a bench in a public space and do not use your phone for 15 minutes.",
"Between now and sleep, greet three strangers without explanation.",
"Find a piece of signage with a number on it. Subtract 4 from that number. Remember the result.",
"Spend 9 minutes doing something you have been avoiding.",
"Walk one full circuit around the block you are currently on before continuing.",
"Wait for the next vehicle to pass completely before you move.",
"For 40 minutes, only enter rooms through the least obvious entrance.",
"Before your next meal, wash your hands regardless of whether they need it.",
"Walk to a staircase and descend 3 flights, then ascend 2, then descend 1.",
"Spend your next idle moment standing rather than seated.",
"At the next shop you enter, buy exactly one item that is not on your list.",
"For one hour, do not answer any question immediately. Wait 7 seconds first.",
"Spend 18 minutes in a place where other people are engaged in something you are not part of.",
"Observe the next transaction between two other people without participating.",
"Find a room with a window that does not face the direction you arrived from.",
"Between now and 10pm, sleep for exactly 12 minutes if possible.",
"Before leaving your current location, note the colour of the nearest door.",
"Walk in the direction of the last sound you heard for one block.",
"For 25 minutes, only use staircases to change floors.",
"Buy or acquire something made of paper before the day ends.",
"The next time you are asked your opinion, give the opposite of what you think.",
"Spend 3 minutes watching something that is moving but making no sound.",
"At the next opportunity, allow someone else to go first.",
"Before your next conversation, decide how long it will be and end it at that point.",
"Walk to a vending machine. Do not buy anything. Leave.",
"Spend 7 minutes in a room you are not normally in.",
"The next time you feel comfortable, change something about your environment.",
"Find a reflection of yourself that is not a mirror.",
"Walk to a place where you can see more sky than building.",
"At your next meal, name every ingredient you can identify before eating.",
"For the next 2 hours, notice every door handle you touch.",
"Place something of yours at the exact geographic centre of whatever room you are in.",
"Before midnight, stand in the rain for at least one minute, or recall a time you were caught in it.",
"The next person who asks you for the time must be told the time in a different format."
};
const int CITY_COUNT = 100;





// =================================================

const char* hardCity[] = {
"For 24 hours, do not enter any building you have been in before. This includes your home.",
"Before the day ends, sleep somewhere you have never slept. This counts as a nap.",
"Spend 3 hours acting on the instructions of the first stranger who volunteers advice.",
"Write a set of 5 rules for yourself and follow all 5 for one full week.",
"Between now and sunrise, visit a location that is difficult to reach without assistance.",
"Find a person who does not know you and follow their routine for 47 minutes with their knowledge.",
"Eat every meal today at a different location. At least one must be somewhere unusual.",
"Do not use any form of digital communication for 6 hours. Begin now.",
"Travel to a street you have never been on and remain there for 22 minutes.",
"Before the week ends, acquire something from a stranger without purchasing it.",
"For 48 hours, do not sit in the same chair twice.",
"Spend one full hour doing only one thing, chosen before the hour begins, with no deviations.",
"Between now and midnight, travel to three locations you have never visited, in sequence.",
"Find a building that is currently open and spend 30 minutes on every floor.",
"Do not speak anyone's name for 12 hours. If necessary, gesture or wait.",
"Arrange to meet someone at a specific location without telling them why you chose it.",
"For the next 4 days, walk a different route to every destination.",
"Spend the afternoon in a library. Use it the way it is intended to be used.",
"Write down every place you enter for 8 hours and the time you entered.",
"Before the day ends, give away something you value to someone who was not expecting it.",
"For 2 hours, only communicate in writing. Carry paper for this purpose.",
"Travel somewhere that takes more than 90 minutes to reach. Do not plan what to do when you arrive.",
"Spend one hour acting as though you are being observed and evaluated by someone important.",
"Do not purchase anything for 48 hours. If you run out of something, go without.",
"Find a public place and remain in it for 3 hours without using your phone.",
"Before the week ends, do something that makes a stranger visibly curious.",
"For one full day, do not make any decisions about food. Accept whatever is offered.",
"Spend 90 minutes doing something that cannot be photographed or documented.",
"Find a stranger who is lost or confused. Assist them completely before continuing your day.",
"Between now and Friday, contact someone you have not spoken to in more than 2 years.",
"For 6 hours, do not use your phone for anything except phone calls.",
"Walk a distance of at least 8 kilometres before tomorrow. It does not need to be in one trip.",
"Spend one morning doing exactly what you did on the morning of a significant day in the past.",
"For the next 72 hours, keep a written log of every time you feel impatient.",
"Travel by a mode of transport you have not used in over a year.",
"Before the day ends, fix something that has been broken for more than a week.",
"Spend 2 hours somewhere uncomfortable without complaining or explaining.",
"Find three people who are strangers to each other and introduce them.",
"For one full day, do not use the word no. Find alternatives.",
"Eat nothing until 3pm. After 3pm, eat only things you prepared yourself.",
"Spend 4 hours somewhere that has no wi-fi and no mobile signal.",
"Before this week ends, attend something you were not invited to. Leave before it ends.",
"For 3 days, wake at the same time, eat at the same time, and sleep at the same time.",
"Write a letter to someone. Send it by post. Do not tell them to expect it.",
"Spend 45 minutes doing something you watched someone else do today.",
"Do not use your dominant hand for 4 hours. Do this on a day when you need your hands.",
"Find a route between two places in your city that avoids every street you normally use.",
"Spend one full hour in silence with another person. Do not explain to them in advance.",
"For 5 hours, travel only on foot. Plan the day accordingly.",
"Before the week ends, find 3 things you own that you have not touched in a year and remove them.",
"Spend a full afternoon without any agenda. Do not plan what you will do with the time.",
"For the next 3 days, eat nothing after 7pm.",
"Find a shop that has been open longer than you have been alive and spend money in it.",
"Travel to a part of your city you have never been to. Navigate without a map.",
"Spend one full evening without electric light after sunset.",
"For 48 hours, do not make plans with anyone. Accept all invitations you receive instead.",
"Find something broken in a public space and report it to the appropriate authority.",
"Spend 3 hours doing something that produces a physical result you can hold at the end.",
"Before the day ends, learn the name of someone you see regularly but have never spoken to.",
"For 6 hours, notice and name the material that everything around you is made of.",
"Spend an hour walking through the part of your city with the oldest buildings.",
"Find something in your possession that was made before 1980. Research its origin.",
"For 2 days, only drink water and tea. No exceptions after the first hour.",
"Find a public garden or green space. Spend 90 minutes there without checking your phone.",
"Before the week ends, repair something using only materials already in your home.",
"Spend 4 hours doing something that earns you nothing and benefits only someone else.",
"For one day, respond to every question with a question.",
"Travel to the edge of your city or neighbourhood. Observe the boundary. Return.",
"Spend 3 hours in a place where you do not speak the primary language.",
"Find a way to be useful to a stranger without them asking.",
"For 24 hours, keep track of how many steps you take.",
"Spend one hour in a place of worship that is not your own tradition. Observe only.",
"Before this week ends, start something you have been postponing for more than 3 months.",
"For 8 hours, do everything slightly slower than feels comfortable.",
"Find the oldest person in any room you enter today and ask them one question.",
"Spend 3 hours in a part of your city that tourists visit but you never do.",
"For 2 days, do not complain about anything, including the weather.",
"Find a staircase of at least 6 floors and walk it from bottom to top three times.",
"Spend one full day without using any word that contains the letter E.",
"Before midnight, revisit the last place you were unhappy and spend 10 minutes there.",
"For 5 hours, only use cash for any transactions.",
"Spend an afternoon with someone who is significantly older or younger than you.",
"Find a way to cross a body of water before the day ends.",
"For 48 hours, document every person who makes eye contact with you.",
"Spend 2 hours reading something published before you were born.",
"Before the week ends, cook and eat something using only 4 ingredients.",
"For one full day, begin every sentence with the word fortunately.",
"Find an animal you do not own and spend 10 minutes with it.",
"Spend 3 hours doing something that your parents did regularly but you do not.",
"For 24 hours, always face the direction you came from when you stop moving.",
"Find a place you can see from your home but have never walked to. Go there.",
"Spend one hour listening to a radio station you have never chosen.",
"For 3 days, keep a list of everything that surprises you.",
"Before the week ends, borrow something and return it the same day.",
"Spend 90 minutes somewhere that smells strongly of something. Identify what.",
"For 4 hours, always enter rooms last.",
"Find a piece of public art you have never noticed before. Spend 8 minutes with it.",
"Before this week ends, have a conversation lasting at least 30 minutes with no agenda."
};
const int HARD_CITY_COUNT = 100;





const char* rareCity[] = {
"FOLLOW THE PERSON WHO LOOKS LIKE THEY KNOW SOMETHING YOU DO NOT",
"FIND THE DOOR THAT DOES NOT MATCH ITS BUILDING AND STAND BEFORE IT",
"THERE IS A LIGHT ON IN A WINDOW THAT SHOULD BE EMPTY. LOCATE IT",
"SOMEONE NEARBY IS CARRYING SOMETHING THEY SHOULD NOT HAVE. OBSERVE ONLY",
"A SOUND DOES NOT BELONG TO THIS STREET. TRACE IT TO ITS ORIGIN",
"FIND THE PLACE WHERE THE CITY FORGOT TO FINISH ITSELF"
};
const int RARE_CITY_COUNT = 6;


// ─────────────────────────────────────────────────
// MENU 2: CON
// ─────────────────────────────────────────────────
const char* easyCon[] = {
"COMPLIMENT ONE COSPLAYER","TAKE A PHOTO OF SOMETHING YOU LIKE",
"FIND SOMEWHERE TO SIT AND REST","DRINK WATER","LOOK FOR ONE LED COSTUME",
"COUNT THREE DIFFERENT FANDOMS","FIND SOMEONE WEARING A CAPE",
"OBSERVE ONE PROP WITHOUT TOUCHING IT","FIND THE QUIETEST SPOT NEARBY",
"LOOK FOR AN ARTIST YOU HAVE NOT SEEN BEFORE",
"FIND SOMEONE WEARING EARS OR A TAIL","NOTICE THE COLOUR USED MOST TODAY",
"COUNT THE PEOPLE WEARING GLASSES","FIND SOMETHING HANDMADE",
"OBSERVE ONE COSTUME DETAIL UP CLOSE","FIND A STICKER YOU LIKE",
"COUNT THE VISIBLE SWORDS OR BLADES","NOTICE THE SMELL OF THE CONVENTION",
"FIND SOMETHING THAT GLOWS","OBSERVE ONE ARTIST WORKING",
"COUNT THE TABLES IN ONE SECTION","FIND SOMETHING PRINTED ON FABRIC",
"NOTICE THE LOUDEST SOUND IN THE ROOM","FIND SOMETHING SMALL AND DETAILED",
"COUNT THE PEOPLE IN FULL COSTUME","OBSERVE ONE QUEUE AND NOTE ITS LENGTH",
"FIND SOMETHING WITH WINGS","NOTICE ONE EMOTION VISIBLE ON A FACE",
"COUNT THE VISIBLE PLUSHIES","FIND SOMETHING MADE OF FOAM",
"NOTICE ONE BOOTH WITH NO CUSTOMERS","FIND SOMETHING THAT MATCHES A CHARACTER EXACTLY",
"COUNT THE VISIBLE WIGS","OBSERVE ONE PERSON TAKING A PHOTO",
"FIND SOMETHING THAT IS ONLY AVAILABLE HERE","NOTICE ONE SOUND THAT STANDS OUT",
"COUNT THE PEOPLE EATING OR DRINKING","FIND SOMETHING BLACK AND WHITE ONLY",
"OBSERVE ONE PERSON WHO LOOKS RELAXED","FIND SOMETHING WITH A LOGO OR SYMBOL",
"COUNT THE VISIBLE BAGS OR BACKPACKS","NOTICE THE TEMPERATURE IN THE ROOM",
"FIND SOMETHING WITH MORE THAN FIVE COLOURS","OBSERVE ONE PERSON WHO IS ALONE",
"COUNT THE VISIBLE CAMERAS OR PHONES TAKING PHOTOS","FIND SOMETHING THAT REPRESENTS A VILLAIN",
"NOTICE ONE PERSON HELPING ANOTHER","FIND SOMETHING THAT IS A REPLICA OF A REAL OBJECT",
"COUNT THE PEOPLE SITTING DOWN","OBSERVE ONE ARTIST'S DISPLAY ARRANGEMENT",
"FIND SOMETHING THAT SURPRISED YOU","NOTICE ONE DETAIL YOU ALMOST MISSED",
"COUNT THE VISIBLE CROWNS OR HEADPIECES","FIND SOMETHING REPRESENTING A CHARACTER OLDER THAN YOU",
"OBSERVE ONE TRANSACTION IN ARTIST ALLEY","FIND SOMETHING THAT BELONGS TO A DUO OR PAIR",
"COUNT THE DIFFERENT HAIRSTYLES VISIBLE","NOTICE ONE SOUND COMING FROM OUTSIDE THE ROOM",
"FIND SOMETHING THAT REQUIRED PATIENCE TO MAKE","OBSERVE ONE COSTUME FROM A DISTANCE",
"COUNT THE VISIBLE LANYARDS","FIND SOMETHING THAT IS A WORK IN PROGRESS",
"NOTICE ONE PERSON WHO CAME ALONE","FIND SOMETHING REPRESENTING AN ANTAGONIST",
"COUNT THE VISIBLE STALLS WITH QUEUES","OBSERVE ONE CHILD AT THE CONVENTION",
"FIND SOMETHING THAT IS CLEARLY VINTAGE","NOTICE ONE INTERACTION BETWEEN STRANGERS",
"COUNT THE VISIBLE ITEMS PRICED UNDER TEN","FIND SOMETHING THAT TELLS A STORY",
"OBSERVE ONE COSPLAYER IN CHARACTER","NOTICE THE MOST COMMON COLOUR TODAY",
"COUNT THE VISIBLE BOOTS","FIND SOMETHING THAT IS A LIMITED EDITION",
"NOTICE ONE AREA THAT IS LESS CROWDED","FIND SOMETHING THAT YOU WOULD WEAR",
"COUNT THE VISIBLE SASHES OR BELTS","OBSERVE ONE PERSON RESTING THEIR FEET",
"FIND SOMETHING THAT REPRESENTS A TRANSFORMATION","NOTICE ONE SIGN YOU HAVE NOT READ",
"COUNT THE VISIBLE MASKS","FIND SOMETHING THAT IS BOTH FRAGILE AND BEING CARRIED",
"OBSERVE ONE COSTUME THAT USES MULTIPLE MATERIALS","FIND SOMETHING WITH A PATTERN",
"COUNT THE VISIBLE NAME BADGES","NOTICE ONE THING THAT HAS BEEN PLACED CAREFULLY",
"FIND SOMETHING THAT REPRESENTS A JOURNEY","OBSERVE ONE GROUP PHOTO BEING TAKEN",
"COUNT THE VISIBLE CHAIRS OR SEATS","FIND SOMETHING THAT REQUIRES TWO HANDS TO CARRY",
"NOTICE ONE DETAIL THAT MAKES A COSTUME UNIQUE","FIND SOMETHING THAT CONNECTS TWO SERIES"
};
const int EASY_CON_COUNT = 100;


// PRESCRIPTS  -  CON (100)
// =================================================
const char* conPrescripts[] = {
"FIND A COSPLAYER WITH WINGS AND ASK FOR A PHOTO",
"LOCATE SOMEONE COSPLAYING A CHARACTER FROM A GAME YOU RECOGNIZE",
"COMPLIMENT THREE DIFFERENT COSPLAYERS",
"FIND A PROP WEAPON LARGER THAN YOUR ARM",
"ASK SOMEONE WHAT THEIR FAVORITE PANEL HAS BEEN",
"LOCATE A GROUP COSPLAY AND IDENTIFY THE SERIES",
"FIND A PERSON WEARING GLOWING ACCESSORIES",
"VISIT AN ARTIST ALLEY TABLE YOU WOULD NORMALLY SKIP",
"TAKE A BREAK AND DRINK WATER BEFORE CONTINUING",
"FIND A COSPLAYER WITH WHITE HAIR",
"COUNT FIVE DIFFERENT FANDOMS IN ONE ROOM",
"LOCATE A MASKED COSPLAYER",
"FIND THE LOUDEST AREA OF THE CONVENTION",
"ASK FOR A RECOMMENDATION FROM AN ARTIST",
"FIND SOMEONE COSPLAYING A VILLAIN",
"LOCATE A PROP SWORD",
"FIND SOMEONE COSPLAYING A NON HUMAN CHARACTER",
"VISIT A PANEL ROOM YOU HAVE NOT ENTERED YET",
"FIND A COSPLAYER WITH LED LIGHTS",
"TAKE A PHOTO OF SOMETHING IMPRESSIVE",
"FIND A COSPLAY WITH HANDMADE ARMOUR",
"LOCATE THE MOST TECHNICALLY COMPLEX COSTUME YOU CAN FIND",
"ASK AN ARTIST WHERE THEIR INSPIRATION CAME FROM",
"FIND SOMEONE WHOSE COSTUME INCLUDES PRACTICAL STORAGE",
"LOCATE A COSPLAY FROM A SERIES OLDER THAN TEN YEARS",
"FIND A COSPLAYER WHO MADE THEIR OWN PROPS",
"COUNT THE NUMBER OF VARIATIONS OF ONE CHARACTER YOU SEE",
"FIND A COUPLE OR PAIR COSPLAYING MATCHING CHARACTERS",
"ASK SOMEONE HOW LONG THEIR COSTUME TOOK TO MAKE",
"FIND A COSPLAY THAT INCLUDES A WORKING LIGHT OR SOUND EFFECT",
"LOCATE THE ARTIST WHOSE TABLE HAS THE LONGEST QUEUE",
"FIND A PRINT THAT MAKES YOU GENUINELY LAUGH",
"OBSERVE THE ENTRANCE FOR ONE MINUTE AND COUNT COSTUMES",
"FIND SOMEONE COSPLAYING A CHARACTER WHO IS RARELY REPRESENTED",
"LOCATE THE QUIETEST CORNER OF THE CONVENTION FLOOR",
"ASK SOMEONE TO EXPLAIN THEIR SERIES IN THIRTY SECONDS",
"FIND A COSPLAY WHERE EVERY DETAIL IS PERFECTLY ACCURATE",
"LOCATE A VENDOR SELLING SOMETHING YOU HAVE NEVER SEEN BEFORE",
"FIND A COSPLAYER WHO IS CLEARLY HAVING THE BEST DAY",
"IDENTIFY THE MOST POPULAR SERIES REPRESENTED RIGHT NOW",
"COUNT HOW MANY PEOPLE ARE WEARING GLASSES WITH THEIR COSTUME",
"FIND A STAFF MEMBER AND THANK THEM FOR THEIR WORK",
"LOCATE A COSPLAY THAT REFERENCES A SPECIFIC SCENE",
"FIND SOMEONE WHOSE COSTUME INCLUDES REAL FABRIC EMBROIDERY",
"ASK AN ARTIST TO SIGN THE PRINT YOU JUST BOUGHT",
"FIND A COSPLAY FROM A SERIES YOU WANT TO LEARN ABOUT",
"LOCATE THE OLDEST PERSON COSPLAYING AND ASK ABOUT THEIR CHARACTER",
"FIND THE YOUNGEST ARTIST IN ARTIST ALLEY AND BUY SOMETHING",
"COUNT THE FANDOMS REPRESENTED IN ONE CORRIDOR",
"FIND A COSPLAY YOU INITIALLY COULD NOT IDENTIFY",
"LOCATE A PROP THAT HAS BEEN WEATHERED TO LOOK USED",
"FIND SOMEONE WHO TRAVELLED THE FURTHEST TO ATTEND",
"IDENTIFY A SERIES YOU HAVE NEVER HEARD OF AND RESEARCH IT",
"FIND A COSPLAY WHERE THE SHOES MATCH PERFECTLY",
"LOCATE THE PERSON WHOSE COSTUME IS MOST PHYSICALLY DEMANDING",
"ASK SOMEONE WHAT THE HARDEST PART OF THEIR BUILD WAS",
"FIND A SILENT COSTUME THAT COMMUNICATES EVERYTHING CLEARLY",
"LOCATE THE TABLE WITH THE MOST VARIED SUBJECT MATTER",
"FIND A COSPLAY THAT REIMAGINES A CHARACTER IN A NEW STYLE",
"COUNT HOW MANY PEOPLE ARE USING THEIR PHONES AS PART OF THEIR COSTUME",
"FIND A CROSSOVER COSPLAY COMBINING TWO DIFFERENT SERIES",
"LOCATE A PRINT THAT DEPICTS A SCENE YOU KNOW BY HEART",
"FIND A COSPLAYER WHOSE MAKEUP TOOK MORE TIME THAN THEIR COSTUME",
"ASK SOMEONE TO POSE IN THEIR CHARACTER'S MOST ICONIC STANCE",
"FIND A COSPLAY WHERE THE ACCESSORIES ARE THE MAIN FEATURE",
"LOCATE THE AREA WHERE PHOTOGRAPHERS GATHER AND OBSERVE FOR ONE MINUTE",
"FIND A COSPLAY THAT REQUIRED LEARNING A NEW SKILL",
"IDENTIFY A TREND IN COSPLAY VISIBLE IN THE LAST HOUR",
"FIND SOMEONE COSPLAYING A CHARACTER THEY CLEARLY LOVE",
"LOCATE A HANDMADE ITEM THAT LOOKS MORE PROFESSIONAL THAN PURCHASED",
"FIND A COSPLAY REFERENCING SOMETHING FROM BEFORE YOU WERE BORN",
"ASK AN ARTIST WHAT PROJECT THEY ARE MOST PROUD OF",
"FIND A COSTUME ELEMENT MADE FROM AN UNEXPECTED MATERIAL",
"LOCATE THE TABLE THAT FEELS MOST LIKE AN ENTIRE WORLD",
"FIND SOMEONE WHOSE ENTIRE OUTFIT INCLUDING ACCESSORIES IS CONSISTENT",
"COUNT THE NUMBER OF DIFFERENT MEDIA TYPES REPRESENTED IN ONE SECTION",
"FIND A COSPLAY THAT WOULD BE IMPOSSIBLE WITHOUT STILTS OR PLATFORMS",
"LOCATE A CONVENTION EXCLUSIVE ITEM AND NOTE WHAT IT IS",
"FIND A COSPLAYER WHOSE CHARACTER IS FROM A CANCELLED SERIES",
"ASK SOMEONE WHAT COSPLAY THEY WANT TO DO NEXT",
"FIND A COSTUME THAT HAS CLEARLY EVOLVED OVER MULTIPLE CONVENTIONS",
"LOCATE THE PANEL THAT IS MOST UNLIKE ANY YOU HAVE ATTENDED",
"FIND A COSPLAY REPRESENTING A CHARACTER WHO DOES NOT SPEAK",
"IDENTIFY THE MOST UNUSUAL FABRIC OR MATERIAL IN USE TODAY",
"FIND A GROUP THAT REPRESENTS AN ENTIRE CAST",
"LOCATE A COSPLAY FROM A SERIES THAT ENDED MORE THAN FIFTEEN YEARS AGO",
"FIND SOMEONE WHOSE PROP IS LARGER THAN THEY ARE",
"ASK THREE ARTISTS WHAT INSPIRED THEM TO COME TO THIS CONVENTION",
"FIND A COSPLAY WHERE COLOUR ACCURACY IS PERFECT",
"LOCATE A PRINT THAT DEPICTS A VILLAIN SYMPATHETICALLY",
"FIND A COSTUME ELEMENT THAT MOVES WITH THE WEARER",
"IDENTIFY THE FASTEST WALKER IN A COSTUME TODAY",
"FIND A COSPLAY THAT SUBVERTS THE ORIGINAL CHARACTER DESIGN",
"LOCATE SOMEONE WHO HAS BEEN COSPLAYING FOR MORE THAN TEN YEARS",
"FIND A COSTUME THAT IS BOTH ACCURATE AND PRACTICAL",
"ASK AN ARTIST TO RECOMMEND ANOTHER ARTIST WHOSE WORK THEY LOVE",
"FIND A COSPLAY THAT YOU COULD NOT HAVE PREDICTED SOMEONE WOULD MAKE",
"LOCATE THE MOST RECENT SERIES REPRESENTED AT THE CONVENTION TODAY"
};
const int CON_COUNT = 100;

// =================================================

const char* hardCon[] = {
"APPROACH A GROUP COSPLAY AND IDENTIFY EVERY CHARACTER IN IT",
"ATTEND A PANEL ON A TOPIC YOU KNOW NOTHING ABOUT AND TAKE NOTES",
"FIND THREE COSPLAYERS FROM SERIES YOU NEVER HEARD OF AND ASK ABOUT THEM",
"PURCHASE ONE ITEM FROM AN ARTIST ALLEY TABLE YOU WOULD NEVER VISIT",
"COMPLETE A FULL LAP OF THE CONVENTION FLOOR WITHOUT YOUR PHONE",
"FIND THE MOST TECHNICALLY IMPRESSIVE PROP AND LEARN HOW IT WAS MADE",
"INTRODUCE YOURSELF TO ONE PERSON AND LEARN THEIR NAME AND FANDOM",
"LOCATE THE MOST OBSCURE FANDOM REPRESENTED AND IDENTIFY IT",
"DESCRIBE FROM MEMORY THREE COSPLAYS YOU SAW TODAY",
"FIND THE PERSON WHO HAS BEEN AT THE CONVENTION LONGEST AND ASK WHAT CHANGED",
"ATTEND AN ENTIRE PANEL AND WRITE THREE THINGS YOU LEARNED",
"FIND SOMEONE WHO HAS COSPLAYED THE SAME CHARACTER MORE THAN FIVE TIMES",
"IDENTIFY EVERY CHARACTER IN THE MOST COMPLEX GROUP COSPLAY YOU SEE",
"PURCHASE SOMETHING FROM AN ARTIST AND LEARN THE STORY BEHIND THE PIECE",
"FIND THE LONGEST RUNNING ARTIST AT THIS CONVENTION AND ASK ABOUT CHANGES",
"ATTEND A PANEL AND ASK A THOUGHTFUL QUESTION",
"IDENTIFY THREE COSPLAYS THAT REQUIRED A SKILL YOU DO NOT HAVE",
"FIND A SERIES WITH MORE THAN TEN REPRESENTATIVES AND DOCUMENT THEM",
"COMPLETE THE FULL ARTIST ALLEY WITHOUT USING YOUR PHONE",
"FIND THE PERSON WHOSE COSTUME REQUIRED THE MOST PLANNING",
"IDENTIFY EVERY TECHNIQUE VISIBLE IN ONE COMPLEX PROP",
"FIND THREE COSPLAYS THAT REPRESENT CHARACTERS FROM DIFFERENT DECADES",
"ATTEND A PANEL OUTSIDE YOUR INTEREST AREA AND FIND ONE POINT OF CONNECTION",
"DOCUMENT EVERY VARIATION OF ONE CHARACTER YOU SEE THROUGHOUT THE DAY",
"FIND A COSPLAYER WHO BUILT THEIR COSTUME ALONE AND LEARN HOW",
"IDENTIFY THE SINGLE MOST TECHNICALLY DIFFICULT ELEMENT VISIBLE TODAY",
"COMPLETE A FULL CIRCUIT OF THE DEALER ROOM WITHOUT BUYING ANYTHING",
"FIND THREE ARTISTS WHO WORK IN COMPLETELY DIFFERENT STYLES AND COMPARE",
"ATTEND THE MOST OBSCURE PANEL AND IDENTIFY ITS DEDICATED AUDIENCE",
"FIND THE MOST ACCURATE REPLICA OF A SPECIFIC SCENE PROP AND VERIFY DETAILS",
"COMPLETE A CONVERSATION WITH FIVE DIFFERENT STRANGERS ABOUT THEIR FANDOM",
"IDENTIFY THE ENTIRE CAST OF THE LARGEST GROUP COSPLAY",
"FIND EVIDENCE OF THREE DIFFERENT CONSTRUCTION METHODS IN ONE COSTUME",
"ATTEND A PANEL AND WRITE A ONE PARAGRAPH SUMMARY IMMEDIATELY AFTER",
"FIND THE COSPLAY THAT HAS BEEN WORN THE MOST TIMES AND LEARN ITS HISTORY",
"IDENTIFY ALL THE SERIES REFERENCED IN ONE SECTION OF ARTIST ALLEY",
"COMPLETE THREE HOURS WITHOUT CHECKING SOCIAL MEDIA",
"FIND A SERIES YOU KNOW NOTHING ABOUT AND LEARN ENOUGH TO IDENTIFY IT",
"ATTEND TWO CONSECUTIVE PANELS ON UNRELATED TOPICS AND FIND A CONNECTION",
"FIND THE COSPLAY THAT TOOK THE MOST RESEARCH TO MAKE ACCURATELY",
"DOCUMENT THE COMPLETE EVOLUTION OF ONE ARTIST'S STYLE AT THEIR TABLE",
"FIND THREE COSPLAYS REPRESENTING DIFFERENT FORMS OF THE SAME CHARACTER",
"ATTEND A WORKSHOP OR INSTRUCTIONAL PANEL AND APPLY ONE THING LEARNED",
"IDENTIFY THE MOST UNDERREPRESENTED SERIES AND FIND ITS FANS",
"COMPLETE A CONVERSATION WITH AN ARTIST ABOUT THEIR CREATIVE PROCESS",
"FIND THE COSTUME THAT WOULD BE IMPOSSIBLE WITHOUT COLLABORATION",
"IDENTIFY EVERY FANDOM VISIBLE FROM ONE FIXED POINT FOR FIVE MINUTES",
"COMPLETE A FULL TRAVERSAL OF THE CONVENTION TAKING NOTES ON OBSERVATIONS",
"FIND THE THREE MOST HISTORICALLY ACCURATE COSTUMES AND VERIFY DETAILS",
"DOCUMENT THE RANGE OF INTERPRETATIONS OF ONE CHARACTER ACROSS THE FLOOR",
"ATTEND A PANEL WHERE YOU ARE THE ONLY PERSON FROM OUTSIDE THAT FANDOM",
"FIND THE COSTUME THAT DEMONSTRATES THE MOST MASTERY OF A SINGLE TECHNIQUE",
"COMPLETE A CONVERSATION WITH SOMEONE ABOUT WHY THEY CHOSE THAT CHARACTER",
"IDENTIFY THE THREE CHARACTERS WHO APPEAR MOST DIFFERENTLY IN EACH COSPLAY",
"FIND EVERY COSPLAY FROM ONE SPECIFIC SERIES AND NOTE THE DIFFERENCES",
"ATTEND A PANEL ON A CRAFT OR TECHNIQUE AND ASK A TECHNICAL QUESTION",
"FIND THE COSTUME THAT MOST CHANGES HOW THE WEARER MOVES",
"COMPLETE A FULL REVIEW OF ARTIST ALLEY AND IDENTIFY THE BEST VALUE",
"FIND THREE COSPLAYS THAT TOGETHER TELL THE STORY OF A SERIES",
"IDENTIFY THE CONSTRUCTION METHODS IN THE MOST COMPLEX COSTUME YOU SEE",
"ATTEND A PANEL AND CONNECT WHAT YOU HEAR TO SOMETHING OUTSIDE THE FANDOM",
"FIND THE COSPLAY WHERE THE MAKER HAS MOST CLEARLY EXCEEDED THEIR OWN LIMITS",
"COMPLETE A CONVERSATION ABOUT A SERIES ENTIRELY IN TERMS OF ITS THEMES",
"IDENTIFY THE RAREST FANDOM REPRESENTED AND FIND MULTIPLE EXAMPLES",
"FIND THE COSTUME THAT MOST ACCURATELY REPRESENTS A SPECIFIC STORY MOMENT",
"DOCUMENT THREE DIFFERENT APPROACHES TO THE SAME CHARACTER CHALLENGE",
"ATTEND AN EVENT AND DESCRIBE IT AFTERWARDS TO SOMEONE WHO WAS NOT THERE",
"FIND THE COSPLAY THAT MOST CHANGED IN THE TRANSITION FROM MEDIA TO REALITY",
"COMPLETE A CIRCUIT OF THE FLOOR CATEGORISING EVERYTHING BY MEDIUM",
"IDENTIFY THE THREE DECISIONS IN A COSTUME THAT MAKE IT RECOGNISABLE",
"FIND THE ARTIST WHOSE WORK BEST CAPTURES THE ESSENCE OF THEIR SUBJECT",
"ATTEND A PANEL AND IDENTIFY ONE ASSUMPTION THE AUDIENCE SHARES",
"FIND THE COSPLAY THAT REQUIRED THE MOST PROBLEM SOLVING",
"COMPLETE A CONVERSATION WITH THREE PEOPLE FROM DIFFERENT GENERATIONS OF FANDOM",
"IDENTIFY THE COSTUME THAT MOST SUCCESSFULLY INTERPRETS A VAGUE DESIGN",
"FIND THE THREE ARTISTS WHOSE WORK MOST CHALLENGES YOUR OWN TASTES",
"ATTEND A PANEL AND FIND ONE THING THAT APPLIES TO SOMETHING ELSE ENTIRELY",
"FIND THE COSPLAY THAT BEST DEMONSTRATES KNOWLEDGE OF SOURCE MATERIAL",
"COMPLETE A DESCRIPTION OF YOUR DAY SO FAR IN DETAIL FROM MEMORY",
"IDENTIFY EVERY CRAFT TECHNIQUE VISIBLE IN ONE SQUARE METRE OF ARTIST ALLEY",
"FIND THE COSTUME THAT MOST DEPENDS ON THE WEARER'S PERFORMANCE",
"ATTEND A PANEL OUTSIDE YOUR COMFORT ZONE AND FIND ITS VALUE",
"FIND THE COSPLAY THAT MOST SUCCESSFULLY SOLVES AN APPARENTLY IMPOSSIBLE PROBLEM",
"COMPLETE A CONVERSATION ABOUT THE HISTORY OF THE SERIES WITH SOMEONE",
"IDENTIFY THE SINGLE ELEMENT THAT DEFINES THE MOST SUCCESSFUL COSTUMES TODAY",
"FIND THREE COSPLAYS THAT ILLUSTRATE HOW INTERPRETATIONS OF ONE CHARACTER EVOLVE",
"ATTEND A PANEL AND CONNECT IT TO SOMETHING FROM YOUR OWN EXPERIENCE",
"FIND THE COSPLAY THAT BEST REPRESENTS THE SPIRIT RATHER THAN THE LETTER OF A DESIGN",
"COMPLETE A SYSTEMATIC ANALYSIS OF ONE AREA OF THE CONVENTION FLOOR",
"IDENTIFY THE COSTUME THAT MOST CLEARLY REPRESENTS A TURNING POINT IN ITS FANDOM",
"FIND THE THREE THINGS UNIQUE TO THIS CONVENTION THAT WOULD NOT EXIST ELSEWHERE",
"ATTEND AN ENTIRE EVENT FROM START TO FINISH AND WRITE A BRIEF REVIEW",
"FIND THE COSPLAY THAT MOST SUCCESSFULLY INTEGRATES MULTIPLE CHALLENGES",
"COMPLETE A CONVERSATION ABOUT THE CRAFT OF COSPLAY WITH SOMEONE WHO HAS NEVER DONE IT",
"IDENTIFY THE PATTERN IN WHAT MAKES THE MOST SUCCESSFUL COSTUMES TODAY",
"FIND THE COSPLAY THAT SHOWS THE MOST GROWTH FROM A MAKER'S PREVIOUS WORK",
"ATTEND A PANEL AND ARTICULATE AFTERWARDS WHAT THE AUDIENCE IS REALLY LOOKING FOR",
"FIND THE THREE THINGS THIS CONVENTION DOES THAT NO OTHER SPACE DOES",
"COMPLETE YOUR ATTENDANCE WITH A WRITTEN ACCOUNT OF THE THREE MOST SIGNIFICANT THINGS YOU SAW"
};
const int HARD_CON_COUNT = 100;


const char* rareCon[] = {
"FIND THE COSPLAYER WHO DOES NOT BELONG TO ANY FANDOM YOU RECOGNIZE",
"LOCATE THE BOOTH THAT SHOULD NOT EXIST ON THE FLOOR PLAN",
"SOMEONE IS WEARING A COSTUME THAT DOES NOT MATCH ANY SOURCE MATERIAL. DOCUMENT IT",
"FIND THE ONE PERSON IN THE CROWD WHO IS COMPLETELY STILL",
"A PROP IN THIS VENUE IS NOT A PROP. YOU WILL KNOW IT WHEN YOU SEE IT",
"IDENTIFY THE EXIT THAT WAS NOT THERE AN HOUR AGO"
};
const int RARE_CON_COUNT = 6;


// ─────────────────────────────────────────────────
// MENU 3: TASK
// ─────────────────────────────────────────────────
const char* easyTask[] = {
"CLOSE ONE APP YOU ARE NOT USING","RESPOND TO ONE MESSAGE",
"WRITE ONE ITEM ON YOUR TO DO LIST","DRINK WATER BEFORE CONTINUING",
"PUT YOUR PHONE FACE DOWN FOR ONE MINUTE","STAND AND STRETCH ONCE",
"DELETE ONE FILE","CHECK THE TIME WITHOUT YOUR PHONE",
"SILENCE ONE NOTIFICATION","TAKE THREE DEEP BREATHS",
"CLOSE ONE TAB","MOVE ONE ITEM TO WHERE IT BELONGS",
"WRITE THE NEXT THING YOU NEED TO DO","OPEN A WINDOW",
"SAVE WHAT YOU ARE WORKING ON","DRINK HALF A GLASS OF WATER",
"SILENCE YOUR PHONE FOR TWO MINUTES","UNCLENCH YOUR HANDS",
"CLOSE ONE BACKGROUND APPLICATION","WRITE ONE WORD ABOUT YOUR GOAL",
"PUT ONE THING AWAY","BLINK SLOWLY AND REFOCUS",
"WRITE THE DATE AT THE TOP OF YOUR NOTES","TURN DOWN YOUR VOLUME",
"REMOVE ONE THING FROM YOUR DESK","STRETCH YOUR NECK GENTLY",
"WRITE DOWN ONE THING DISTRACTING YOU","CLEAR YOUR CLIPBOARD",
"CLOSE ONE SOCIAL MEDIA TAB","SAVE A DRAFT OF WHAT YOU ARE WRITING",
"WRITE YOUR NAME AND CURRENT TASK","TAKE ONE MINDFUL BREATH",
"LOOK AWAY FROM THE SCREEN FOR TWENTY SECONDS","DELETE ONE APP NOTIFICATION",
"TIDY ONE CABLE ON YOUR DESK","WRITE THE SMALLEST NEXT STEP",
"PUT YOUR PHONE IN ANOTHER POCKET OR BAG","CLOSE AN EMPTY WINDOW",
"NAME ONE THING YOU HAVE FINISHED TODAY","WRITE TOMORROW'S FIRST TASK",
"LOWER YOUR SCREEN BRIGHTNESS SLIGHTLY","MOVE ONE ITEM INTO A FOLDER",
"CLOSE A FILE YOU NO LONGER NEED OPEN","WRITE ONE WORD THAT DESCRIBES YOUR STATE",
"TAKE THE RUBBISH AWAY FROM YOUR DESK","CHECK YOUR NEXT CALENDAR ITEM",
"WRITE THE TIME YOU WANT TO FINISH","CONFIRM YOUR WORK IS BACKED UP",
"CLOSE ONE NOTIFICATION PANEL","MOVE SOMETHING OUT OF YOUR LINE OF SIGHT",
"WRITE A SINGLE SENTENCE ABOUT WHAT YOU ARE DOING","RELAX YOUR SHOULDERS",
"DELETE ONE DUPLICATE FILE","CLOSE YOUR BROWSER HISTORY TAB",
"WRITE ONE THING THAT WENT WELL","PUT SOMETHING IN ITS CASE",
"TAKE THE HEADPHONES OUT FOR ONE MINUTE","CLOSE A GROUP CHAT YOU DO NOT NEED",
"WRITE DOWN ONE THING YOU WANT TO REMEMBER","MOVE A WINDOW TO A BETTER POSITION",
"SILENCE ONE ALERT OR SOUND","IDENTIFY THE NEXT PHYSICAL ACTION FOR YOUR TASK",
"WRITE THE ONE THING THAT WOULD MOST HELP RIGHT NOW","CLEAR YOUR DESKTOP OF ONE ICON",
"CLOSE ONE TAB YOU OPENED BY HABIT","WRITE THE DATE AND STOP TO READ IT",
"TAKE ONE SIP OF SOMETHING","MOVE ONE THING FROM OPEN TO SAVED",
"WRITE DOWN WHAT TIME IT IS AND WHEN YOUR NEXT BREAK IS",
"CLOSE ONE WINDOW THAT HAS BEEN OPEN ALL DAY","NOTICE IF YOU ARE HOLDING TENSION",
"WRITE THE NAME OF YOUR CURRENT PROJECT","PUT SOMETHING IN THE BIN",
"TAKE ONE BREATH BEFORE YOUR NEXT KEYSTROKE","CLEAR ONE LINE OF NOTIFICATIONS",
"WRITE DOWN THE LAST THING YOU COMPLETED","MOVE ONE ITEM TO DONE",
"SILENCE ALL SOUNDS FOR TWO MINUTES","NAME ONE SMALL WIN FROM TODAY",
"WRITE THE SIMPLEST POSSIBLE DESCRIPTION OF YOUR CURRENT TASK",
"CLOSE ONE FILE YOU SAVED SOMEWHERE TEMPORARY","NOTICE YOUR POSTURE AND ADJUST",
"WRITE ONE QUESTION YOU NEED THE ANSWER TO","REMOVE ONE THING FROM YOUR SCREEN",
"TAKE THIRTY SECONDS AWAY FROM THE KEYBOARD","CLOSE ONE THING COMPLETELY",
"WRITE A BRIEF HEADING FOR WHAT YOU ARE WORKING ON",
"MOVE ONE ITEM FROM YOUR INBOX TO A FOLDER","PUT YOUR FEET FLAT ON THE FLOOR",
"WRITE DOWN THE NEXT DECISION YOU NEED TO MAKE","CLOSE ONE TAB YOU FORGOT WAS OPEN"
};
const int EASY_TASK_COUNT = 100;

// =================================================

// PRESCRIPTS  -  TASK (100)
// =================================================
const char* taskPrescripts[] = {
"GO OUTSIDE FOR FIVE MINUTES",
"DRINK WATER BEFORE USING YOUR PHONE AGAIN",
"CLEAN ONE SMALL AREA COMPLETELY",
"WRITE DOWN THREE THINGS YOU NEED TO DO TODAY",
"STRETCH YOUR SHOULDERS AND BACK",
"DELETE FIVE UNNECESSARY FILES OR PHOTOS",
"RESPOND TO ONE MESSAGE YOU HAVE BEEN AVOIDING",
"ORGANIZE YOUR DESK FOR TWO MINUTES",
"TAKE TEN DEEP BREATHS WITHOUT DISTRACTION",
"READ ONE PAGE OF SOMETHING EDUCATIONAL",
"PUT AWAY ONE ITEM THAT IS OUT OF PLACE",
"STEP AWAY FROM SCREENS FOR THREE MINUTES",
"COMPLETE ONE TASK THAT TAKES LESS THAN TWO MINUTES",
"CHECK YOUR POSTURE RIGHT NOW",
"WRITE A SHORT NOTE ABOUT YOUR CURRENT GOAL",
"OPEN A WINDOW",
"EMPTY ONE SECTION OF YOUR TRASH",
"WALK AROUND YOUR ROOM ONCE",
"STOP MULTITASKING FOR FIVE MINUTES",
"WRITE DOWN ONE THING YOU ARE AVOIDING",
"CLOSE EVERY APPLICATION RUNNING IN THE BACKGROUND",
"CHARGE ONE DEVICE THAT IS BELOW FIFTY PERCENT",
"REPLY TO THE OLDEST UNREAD MESSAGE IN YOUR INBOX",
"WIPE DOWN ONE SURFACE WITH A CLOTH",
"LABEL ONE FOLDER OR FILE THAT HAS A VAGUE NAME",
"DELETE ALL APPS FROM YOUR HOME SCREEN THAT YOU HAVE NOT OPENED THIS WEEK",
"WRITE A SINGLE SENTENCE ABOUT WHAT YOU ACCOMPLISHED TODAY",
"MOVE YOUR BODY  -  STRETCH OR WALK FOR THREE MINUTES",
"WRITE DOWN THREE THINGS THAT COULD GO BETTER TOMORROW",
"REVIEW YOUR CALENDAR FOR THE NEXT THREE DAYS",
"DRINK A FULL GLASS OF WATER BEFORE YOUR NEXT TASK",
"CLEAR YOUR DOWNLOAD FOLDER OF ANYTHING OLDER THAN A MONTH",
"BACK UP ONE FILE OR FOLDER YOU WOULD MISS IF LOST",
"SET A FIFTEEN MINUTE TIMER AND DO ONLY ONE THING",
"UNSUBSCRIBE FROM ONE EMAIL LIST THAT ADDS NO VALUE",
"WRITE THE THREE TASKS THAT HAVE THE HIGHEST STAKES TODAY",
"STAND UP AND ROLL YOUR SHOULDERS TEN TIMES",
"CLEAN YOUR KEYBOARD OR MOUSE",
"PUT EVERYTHING ON YOUR DESK IN ITS CORRECT PLACE",
"WRITE A TWO SENTENCE SUMMARY OF YOUR CURRENT PROJECT",
"TURN YOUR PHONE FACE DOWN AND WORK FOR TEN MINUTES",
"IDENTIFY ONE RECURRING PROBLEM AND NAME ITS ROOT CAUSE",
"SAVE AND CLOSE EVERY FILE YOU ARE NOT ACTIVELY EDITING",
"MAKE A LIST OF EVERYTHING UNFINISHED AND RANK IT BY IMPORTANCE",
"DO FIVE MINUTES OF EXERCISE BEFORE CONTINUING",
"REVIEW THE LAST THING YOU SAVED AND DECIDE IF IT IS STILL USEFUL",
"CLEAR YOUR NOTIFICATIONS COMPLETELY BEFORE CHECKING THEM",
"WRITE DOWN THE ONE THING THAT WOULD MAKE TODAY WORTHWHILE",
"FOLD OR PUT AWAY ONE PIECE OF CLOTHING THAT IS OUT OF PLACE",
"FIND THE LOUDEST DISTRACTION IN YOUR SPACE AND REDUCE IT",
"SPEND FIVE MINUTES READING SOMETHING WITH NO PRACTICAL PURPOSE",
"LOOK AWAY FROM ALL SCREENS FOR TWO FULL MINUTES",
"MAKE YOUR NEXT BREAK SCREEN FREE",
"WRITE DOWN THE LAST THREE THINGS THAT INTERRUPTED YOU",
"CLOSE ONE SUBSCRIPTION OR SERVICE YOU ARE NOT USING",
"SORT YOUR MOST USED FOLDER BY DATE AND DELETE OLD ITEMS",
"READ BACK ONE PARAGRAPH OF YOUR RECENT WORK AND IMPROVE IT",
"STAND OUTSIDE FOR TWO MINUTES WITHOUT DOING ANYTHING ELSE",
"IDENTIFY THE TASK THAT WOULD REDUCE YOUR STRESS THE MOST AND START IT",
"PUT YOUR PHONE IN A DIFFERENT ROOM FOR THE NEXT BLOCK",
"CLEAR YOUR DESKTOP OF ALL ICONS EXCEPT THE THREE YOU USE MOST",
"WRITE A BRIEF LOG ENTRY FOR TODAY",
"DO ONE THING TO MAKE TOMORROW MORNING EASIER",
"REVIEW YOUR SPENDING FROM THE LAST WEEK WITHOUT JUDGMENT",
"REMOVE THREE THINGS FROM YOUR ENVIRONMENT THAT CAUSE FRICTION",
"WRITE THE QUESTION YOU MOST NEED TO ANSWER TO MOVE FORWARD",
"OPEN YOUR MOST IGNORED PROJECT AND ADD ONE LINE",
"REORGANIZE YOUR BOOKMARKS OR SAVED ITEMS",
"TAKE A GLASS OF WATER TO WHEREVER YOU WILL WORK NEXT",
"NAME THE HABIT THAT COSTS YOU THE MOST EVERY DAY",
"CLEAN ONE ITEM THAT YOU USE EVERY DAY",
"FIND AND FIX ONE SMALL BROKEN THING IN YOUR ENVIRONMENT",
"WRITE ONE PARAGRAPH ABOUT WHERE YOU WANT TO BE IN THREE MONTHS",
"ARCHIVE FIVE ITEMS FROM YOUR INBOX THAT REQUIRE NO ACTION",
"MOVE YOUR MOST IMPORTANT TASK TO THE FIRST THING TOMORROW",
"WRITE A LIST OF WHAT DISTRACTED YOU TODAY",
"SPEND FIVE MINUTES ON SOMETHING CREATIVE WITH NO GOAL",
"CLEAR YOUR RECENT FILES LIST",
"PUT SOMETHING BACK WHERE IT BELONGS RIGHT NOW",
"WRITE DOWN THREE DECISIONS YOU HAVE BEEN DEFERRING",
"REVIEW AND UPDATE YOUR PASSWORD FOR ONE IMPORTANT ACCOUNT",
"SEND ONE THANK YOU MESSAGE TO SOMEONE WHO DESERVES IT",
"IDENTIFY THE MOST TIME CONSUMING PART OF YOUR DAY AND QUESTION IT",
"COMPLETE THE SMALLEST OUTSTANDING TASK ON YOUR LIST",
"READ THE NOTES FROM YOUR LAST MEETING OR SESSION",
"TURN OFF ONE FEATURE OR SETTING YOU ENABLED AND NEVER USED",
"WRITE DOWN WHAT YOU NEED TO STOP DOING AS MUCH AS WHAT TO START",
"MOVE ONE OBJECT TO WHERE YOU WILL NEED IT TOMORROW",
"TAKE FIVE MINUTES TO DO ABSOLUTELY NOTHING INTENTIONALLY",
"WRITE ONE HONEST SENTENCE ABOUT HOW YOUR DAY IS GOING",
"SORT YOUR MOST USED APPLICATION BY WHAT YOU ACTUALLY NEED",
"FIND THE TASK THAT IS OVERDUE AND DO THE FIRST STEP NOW",
"CLEAN THE SCREEN OF THE DEVICE YOU USE MOST",
"WRITE A LIST OF EVERYTHING YOU ARE CARRYING IN YOUR HEAD",
"IDENTIFY THE EARLIEST YOU COULD FINISH YOUR MOST IMPORTANT TASK",
"REVIEW ONE RESOURCE YOU SAVED AND NEVER READ",
"DO ONE SMALL THING FOR SOMEONE ELSE TODAY",
"WRITE DOWN WHAT YOU WANT TO FEEL AT THE END OF TODAY",
"CLEAR YOUR WORKSPACE COMPLETELY AND REBUILD IT WITH ONLY WHAT YOU NEED"
};
const int TASK_COUNT = 100;

// =================================================

const char* hardTask[] = {
"COMPLETE THE SINGLE HARDEST ITEM ON YOUR TASK LIST TODAY",
"AUDIT YOUR ENTIRE TO DO LIST AND ELIMINATE ANYTHING NON ESSENTIAL",
"WORK FOR FORTY FIVE MINUTES WITHOUT TOUCHING YOUR PHONE",
"WRITE A CLEAR PLAN FOR THE NEXT WEEK WITH DAILY GOALS",
"IDENTIFY THE THREE TASKS WITH THE MOST IMPACT AND DO ONE NOW",
"SET UP AN AUTOMATIC SYSTEM FOR ONE THING YOU DO MANUALLY",
"CLEAR YOUR INBOX TO ZERO BEFORE CONTINUING ANY OTHER TASK",
"FINISH ONE PROJECT MORE THAN HALFWAY DONE FOR OVER A WEEK",
"BLOCK OUT FOUR HOURS OF FOCUSED WORK TIME AND PROTECT IT",
"WRITE DOWN EVERY INCOMPLETE COMMITMENT AND ASSIGN EACH A DATE",
"COMPLETE THE SINGLE TASK WITH THE HIGHEST CONSEQUENCE IF UNDONE",
"WRITE A COMPLETE AND HONEST ACCOUNT OF HOW YOU SPENT TODAY",
"IDENTIFY EVERYTHING BLOCKING YOUR MOST IMPORTANT WORK AND REMOVE ONE",
"WORK ON ONE PROBLEM CONTINUOUSLY FOR NINETY MINUTES",
"WRITE A DETAILED SPECIFICATION FOR WHAT DONE MEANS FOR YOUR MAIN PROJECT",
"COMPLETE EVERY OUTSTANDING REPLY BEFORE OPENING ANYTHING NEW",
"IDENTIFY YOUR THREE HIGHEST VALUE ACTIVITIES AND DO ONLY THOSE TODAY",
"WRITE A COMPLETE PLAN FOR REACHING YOUR NEXT MILESTONE",
"CLEAR EVERY TEMPORARY STORAGE LOCATION YOU USE",
"COMPLETE THE TASK THAT IS MOST OVERDUE",
"WORK FOR TWO HOURS WITH ALL COMMUNICATIONS CLOSED",
"WRITE AN HONEST POST MORTEM ON WHY YOUR LAST TASK TOOK AS LONG AS IT DID",
"IDENTIFY THE SINGLE CHANGE THAT WOULD MOST IMPROVE YOUR PRODUCTIVITY",
"COMPLETE EVERYTHING ON TODAY'S LIST BEFORE STARTING TOMORROW'S",
"WRITE A COMPLETE PROCESS DOCUMENT FOR ONE RECURRING TASK",
"IDENTIFY AND CANCEL EVERY COMMITMENT YOU CANNOT HONOUR",
"WORK ON YOUR MOST COGNITIVELY DEMANDING TASK AT PEAK FOCUS",
"WRITE THE ARGUMENT AGAINST YOUR CURRENT APPROACH AND RESPOND TO IT",
"COMPLETE THE ADMINISTRATIVE BACKLOG THAT HAS BEEN ACCUMULATING",
"IDENTIFY THE RECURRING PROBLEM IN YOUR WORKFLOW AND SOLVE IT PERMANENTLY",
"WORK FOR THREE HOURS ON ONE TASK WITHOUT CONTEXT SWITCHING",
"WRITE A COMPLETE BRIEFING ON YOUR CURRENT PROJECT FOR SOMEONE WHO KNOWS NOTHING",
"IDENTIFY EVERY TASK THAT ONLY YOU CAN DO AND PRIORITISE THEM",
"COMPLETE THE TASK THAT OTHERS ARE WAITING ON",
"WRITE A COMPLETE RISK ASSESSMENT FOR YOUR CURRENT PROJECT",
"IDENTIFY THE MOST EXPENSIVE HABIT IN YOUR WORKFLOW AND CHANGE IT",
"WORK UNTIL YOUR MOST IMPORTANT TASK IS IN A SHIPPABLE STATE",
"WRITE A COMPLETE DESCRIPTION OF THE PROBLEM YOU ARE ACTUALLY TRYING TO SOLVE",
"COMPLETE EVERY TASK THAT REQUIRES LESS THAN FIVE MINUTES",
"IDENTIFY THE THREE DECISIONS MOST AFFECTING YOUR CURRENT WORK",
"WORK ON THE TASK YOU MOST RESIST FOR ONE HOUR",
"WRITE A COMPLETE ACCOUNT OF EVERY OPEN COMMITMENT",
"COMPLETE YOUR MOST IMPORTANT DELIVERABLE FOR THIS WEEK",
"IDENTIFY WHERE YOUR TIME ACTUALLY GOES AND CHANGE ONE THING",
"WORK THROUGH THE HARDEST PART OF YOUR CURRENT PROBLEM",
"WRITE A COMPLETE ANALYSIS OF WHAT YOU NEED TO SUCCEED",
"COMPLETE THE REVIEW OR QUALITY CHECK YOU HAVE BEEN AVOIDING",
"IDENTIFY THE SINGLE MOST IMPORTANT METRIC FOR YOUR CURRENT WORK",
"WORK ON DOCUMENTATION FOR SOMETHING OTHERS DEPEND ON",
"WRITE THE COMPLETE NEXT DRAFT OF YOUR MOST IMPORTANT DOCUMENT",
"COMPLETE THE TASK THAT UNLOCKS THE MOST OTHER WORK",
"IDENTIFY EVERY TASK YOU HAVE DELEGATED AND CHECK ON EACH ONE",
"WORK FOR AN HOUR ON SOMETHING THAT MATTERS IN SIX MONTHS",
"WRITE A COMPLETE PLAN FOR WHAT YOU WILL ACCOMPLISH BEFORE NEXT WEEK",
"COMPLETE THE SINGLE ITEM WITH THE NEAREST DEADLINE",
"IDENTIFY EVERY MEETING ON YOUR CALENDAR AND REMOVE AT LEAST ONE",
"WORK ON THE PROBLEM YOU HAVE BEEN ROUTING AROUND",
"WRITE A COMPLETE LIST OF EVERY DECISION PENDING IN YOUR WORK",
"COMPLETE THE TASK THAT WOULD MOST REDUCE YOUR ANXIETY",
"IDENTIFY THE DIFFERENCE BETWEEN WHAT YOU PLANNED AND WHAT YOU DID",
"WORK FOR NINETY MINUTES ON YOUR HIGHEST PRIORITY WITHOUT STOPPING",
"WRITE A COMPLETE ACCOUNT OF WHAT NEEDS TO HAPPEN NEXT WEEK",
"COMPLETE EVERYTHING OUTSTANDING BEFORE YOUR NEXT PLANNED BREAK",
"IDENTIFY THE TASK THAT HAS BEEN WAITING LONGEST AND FINISH IT",
"WORK ON THE PART OF YOUR PROJECT THAT WILL AFFECT EVERYTHING ELSE",
"WRITE THE CLEAREST POSSIBLE STATEMENT OF YOUR CURRENT CONSTRAINT",
"COMPLETE THE TASK THAT REQUIRES THE SKILL YOU HAVE BEEN AVOIDING",
"IDENTIFY EVERY SOURCE OF TECHNICAL DEBT IN YOUR WORK",
"WORK ON A PROBLEM YOU DO NOT YET KNOW HOW TO SOLVE",
"WRITE A COMPLETE COMPARISON OF YOUR OPTIONS BEFORE DECIDING",
"COMPLETE THE FULL OUTLINE OF YOUR MOST IMPORTANT UPCOMING WORK",
"IDENTIFY AND RESOLVE THE AMBIGUITY THAT IS SLOWING EVERYTHING DOWN",
"WORK UNTIL YOU HAVE MADE IRREVERSIBLE PROGRESS ON YOUR MAIN PROJECT",
"WRITE A COMPLETE DESCRIPTION OF WHAT THE NEXT NINETY DAYS SHOULD LOOK LIKE",
"COMPLETE THE TASK THAT WILL MOST IMPRESS THE PERSON WHO MATTERS MOST",
"IDENTIFY EVERY THING YOU ARE DOING THAT IS NOT MOVING YOUR WORK FORWARD",
"WORK FOR TWO HOURS ON SOMETHING THAT WILL STILL MATTER NEXT YEAR",
"WRITE A COMPLETE REVIEW OF THE LAST MONTH OF WORK",
"COMPLETE THE HIGHEST STAKES ITEM BEFORE DOING ANYTHING LOW STAKES",
"IDENTIFY THE MOST IMPORTANT QUESTION YOUR WORK HAS NOT ANSWERED",
"WORK UNTIL YOUR NEXT CHECKPOINT IS READY FOR REVIEW",
"WRITE A COMPLETE BRIEF FOR THE WORK YOU WANT TO DO TOMORROW",
"COMPLETE THE TASK THAT REQUIRES THE MOST SUSTAINED ATTENTION",
"IDENTIFY THE THREE THINGS YOU NEED TO ACCEPT TO MAKE PROGRESS",
"WORK FOR ONE HOUR ON THE PROBLEM ONLY YOU CAN SOLVE",
"WRITE A COMPLETE ANSWER TO THE QUESTION BLOCKING YOUR PROGRESS",
"COMPLETE THE SINGLE MOST TECHNICALLY DIFFICULT OUTSTANDING ITEM",
"IDENTIFY WHAT YOU WOULD DO WITH ONE UNINTERRUPTED DAY AND START IT",
"WORK ON SOMETHING THAT SCARES YOU BECAUSE IT MIGHT NOT WORK",
"WRITE A COMPLETE DESCRIPTION OF DONE FOR YOUR CURRENT PROJECT",
"COMPLETE THE TASK THAT HAS THE MOST DEPENDENCIES ON IT",
"IDENTIFY EVERY ASSUMPTION IN YOUR CURRENT PLAN",
"WORK UNTIL YOUR CURRENT TASK IS FINISHED RATHER THAN PAUSED",
"WRITE A COMPLETE AND HONEST ANSWER TO WHETHER YOUR APPROACH IS WORKING",
"COMPLETE THE FULL SCOPE OF YOUR NEXT MILESTONE BEFORE STOPPING",
"IDENTIFY THE SINGLE BEST USE OF THE NEXT FOUR HOURS AND DO IT",
"WORK ON YOUR MOST IMPORTANT RELATIONSHIP IN YOUR CURRENT PROJECT",
"WRITE THE DOCUMENT THAT WOULD MOST HELP THE PERSON WHO COMES AFTER YOU",
"COMPLETE EVERY TASK YOU TOLD SOMEONE ELSE YOU WOULD COMPLETE THIS WEEK"
};
const int HARD_TASK_COUNT = 100;

const char* rareTask[] = {
"YOUR MOST PRODUCTIVE HOUR TODAY HAS ALREADY PASSED. RECOVER WHAT YOU CAN",
"DELETE SOMETHING YOU HAVE BEEN KEEPING FOR REASONS YOU CANNOT EXPLAIN",
"WRITE DOWN THE TASK YOU KEEP MOVING TO TOMORROW. IT IS DUE NOW",
"THE FILE YOU NEED IS NOT LOST. YOU MOVED IT. FIND IT",
"CLOSE EVERY WINDOW AND REOPEN ONLY WHAT YOU ACTUALLY NEED",
"SIT DOWN AND DO THE THING. THE PREPARATION IS ALREADY DONE"
};
const int RARE_TASK_COUNT = 6;

// Rare gamePrescripts for the TIMED menu pull from all four pools
bool isRareEvent = false;

// =================================================
// REWARDS + PUNISHMENTS - GAME
// =================================================

const char* coreRewards[] = {
"ALLOW YOURSELF A SMALL SNACK OR TREAT",
"TAKE A TEN MINUTE BREAK WITH NO OBLIGATIONS",
"WATCH ONE SHORT VIDEO GUILT FREE",
"SIT IN SILENCE FOR FIVE MINUTES AS A REWARD",
"MAKE YOURSELF A HOT DRINK AND ENJOY IT SLOWLY",
"CLOSE ALL YOUR TABS FOR A FULL MINUTE"
};
const int GAME_REWARD_COUNT = 6;
const char* corePunishments[] = {
"WRITE YOUR NEXT THREE TASKS DOWN IMMEDIATELY",
"STAND UP AND DO TEN SLOW STRETCHES NOW",
"CLOSE EVERY NON ESSENTIAL TAB OR APP",
"WRITE ONE HONEST REASON YOU FAILED THIS",
"SET A TWO MINUTE TIMER AND DO NOTHING ELSE",
"DELETE ONE THING FROM YOUR PHONE RIGHT NOW"
};
const int GAME_PUNISH_COUNT = 6;

// =================================================
// REWARDS + PUNISHMENTS  -  CITY
// =================================================
const char* cityRewards[] = {
"FIND A 24H CAFE AND ORDER SOMETHING SMALL",
"BUY A DRINK FROM THE NEXT VENDOR YOU SEE",
"SIT ON A BENCH AND WATCH THE STREET FOR FIVE MINUTES",
"TAKE THE LONGER ROUTE HOME AS A REWARD WALK",
"FIND A QUIET ALLEY AND STAND IN IT FOR ONE MINUTE",
"LOOK UP AT THE ROOFTOPS FOR THIRTY SECONDS"
};
const int CITY_REWARD_COUNT = 6;
const char* cityPunishments[] = {
"WALK ONE EXTRA BLOCK WITHOUT A DESTINATION",
"STAND IN AN UNCOMFORTABLE SPOT FOR TWO MINUTES",
"ENTER THE NEXT BUILDING YOU PASS THAT IS OPEN",
"STOP AND FACE THE OPPOSITE DIRECTION FOR THIRTY SECONDS",
"STAY OFF YOUR PHONE FOR THE NEXT FIVE MINUTES",
"READ EVERY SIGN YOU PASS FOR ONE FULL BLOCK"
};
const int CITY_PUNISH_COUNT = 6;

// =================================================
// REWARDS + PUNISHMENTS  -  CON
// =================================================
const char* conRewards[] = {
"GET ICE CREAM FROM THE CON VENUE",
"BROWSE THE DEALER ROOM FOR TEN MINUTES WITH NO BUDGET",
"SIT AND WATCH THE CROWD FROM ONE SPOT FOR FIVE MINUTES",
"FIND THE BEST PROP YOU HAVE SEEN TODAY AND STUDY IT",
"TAKE A PHOTO OF YOUR FAVORITE COSPLAY SO FAR",
"FIND A QUIET CORNER AND REST FOR FIVE MINUTES"
};
const int CON_REWARD_COUNT = 6;
const char* conPunishments[] = {
"ASK A STRANGER WHICH FANDOM THEY ARE FROM",
"STAND IN THE BUSIEST HALLWAY FOR TWO MINUTES",
"FIND A PANEL YOU KNOW NOTHING ABOUT AND ENTER IT",
"COMPLIMENT SOMEONE YOU HAVE ALREADY PASSED",
"VISIT A TABLE IN ARTIST ALLEY YOU HAVE BEEN IGNORING",
"WALK THE ENTIRE MAIN FLOOR WITHOUT STOPPING"
};
const int CON_PUNISH_COUNT = 6;

// =================================================
// REWARDS + PUNISHMENTS  -  TASK
// =================================================
const char* taskRewards[] = {
"TAKE A TEN MINUTE SCREEN FREE REST",
"MAKE YOURSELF A SNACK BEFORE YOUR NEXT TASK",
"CLOSE EVERYTHING AND SIT QUIETLY FOR THREE MINUTES",
"WRITE DOWN ONE THING YOU ARE PROUD OF TODAY",
"SPEND FIVE MINUTES ON SOMETHING PURELY ENJOYABLE",
"ALLOW YOURSELF TO DO NOTHING FOR TWO MINUTES"
};
const int TASK_REWARD_COUNT = 6;
const char* taskPunishments[] = {
"DELETE ONE COMFORT APP ICON FROM YOUR HOME SCREEN",
"WRITE DOWN EVERY UNFINISHED TASK YOU CAN THINK OF",
"SET YOUR PHONE FACE DOWN FOR THE NEXT TEN MINUTES",
"CLOSE ALL ENTERTAINMENT TABS IMMEDIATELY",
"WRITE ONE HONEST SENTENCE ABOUT YOUR CURRENT FOCUS",
"DO TWENTY SLOW SHOULDER ROLLS BEFORE CONTINUING"
};
const int TASK_PUNISH_COUNT = 6;

// =================================================
// REWARDS + PUNISHMENTS
// rewards[menu][0] = reward strings
// rewards[menu][1] = punishment strings
// rewardCounts[menu][0/1] = count for each
// =================================================
static const char** rewards[4][2] = {
  { coreRewards,  corePunishments  },
  { cityRewards,  cityPunishments  },
  { conRewards,   conPunishments   },
  { taskRewards,  taskPunishments  }
};
static const int rewardCounts[4][2] = {
  { GAME_REWARD_COUNT,  GAME_PUNISH_COUNT  },
  { CITY_REWARD_COUNT,  CITY_PUNISH_COUNT  },
  { CON_REWARD_COUNT,   CON_PUNISH_COUNT   },
  { TASK_REWARD_COUNT,  TASK_PUNISH_COUNT  }
};
// =================================================
#define MAX_DECK 100

int decks[5][MAX_DECK];
int deckPos[5] = {MAX_DECK, MAX_DECK, MAX_DECK, MAX_DECK, MAX_DECK};

static const int deckCounts[5] = {
  GAME_POOL_COUNT, CITY_COUNT, CON_COUNT, TASK_COUNT, GAME_POOL_COUNT
};

void shuffleDeck(int* deck, int count) {
  for (int i = 0; i < count; i++) deck[i] = i;
  for (int i = count - 1; i > 0; i--) {
    int j = random(i + 1);
    int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp;
  }
}

int drawCard(int menuIndex) {
  int count = deckCounts[menuIndex];
  if (deckPos[menuIndex] >= count) {
    shuffleDeck(decks[menuIndex], count);
    deckPos[menuIndex] = 0;
  }
  return decks[menuIndex][deckPos[menuIndex]++];
}

// =================================================
// PERSISTENCE
// =================================================
static bool _needsTimedRedraw = false;   // set by tickTimedBackground, consumed by loop
static bool _deckDirty   = false;   // deck arrays changed (shuffle/draw)
static bool _stateDirty  = false;   // counters/timed state changed
static unsigned long _lastSave = 0;
#define SAVE_DEBOUNCE_MS 5000   // write at most every 5 seconds

// Save only counters, timed state, and settings — fast, no deck arrays
void saveStateFast() {
  prefs.begin("pscript", false);

  for (int i = 0; i < 5; i++) {
    char k[8];
    snprintf(k, 8, "pos%d", i);    prefs.putInt(k, deckPos[i]);
    snprintf(k, 8, "pas%d", i);    prefs.putInt(k, passCount[i]);
    snprintf(k, 8, "fai%d", i);    prefs.putInt(k, failCount[i]);
    snprintf(k, 8, "str%d", i);    prefs.putInt(k, streak[i]);
    snprintf(k, 8, "bst%d", i);    prefs.putInt(k, bestStreak[i]);
    snprintf(k, 8, "hhd%d", i);    prefs.putInt(k, histHead[i]);
  }
  prefs.putBytes("hist0", history[0], HIST_LEN * sizeof(int));
  prefs.putBytes("hist1", history[1], HIST_LEN * sizeof(int));
  prefs.putBytes("hist2", history[2], HIST_LEN * sizeof(int));
  prefs.putBytes("hist3", history[3], HIST_LEN * sizeof(int));
  prefs.putBytes("hist4", history[4], HIST_LEN * sizeof(int));

  prefs.putUShort("tmin", timerMinutes);
  prefs.putUChar("thr",  timerHours);
  prefs.putUChar("tdy",  timerDays);
  prefs.putUChar("brt",  brightnessLevel);
  prefs.putULong("slp",  sleepMs);
  prefs.putUChar("spg",  cfg_glyphSpinMs);
  prefs.putUChar("spr",  cfg_revealChars);
  prefs.putUChar("spt",  cfg_transitionPx);
  prefs.putInt("dmed",   diffMedThreshold);
  prefs.putInt("dhrd",   diffHardThreshold);
  prefs.putUChar("tpol", timedPoolEnabled);

  prefs.putBool("tact",  timedActive);
  prefs.putBool("tpass", timedPassed);
  prefs.putBool("tpaus", timedPaused);
  {
    unsigned long dur = timerDurationMs(), rem = 0;
    if (timedActive) {
      if (timedPaused) rem = timedPauseRem;
      else { unsigned long e = millis()-timedStart; rem=(e<dur)?dur-e:0; }
    }
    prefs.putULong("trem", rem);
  }
  prefs.putString("tcur",  timedCurrent);
  prefs.putBool("trare",   timedIsRare);
  prefs.putInt("tpool",    timedPool);
  prefs.putInt("tcidx",    timedCardIndex);
  prefs.putBytes("trtd",   tarotDeck, 22*sizeof(int));
  prefs.putInt("trtp",     tarotPos);

  prefs.end();
  _stateDirty = false;
  _lastSave   = millis();
}

// Full save including deck arrays — only on shuffle/settings change
void saveState() {
  esp_task_wdt_reset();   // feed watchdog before slow flash writes
  prefs.begin("pscript", false);

  prefs.putBytes("deck0", decks[0], GAME_POOL_COUNT * sizeof(int));
  esp_task_wdt_reset();
  prefs.putBytes("deck1", decks[1], CITY_COUNT      * sizeof(int));
  esp_task_wdt_reset();
  prefs.putBytes("deck2", decks[2], CON_COUNT       * sizeof(int));
  esp_task_wdt_reset();
  prefs.putBytes("deck3", decks[3], TASK_COUNT      * sizeof(int));
  esp_task_wdt_reset();
  prefs.putBytes("deck4", decks[4], GAME_POOL_COUNT * sizeof(int));
  esp_task_wdt_reset();

  prefs.end();

  _deckDirty = false;
  saveStateFast();
}

// Mark state dirty — actual write is debounced in loop()
void markStateDirty() { _stateDirty = true; }
void markDeckDirty()  { _deckDirty  = true; _stateDirty = true; }

void loadState() {
  prefs.begin("pscript", true);
  bool hasData = prefs.isKey("pos0");

  if (hasData) {
    prefs.getBytes("deck0", decks[0], GAME_POOL_COUNT   * sizeof(int));
    prefs.getBytes("deck1", decks[1], CITY_COUNT        * sizeof(int));
    prefs.getBytes("deck2", decks[2], CON_COUNT         * sizeof(int));
    prefs.getBytes("deck3", decks[3], TASK_COUNT  * sizeof(int));
    prefs.getBytes("deck4", decks[4], GAME_POOL_COUNT   * sizeof(int));

    for (int i = 0; i < 5; i++) {
      char k[8];
      snprintf(k, 8, "pos%d", i);   deckPos[i]    = prefs.getInt(k, MAX_DECK);
      snprintf(k, 8, "pas%d", i);   passCount[i]  = prefs.getInt(k, 0);
      snprintf(k, 8, "fai%d", i);   failCount[i]  = prefs.getInt(k, 0);
      snprintf(k, 8, "str%d", i);   streak[i]     = prefs.getInt(k, 0);
      snprintf(k, 8, "bst%d", i);   bestStreak[i] = prefs.getInt(k, 0);
      snprintf(k, 8, "hhd%d", i);   histHead[i]   = prefs.getInt(k, 0);
    }

    prefs.getBytes("hist0", history[0], HIST_LEN * sizeof(int));
    prefs.getBytes("hist1", history[1], HIST_LEN * sizeof(int));
    prefs.getBytes("hist2", history[2], HIST_LEN * sizeof(int));
    prefs.getBytes("hist3", history[3], HIST_LEN * sizeof(int));
    prefs.getBytes("hist4", history[4], HIST_LEN * sizeof(int));

    timerMinutes   = prefs.getUShort("tmin", 20);
    timerHours     = prefs.getUChar("thr",  0);
    timerDays      = prefs.getUChar("tdy",  0);
    brightnessLevel= prefs.getUChar("brt",  2);
    sleepMs        = prefs.getULong("slp",  120000UL);

    cfg_glyphSpinMs  = prefs.getUChar("spg", 90);
    cfg_revealChars  = prefs.getUChar("spr", 1);
    cfg_transitionPx = prefs.getUChar("spt", 10);

    diffMedThreshold  = prefs.getInt("dmed", 10);
    diffHardThreshold = prefs.getInt("dhrd", 25);
    timedPoolEnabled = prefs.getUChar("tpol", 0b00001111);

    timedActive    = prefs.getBool("tact",  false);
    timedPassed    = prefs.getBool("tpass", false);
    String _tc = prefs.getString("tcur", "");
    strncpy(timedCurrent, _tc.c_str(), sizeof(timedCurrent)-1);
    timedCurrent[sizeof(timedCurrent)-1] = '\0';
    timedIsRare    = prefs.getBool("trare", false);
    timedPool      = prefs.getInt("tpool", 0);
    timedCardIndex = prefs.getInt("tcidx", -1);

    // Restore timer: always resume as paused so millis() offset doesn't corrupt it
    if (timedActive && !timedPassed) {
      unsigned long rem = prefs.getULong("trem", 0);
      timedPauseRem = rem;
      timedPaused   = true;
      timedStart    = 0;   // not used while paused
    } else {
      timedPaused   = prefs.getBool("tpaus", false);
      timedPauseRem = 0;
      timedStart    = 0;
    }
  }

  prefs.end();

  if (!hasData) {
    shuffleDeck(decks[0], GAME_POOL_COUNT);
    shuffleDeck(decks[1], CITY_COUNT);
    shuffleDeck(decks[2], CON_COUNT);
    shuffleDeck(decks[3], TASK_COUNT);
    shuffleDeck(decks[4], GAME_POOL_COUNT);
    shuffleDeck(decks[0], GAME_POOL_COUNT);
    shuffleDeck(decks[1], CITY_COUNT);
    shuffleDeck(decks[2], CON_COUNT);
    shuffleDeck(decks[3], TASK_COUNT);
    shuffleDeck(decks[4], GAME_POOL_COUNT);
    for (int i = 0; i < 5; i++) {
      deckPos[i] = 0;
      for (int j = 0; j < HIST_LEN; j++) history[i][j] = -2;
    }
  }
}

void resetDeck(int menuIndex, bool resetStats) {
  shuffleDeck(decks[menuIndex], deckCounts[menuIndex]);
  deckPos[menuIndex] = 0;
  if (resetStats) {
    passCount[menuIndex]  = 0;
    failCount[menuIndex]  = 0;
    streak[menuIndex]     = 0;
    bestStreak[menuIndex] = 0;
    histHead[menuIndex]   = 0;
    for (int j = 0; j < HIST_LEN; j++) history[menuIndex][j] = -2;
  }
  markStateDirty();
}

// =================================================
// MENU_GAME STATE  (regular menus)
// =================================================
char   current[256];
int    revealIndex = 0;
int    prescriptTextSize = 2;   // 2 for short, 1 for long prescripts
char   lines[20][32];
int    lineCount   = 0;
int    logoBottomY = 0;

// Per-menu persistent prescript storage so each menu keeps its
// own card until passed/failed.
char         menuCurrent[4][256];     // 0=GAME 1=CITY 2=CON 3=TASK
int          menuRevealIndex[4]    = {0,0,0,0};
char         menuLines[4][20][32];
int          menuLineCount[4]       = {0,0,0,0};
bool         menuIsRare[4]          = {false,false,false,false};
bool         menuHasPrescript[4]    = {false,false,false,false};

// =================================================
// RESULT STATE
// =================================================
bool resultPass = false;

enum ResultAnimState {
  RESULT_FILL,
  RESULT_REVEAL,
  RESULT_HOLD,
  RESULT_REWARD_REVEAL,
  RESULT_REWARD_HOLD,
  RESULT_DRAIN
};

bool            resultFullyDrawn  = false;
ResultAnimState resultState       = RESULT_FILL;
int             resultFillY       = 240;
int             resultRevealIndex = 0;
int             rewardRevealIndex = 0;
bool            resultInitialized = false;
char            resultText[16]    = "";
unsigned long   lastAnimFrame     = 0;
unsigned long   lastReveal        = 0;

// =================================================
// ACTIVE SET  (used by regular menus)
// =================================================
const char** activeSet;
int          activeCount;

// Scroll positions for scrollable menus
int toolsScrollY = 0;
int cyberScrollY = 0;

// =================================================
// FORWARD DECLARATIONS
// =================================================
void goTo(Screen target);
int  screenToMenuIdx(Screen s);
const char* prescriptScreenName(Screen s);
int  wrapText(const char* src, char out[][32], int maxCols);
int  wrapTextWide(const char* src, char out[][54], int maxCols);
void drawMenu();
void drawSettings();
void drawSettingsTimer();
void drawSettingsIdle();
void drawSettingsBright();
void drawSettingsSpeed();
void drawSettingsDifficulty();
void drawSettingsCategories();
void drawResetConfirm();
void drawHistoryScreen();
void drawCyberMenu();
void drawToolsMenu();
void drawTimedMenu();
void tickTimedMenu();
void tickTimedBackground();
// =================================================
// =================================================
// LYRICS SCREEN — Children of the City (LoR)
// =================================================
// Each entry is one display line. Blank entries ("") create a visual pause.
// First entry should always be the song title.
static const char* lyricsLines[] = {
  "CHILDREN OF THE CITY",
  "AND THEN DRINK A LITER OF MILK",
  "WARM-UP BEFORE YOU GO PLAY",
  "ONLY EAT, OR WRITE",
  "OR PULL THE TRIGGER WITH YOUR RIGHT HAND",
  "ONLY THING THAT'S LEFT",
  "IS TO WORK ON FOLLOWING COMMANDS",
  "",
  "BY THE TIME YOU REALIZE",
  "YOU'LL BE RESTRAINED TO A DESK",
  "AND WITH YOUR DREAMS ON THE FLOOR, YOU COMPLY",
  "EYES CHAINED TO THE TEST",
  "IN 30 MINUTES, FIND A GROOM OR BRIDE",
  "BONUS IF BRUNETTE",
  "IN 90 HOURS, SPILL THEIR INSIDES",
  "PAINT YOUR ROOM PICTURESQUE",
  "",
  "NOW IT'S TIME FOR ANOTHER VENDETTA",
  "GOING THROUGH THE SHELVES",
  "PICKING OUT MY PRE-WRITTEN PERSONA",
  "(HA-AH-AH)",
  "CHILDREN OF THE CITY SEES ONLY THE NEON STARS",
  "REFLECTED UPON THE MURKY GUTTER SKY",
  "DON'T ASK ME WHY",
  "I DESPERATELY WISH TO BE INCLUDED",
  "IN THE CITY'S NIGHT",
  "",
  "IN FOUR HUNDRED THOUSAND METERS, TURN RIGHT",
  "",
  "PICK UP A KNIFE AND STAB A FAMILIAR WARM BODY",
  "LEARNED TO FIGHT BEFORE I KNEW LOVE",
  "OR BITTERNESS OF COFFEE",
  "SNIPPY SCISSORS CUT DOWN THE STRINGS",
  "I SET MYSELF FREE",
  "ONLY TO FIGURE OUT EVERYTHING",
  "I CHOSE WAS BY PROXY",
  "",
  "AS WE SUCKLED UPON THE NINE MILLIMETER PACIFIER",
  "SWALLOWING THE FACT THAT OTHER THAN TO EXPAND",
  "WE HAD NO PURPOSE",
  "AS MY EVER-BURNING WILL TO STAY AFLOAT BACKFIRES",
  "I NOW KNOW I MUST BE COMFORTABLE BEING",
  "WHO I CONSIDERED WORTHLESS",
  "",
  "FOLLOW THE CITY'S RIBBON",
  "TO A HEART NOBODY SEEMS TO LISTEN",
  "IT TAKES MY HEART BEING BROKEN AND BROKEN AGAIN",
  "(BROKEN AND BROKEN AGAIN)",
  "TO KNOW THAT I AM THE REASON WHY",
  "(THE REASON WHY)",
  "THE SUFFERINGS NEVER END",
  "",
  "NOW IT'S TIME FOR ANOTHER VENDETTA",
  "(HMM-MMM-MMM)",
  "GOING THROUGH THE SHELVES",
  "PICKING OUT MY PRE-WRITTEN PERSONA",
  "(HA-AH-AH)",
  "CHILDREN OF THE CITY SEES",
  "ONLY THE NEON STARS",
  "REFLECTED UPON THE MURKY GUTTER SKY",
  "DON'T ASK ME WHY",
  "I DESPERATELY WISH TO BE NOTICED",
  "BY THE CITY'S NIGHT",
  "",
  "DO NOT GO HOME UNTIL YOU FINISH",
  "READING THE VALUE OF E.",
  "2.71 8281 8284 5904 5235 3602 8747 1352",
  "6624 9775 7247 0936 9995 9574 9669 6762",
};
static const int LYRICS_COUNT = sizeof(lyricsLines) / sizeof(lyricsLines[0]);

// =================================================
// SONG CATALOGUE
// Add songs.h to your Arduino project folder with
// additional song arrays following the same format.
// =================================================
#include "songs.h"

struct SongDef {
  const char*  title;
  const char** lines;
  int          count;
};

static SongDef songCatalogue[] = {
  { "CHILDREN OF THE CITY", lyricsLines,               LYRICS_COUNT               },
  { "IRON LOTUS",           lyrics_IRON_LOTUS,          LYRICS_IRON_LOTUS_COUNT    },
  { "FLY MY WINGS",         lyrics_FLY_MY_WINGS,        LYRICS_FLY_MY_WINGS_COUNT  },
  { "HERO",                 lyrics_HERO,                LYRICS_HERO_COUNT          },
  { "THROUGH PATCHES",      lyrics_THROUGH_PATCHES_OF_VIOLET, LYRICS_THROUGH_PATCHES_OF_VIOLET_COUNT },
  { "COMPASS",              lyrics_COMPASS,             LYRICS_COMPASS_COUNT       },
  { "TIAN TIAN",            lyrics_TIAN_TIAN,           LYRICS_TIAN_TIAN_COUNT     },
  { "SAIKAI",               lyrics_SAIKAI,              LYRICS_SAIKAI_COUNT        },
  { "STRING THEOCRACY",     lyrics_STRING_THEOCRACY,    LYRICS_STRING_THEOCRACY_COUNT },
  { "FROM A PLACE OF LOVE", lyrics_FROM_A_PLACE_OF_LOVE,LYRICS_FROM_A_PLACE_OF_LOVE_COUNT },
  { "PASS ON",              lyrics_PASS_ON,              LYRICS_PASS_ON_COUNT               },
};
static const int SONG_COUNT = sizeof(songCatalogue) / sizeof(songCatalogue[0]);

static int songSelectScroll = 0;   // top visible song index

// Active song pointer — set when user selects a song
static const char** activeLyricsLines = nullptr;
static int          activeLyricsCount = 0;
static int          activeSongIdx     = 0;   // which song is selected
// Slot 0 = older line (may be erasing), Slot 1 = current line
// =================================================

static LyricsSlot lyricsSlots[2];
static int  lyricsLineIdx    = 0;
static int  lyricsActiveSlot = 0;
static bool lyricsSongDone   = false;
static bool lyricsWaveInit[2] = {false, false};
static int8_t lyricsWavePrev[2][32];

static void lyricsPickDir(LyricsDir* d) {
  // dx = per-ROW x offset (staggers rows horizontally)
  // dy = per-CHARACTER y offset within a row (creates the diagonal reading angle)
  // Positive dy = text slopes downward left-to-right
  // Negative dy = text slopes upward left-to-right
  static const LyricsDir dirs[] = {
    {0,  0}, {0,  0},    // flat (weighted)
    {0,  1}, {0, -1},    // very gentle slope
    {0,  2}, {0, -2},    // gentle slope
    {0,  3}, {0, -3},    // moderate slope
    {4,  2}, {4, -2},    // moderate slope with row stagger
    {-4, 2}, {-4,-2},    // opposite stagger direction
  };
  *d = dirs[random(12)];
}

// Wrap a raw line into a slot with safe on-screen positioning
static void lyricsLoadSlot(LyricsSlot* s, const char* line, int slotIdx) {
  // Check for emphasis prefix '!'
  bool emphasis = (line[0] == '!');
  if (emphasis) line++;   // skip the '!' for display

  int len = strlen(line);
  s->reveal    = 0;
  s->holding   = false;
  s->holdTimer = 0;
  s->erasing   = false;
  s->eraseMs   = 0;
  s->active    = true;
  s->rowCount  = 0;
  s->totalChars= 0;
  s->isTitle   = false;
  s->isEmphasis = emphasis;

  if (len == 0) { s->active = false; return; }

  // Title line (index 0) always size 3, centred, no diagonal
  // Emphasis lines (! prefix): size 3, centred, no diagonal  
  // Regular lines: size 2, diagonal, randomised position
  bool isTitle = (lyricsLineIdx == 0);
  bool isLastLine = (lyricsLineIdx == activeLyricsCount - 1);
  if (isTitle || isLastLine || emphasis) {
    s->cw = 18; s->ch = 26;
    s->dir.dx = 0; s->dir.dy = 0;
  } else {
    s->cw = 12; s->ch = 18;
    lyricsPickDir(&s->dir);
  }
  int cols = (s->cw == 18) ? 13 : 20;
  s->isTitle    = isTitle;
  s->isEmphasis = (emphasis || isTitle || isLastLine);

  // Word-wrap
  const char* p = line;
  while (*p && s->rowCount < 8) {
    int remaining = len - (int)(p - line);
    char buf[54]; int bi = 0;
    if (remaining <= cols) {
      while (*p) buf[bi++] = *p++;
    } else {
      const char* q = p;
      for (int i = 0; i < cols && *q; i++) buf[bi++] = *q++;
      int bi2 = bi; const char* q2 = q;
      while (bi2 > 0 && buf[bi2-1] != ' ') { bi2--; q2--; }
      if (bi2 == 0) { p = q; }
      else { bi = bi2; p = q2;
             while (bi > 0 && buf[bi-1] == ' ') bi--; }
    }
    buf[bi] = '\0';
    strncpy(s->wrapped[s->rowCount], buf, 53);
    s->wrapped[s->rowCount][53] = '\0';
    s->totalChars += strlen(s->wrapped[s->rowCount]);
    s->rowCount++;
  }

  // Cap dx so the full swept row stays on screen
  // Worst case: baseX + rlen*(cw+dx) <= 318
  int maxRlen = 0;
  for (int r = 0; r < s->rowCount; r++) {
    int l = strlen(s->wrapped[r]);
    if (l > maxRlen) maxRlen = l;
  }
  if (maxRlen > 0) {
    int maxDx = max(0, (316 - maxRlen * s->cw) / maxRlen);
    if (abs(s->dir.dx) > maxDx) s->dir.dx = maxDx * (s->dir.dx >= 0 ? 1 : -1);
  }

  // No dx cap needed — x is always baseX + i*cw (straight left-to-right)
  // dy is per-character y offset — need to ensure row stays within screen vertically
  // Max y drift within a row = (rlen-1) * abs(dy)

  // Pixel width: always cols*cw, straight horizontal
  int maxRowPxW = 0;
  for (int r = 0; r < s->rowCount; r++) {
    int rlen = strlen(s->wrapped[r]);
    int w = rlen * s->cw;
    if (w > maxRowPxW) maxRowPxW = w;
  }

  // Block height: row stacking + per-char dy drift within each row
  int maxRowDrift = 0;
  for (int r = 0; r < s->rowCount; r++) {
    int rlen = strlen(s->wrapped[r]);
    int drift = (rlen - 1) * abs(s->dir.dy);
    if (drift > maxRowDrift) maxRowDrift = drift;
  }
  // Total vertical span: rows stacked + drift within each row
  int blockH = s->rowCount * s->ch
             + abs(s->dir.dx) * max(0, s->rowCount - 1)   // row stagger
             + maxRowDrift;                                  // per-char y drift

  // First/last lines and emphasis lines always centred
  if (emphasis || lyricsLineIdx == 0 || lyricsLineIdx == activeLyricsCount - 1) {
    s->baseX = max(2, (320 - maxRowPxW) / 2);
    s->baseY = max(2, (240 - blockH) / 2);
    s->dir.dx = 0; s->dir.dy = 0;
  } else {
    int xSafe = max(2, 320 - maxRowPxW - 2);
    s->baseX = (xSafe > 4) ? random(2, xSafe) : 2;

    // Y start: if dy is positive text drifts down, so start near top of half
    // If dy is negative text drifts up, start lower in half
    int yHalf = 118;
    if (slotIdx == 0) {
      int yBot = yHalf - maxRowDrift - 2;
      int yTop = 4;
      if (s->dir.dy < 0) {
        // Slopes up — start lower so drift doesn't exit top
        s->baseY = (yBot > yTop + maxRowDrift) ? random(yTop + maxRowDrift, yBot) : yTop + maxRowDrift;
      } else {
        s->baseY = (yBot > yTop + 2) ? random(yTop, yBot) : yTop;
      }
    } else {
      int yTop = yHalf + 2;
      int yBot = max(yTop + 2, 236 - s->rowCount * s->ch - maxRowDrift);
      if (s->dir.dy < 0) {
        s->baseY = (yBot > yTop + maxRowDrift) ? random(yTop + maxRowDrift, yBot) : yTop + maxRowDrift;
      } else {
        s->baseY = (yBot > yTop + 2) ? random(yTop, yBot) : yTop;
      }
    }
  }
}

// Draw one slot — reveal or erase
static void lyricsDrawSlot(LyricsSlot* s) {
  if (!s->active) return;
  tft.setTextSize(s->cw == 18 ? 3 : 2);

  static unsigned long lastWaveDraw[2] = {0, 0};
  int slotN = (s == &lyricsSlots[0]) ? 0 : 1;
  if (!lyricsWaveInit[slotN]) {
    memset(lyricsWavePrev[slotN], 0, 32);
    lyricsWaveInit[slotN] = true;
  }
  bool waveChanged = (millis() - lastWaveDraw[slotN] > 120);
  if (!s->erasing && !waveChanged && s->reveal >= s->totalChars) return;
  if (waveChanged) lastWaveDraw[slotN] = millis();

  int waveOff[32];
  float phase = (float)(millis() % 8000) / 8000.0f * 6.2832f;
  float amp   = s->isEmphasis ? 1.2f : 1.8f;
  for (int w = 0; w < 32; w++) {
    waveOff[w] = (int)(sinf(phase + w * 0.4f) * amp);
  }

  if (waveChanged && !s->erasing && s->reveal >= s->totalChars) {
    int ci = 0;
    for (int r = 0; r < s->rowCount; r++) {
      int rlen = strlen(s->wrapped[r]);
      int rowBaseX = s->baseX + r * s->dir.dx;
      int rowBaseY = s->baseY + r * (s->ch + abs(s->dir.dy));
      bool fflat = s->isEmphasis;
      if (fflat) rowBaseX = (320 - rlen * s->cw) / 2;
      for (int i = 0; i < rlen; i++) {
        int cx = rowBaseX + i * s->cw;
        int8_t oldOff = lyricsWavePrev[slotN][ci % 32];
        int8_t newOff = (int8_t)waveOff[ci % 32];
        if (oldOff != newOff) {
          int oldCy = rowBaseY + (fflat ? 0 : i * s->dir.dy) + oldOff;
          if (cx >= 0 && cx + s->cw <= 320 && oldCy >= 0 && oldCy + s->ch <= 240)
            tft.fillRect(cx, oldCy, s->cw, s->ch, BG_COLOR);
        }
        ci++;
      }
    }
    memcpy(lyricsWavePrev[slotN], waveOff, 32);
  }

  int charIdx = 0;
  for (int r = 0; r < s->rowCount; r++) {
    const char* row = s->wrapped[r];
    int rlen = strlen(row);
    int rowBaseX = s->baseX + r * s->dir.dx;
    int rowBaseY = s->baseY + r * (s->ch + abs(s->dir.dy));

    // Title line (first line of song) or emphasis line — centred, no diagonal
    bool forceFlat = s->isTitle || s->isEmphasis;
    if (forceFlat) rowBaseX = (320 - rlen * s->cw) / 2;
    for (int i = 0; i < rlen; i++) {
      // x advances straight left-to-right
      // y shifts per character — this IS the diagonal
      int cx = rowBaseX + i * s->cw;
      int cy = rowBaseY + (forceFlat ? 0 : i * s->dir.dy)
               + (s->erasing ? 0 : waveOff[charIdx % 32]);  // no wave during erase
      if (cx < 0 || cx + s->cw > 320) { charIdx++; continue; }
      if (cy < 0 || cy + s->ch > 240) { charIdx++; continue; }

      char c; uint16_t col;
      if (s->erasing) {
        unsigned long charStartMs = (s->totalChars > 1)
          ? (unsigned long)charIdx * 1600UL / (s->totalChars - 1) : 0UL;
        unsigned long charEndMs = charStartMs + 400UL;
        if (s->eraseMs < charStartMs) {
          c = row[i]; col = TEXT_COLOR;
        } else if (s->eraseMs < charEndMs) {
          unsigned long elapsed = s->eraseMs - charStartMs;
          uint8_t br = (uint8_t)(80UL * (400UL - elapsed) / 400UL);
          c = (char)random(33, 126);
          col = tft.color565(br/4, br/2, br);
        } else {
          c = ' '; col = BG_COLOR;
        }
      } else {
        if (charIdx < s->reveal) { c = row[i]; col = TEXT_COLOR; }
        else { c = (char)random(33,126); col = tft.color565(3,5,7); }
      }
      // setTextColor with BG_COLOR background repaints the full char cell inline —
      // no separate fillRect needed, no flicker, no trail
      tft.setTextColor(col, BG_COLOR);
      tft.setCursor(cx, cy); tft.print(c);
      charIdx++;
    }
  }
}

static int lyricsVisibleLines = 0;

static void lyricsReset() {
  lyricsLineIdx    = 0;
  lyricsActiveSlot = 0;
  lyricsSongDone   = false;
  lyricsVisibleLines = 0;
  lyricsSlots[0].active = false;
  lyricsSlots[1].active = false;
  lyricsLoadSlot(&lyricsSlots[0], activeLyricsLines[0], 0);
}

static void fxClear();   // forward declaration — defined in effects section below

void drawLyricsScreen() {
  tft.fillScreen(BG_COLOR);
  lyricsVisibleLines = 0;
  fxClear();
  lyricsReset();
}

void lyricsSelectSong(int idx) {
  activeSongIdx     = idx;
  activeLyricsLines = songCatalogue[idx].lines;
  activeLyricsCount = songCatalogue[idx].count;
}

void drawSongSelect() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320 - 11*6)/2, 4);
  tft.print("SELECT SONG");
  tft.drawFastHLine(0, 14, 320, tft.color565(30,50,80));
  const int itemH = 22, startY = 18;
  for (int i = 0; i < SONG_COUNT; i++) {
    int iy = startY + (i - songSelectScroll) * itemH;
    if (iy < 14 || iy + itemH > 230) continue;
    bool sel = (i == activeSongIdx);
    if (sel) tft.fillRect(0, iy, 320, itemH-1, tft.color565(10,20,40));
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(80,120,180), BG_COLOR);
    tft.setCursor(8, iy + 7); tft.print(songCatalogue[i].title);
  }
  tft.drawFastHLine(0, 232, 320, tft.color565(30,50,80));
  tft.setTextColor(tft.color565(40,60,90), BG_COLOR);
  tft.setCursor(4, 234); tft.print("TAP: PLAY  SWIPE DOWN: BACK");
}

// =================================================
// PER-SONG VISUAL EFFECTS
// Song indices: 0=Children of the City, 1=Iron Lotus, 2=Fly My Wings,
//               3=Hero, 4=Through Patches of Violet, 5=Compass,
//               6=Tian Tian, 7=Saikai, 8=String Theocracy, 9=From a Place of Love
// =================================================

// ── Shared effect state ─────────────────────────
#define FX_MAX 24
static EffectParticle fxParts[FX_MAX];
static unsigned long fxLastTick = 0;

#define MAX_CRACKS 6
static GlassCrack glassCracks[MAX_CRACKS];
static unsigned long crackLastSpawn = 0;

#define MAX_SLASHES 3
static SwordSlash swordSlashes[MAX_SLASHES];
static unsigned long slashLastSpawn = 0;

static void fxClear() {
  for (int i = 0; i < FX_MAX; i++) fxParts[i].life = 0;
  for (int i = 0; i < MAX_CRACKS; i++) glassCracks[i].active = false;
  for (int i = 0; i < MAX_SLASHES; i++) swordSlashes[i].active = false;
}

static EffectParticle* fxSpawn() {
  for (int i = 0; i < FX_MAX; i++) {
    if (fxParts[i].life == 0) return &fxParts[i];
  }
  return nullptr;
}

// ── Effect: twinkling stars (Hero) ──────────────
static void fxTickStars() {
  // Spawn multiple stars per tick
  int spawns = random(1, 4);
  for (int s = 0; s < spawns; s++) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = random(0, 320); p->y = random(0, 240);
      p->life = random(6, 18);
      p->vx = 0; p->vy = 0; p->type = 0;
    }
  }
  for (int i = 0; i < FX_MAX; i++) {
    EffectParticle& p = fxParts[i];
    if (!p.life || p.type != 0) continue;
    uint8_t phase = p.life % 6;
    uint8_t br = (phase < 3) ? 60 + phase * 60 : 240 - (phase-3)*60;
    uint16_t c = tft.color565(br, br, min(255,(int)br+60));
    // Draw cross-shaped star
    tft.drawPixel(p.x, p.y, c);
    if (br > 120) {
      tft.drawPixel(p.x+1, p.y, tft.color565(br/2,br/2,br/2));
      tft.drawPixel(p.x-1, p.y, tft.color565(br/2,br/2,br/2));
      tft.drawPixel(p.x, p.y+1, tft.color565(br/2,br/2,br/2));
      tft.drawPixel(p.x, p.y-1, tft.color565(br/2,br/2,br/2));
    }
    p.life--;
    if (!p.life) {
      tft.drawPixel(p.x, p.y, BG_COLOR);
      tft.drawPixel(p.x+1, p.y, BG_COLOR); tft.drawPixel(p.x-1, p.y, BG_COLOR);
      tft.drawPixel(p.x, p.y+1, BG_COLOR); tft.drawPixel(p.x, p.y-1, BG_COLOR);
    }
  }
}

// ── Effect: fire embers along bottom (Iron Lotus) ─
static void fxTickFire() {
  // Spawn multiple embers across full bottom edge
  int spawns = random(2, 5);
  for (int s = 0; s < spawns; s++) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = random(0, 320); p->y = random(225, 240);
      p->vx = random(-120, 120); p->vy = -random(150, 400);
      p->life = random(16, 36);
      p->type = 1;
    }
  }
  for (int i = 0; i < FX_MAX; i++) {
    EffectParticle& p = fxParts[i];
    if (!p.life || p.type != 1) continue;
    // Erase old pos (2px wide for visibility)
    tft.drawPixel(p.x, p.y, BG_COLOR);
    tft.drawPixel(p.x+1, p.y, BG_COLOR);
    p.x += p.vx / 100;
    p.y += p.vy / 100;
    p.vy += 20;  // gravity
    p.life--;
    if (p.x < 0 || p.x >= 319 || p.y < 0 || !p.life) {
      p.life = 0; continue;
    }
    uint8_t g = min(255, (int)(36 - p.life) * 8);
    uint16_t col = tft.color565(255, g, 0);
    tft.drawPixel(p.x, p.y, col);
    tft.drawPixel(p.x+1, p.y, col);  // 2px wide ember
  }
  // Draw persistent flame base along bottom
  for (int x = 0; x < 320; x += 2) {
    int fy = 235 + (int)(sinf(x * 0.15f + millis() * 0.003f) * 3);
    uint8_t g = random(40, 140);
    tft.drawPixel(x, fy, tft.color565(255, g, 0));
  }
}

// ── Effect: rising feather sparks (Fly My Wings) ─
static void fxTickFeathers() {
  if (random(5) == 0) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = random(0, 320); p->y = 230;
      p->vx = random(-40, 40); p->vy = -random(60, 140);
      p->life = random(18, 36);
      p->col = tft.color565(180, 220, 255);
      p->type = 2;
    }
  }
  for (int i = 0; i < FX_MAX; i++) {
    EffectParticle& p = fxParts[i];
    if (!p.life || p.type != 2) continue;
    tft.drawPixel(p.x, p.y, BG_COLOR);
    p.x += p.vx / 100 + (int)(sinf(millis()*0.002f + p.x*0.1f) * 1);
    p.y += p.vy / 100;
    p.life--;
    if (p.y < 0 || !p.life) { p.life = 0; continue; }
    uint8_t br = p.life * 7;
    tft.drawPixel(p.x, p.y, tft.color565(br/2, br/2, br));
  }
}

// ── Effect: cracked glass (Through Patches of Violet) ─

static void glassInit() {
  for (int i = 0; i < MAX_CRACKS; i++) glassCracks[i].active = false;
}

static void drawCrackLine(int x0, int y0, int x1, int y1, uint16_t col) {
  // Bresenham-style but we use TFT line
  tft.drawLine(x0, y0, x1, y1, col);
}

static void fxTickGlass() {
  unsigned long now = millis();

  // Spawn a new crack every 1.5-3 seconds
  if (now - crackLastSpawn > (unsigned long)random(1500, 3000)) {
    crackLastSpawn = now;
    for (int i = 0; i < MAX_CRACKS; i++) {
      if (!glassCracks[i].active) {
        GlassCrack& c = glassCracks[i];
        c.ox = random(20, 300); c.oy = random(20, 220);
        // Main crack — random angle, 40-120px long
        float angle = random(0, 628) / 100.0f;
        int len = random(40, 120);
        c.x0 = c.ox; c.y0 = c.oy;
        c.x1 = c.ox + (int)(cosf(angle) * len);
        c.y1 = c.oy + (int)(sinf(angle) * len);
        // Branch crack — offset angle, shorter
        float branchAngle = angle + (random(0,2) ? 0.6f : -0.6f) + random(-20,20)*0.01f;
        int blen = random(20, 70);
        int midX = (c.ox + c.x1)/2 + random(-10,10);
        int midY = (c.oy + c.y1)/2 + random(-10,10);
        c.x2 = midX + (int)(cosf(branchAngle) * blen);
        c.y2 = midY + (int)(sinf(branchAngle) * blen);
        c.hasBranch = (random(3) != 0);
        c.born = now;
        c.lifetime = random(3000, 7000);
        c.active = true;
        // Draw immediately
        uint16_t col = tft.color565(80, 0, 120);
        drawCrackLine(c.x0, c.y0, c.x1, c.y1, col);
        if (c.hasBranch) drawCrackLine(midX, midY, c.x2, c.y2, col);
        // Add small splinter lines
        for (int s = 0; s < random(2,5); s++) {
          int sx = random(min(c.x0,c.x1), max(c.x0,c.x1)+1);
          int sy = random(min(c.y0,c.y1), max(c.y0,c.y1)+1);
          float sa = angle + (random(0,2) ? 1.2f : -1.2f);
          int sl = random(8, 25);
          tft.drawLine(sx, sy, sx+(int)(cosf(sa)*sl), sy+(int)(sinf(sa)*sl),
            tft.color565(60, 0, 90));
        }
        break;
      }
    }
  }

  // Erase expired cracks
  for (int i = 0; i < MAX_CRACKS; i++) {
    GlassCrack& c = glassCracks[i];
    if (!c.active) continue;
    if (now - c.born > (unsigned long)c.lifetime) {
      // Erase by drawing BG over them
      drawCrackLine(c.x0, c.y0, c.x1, c.y1, BG_COLOR);
      if (c.hasBranch) {
        int midX = (c.ox + c.x1)/2; int midY = (c.oy + c.y1)/2;
        drawCrackLine(midX, midY, c.x2, c.y2, BG_COLOR);
      }
      c.active = false;
    }
  }
}

// ── Effect: wave ripples at bottom (Compass) ────
static void fxTickWaves() {
  static int waveX = 0;
  waveX = (waveX + 6) % 320;   // faster scroll
  // Three wave layers at different depths and speeds
  for (int x = 0; x < 320; x++) {
    // Back wave — deep blue, slow
    int wy1 = 222 + (int)(sinf((x + waveX) * 0.05f) * 7);
    // Mid wave — cyan, medium
    int wy2 = 226 + (int)(sinf((x + waveX * 1.4f) * 0.09f) * 5);
    // Front wave — bright, fast
    int wy3 = 230 + (int)(sinf((x + waveX * 2.1f) * 0.13f) * 4);
    tft.drawPixel(x, wy1, tft.color565(0, 40, 140));
    tft.drawPixel(x, wy2, tft.color565(0, 120 + random(30), 200));
    tft.drawPixel(x, wy3, tft.color565(40, 180 + random(40), 255));
    // Erase between waves
    for (int y = wy1 - 1; y < wy3 + 2 && y < 240; y++) {
      if (y != wy1 && y != wy2 && y != wy3)
        tft.drawPixel(x, y, BG_COLOR);
    }
  }
}

// ── Effect: floating bubbles (Tian Tian) ────────
static void fxTickBubbles() {
  if (random(6) == 0) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = random(10, 310); p->y = random(200, 235);
      p->vx = random(-20, 20); p->vy = -random(20, 60);
      p->life = random(20, 40);
      p->type = 5;
    }
  }
  for (int i = 0; i < FX_MAX; i++) {
    EffectParticle& p = fxParts[i];
    if (!p.life || p.type != 5) continue;
    tft.drawPixel(p.x, p.y, BG_COLOR);
    tft.drawPixel(p.x+1, p.y, BG_COLOR);
    tft.drawPixel(p.x, p.y+1, BG_COLOR);
    p.x += p.vx / 100;
    p.y += p.vy / 100;
    p.life--;
    if (p.y < 0 || !p.life) { p.life = 0; continue; }
    uint8_t br = p.life * 5;
    uint16_t c = tft.color565(br, br/2, br);
    tft.drawPixel(p.x, p.y, c);
    tft.drawPixel(p.x+1, p.y, c);
    tft.drawPixel(p.x, p.y+1, c);
  }
}

// ── Effect: sword slashes (Saikai / Ryoshu) ─────

static void slashInit() {
  for (int i = 0; i < MAX_SLASHES; i++) swordSlashes[i].active = false;
}

static void fxTickSlash() {
  unsigned long now = millis();

  // Spawn slash every 1.8-4 seconds
  if (now - slashLastSpawn > (unsigned long)random(1800, 4000)) {
    slashLastSpawn = now;
    for (int i = 0; i < MAX_SLASHES; i++) {
      if (!swordSlashes[i].active) {
        SwordSlash& s = swordSlashes[i];
        // Ryoshu-style: fast diagonal slashes, mostly top-left to bottom-right
        // or sharp near-vertical cuts
        int slashType = random(3);
        if (slashType == 0) {
          // Diagonal slash across most of screen
          s.x0 = random(0, 80);   s.y0 = random(0, 60);
          s.x1 = random(240, 320); s.y1 = random(180, 240);
        } else if (slashType == 1) {
          // Counter-diagonal
          s.x0 = random(240, 320); s.y0 = random(0, 60);
          s.x1 = random(0, 80);    s.y1 = random(180, 240);
        } else {
          // Short sharp cut — mid-screen
          int cx = random(60, 260), cy = random(40, 200);
          int len = random(60, 140);
          float angle = random(-30, 30) * 0.05f; // near-vertical with slight tilt
          s.x0 = cx - (int)(sinf(angle)*len/2); s.y0 = cy - len/2;
          s.x1 = cx + (int)(sinf(angle)*len/2); s.y1 = cy + len/2;
        }
        s.born    = now;
        s.drawMs  = random(30, 80);   // fast draw phase
        s.holdMs  = random(200, 600); // hold before fade
        s.active  = true;
        s.drawn   = false;
        break;
      }
    }
  }

  for (int i = 0; i < MAX_SLASHES; i++) {
    SwordSlash& s = swordSlashes[i];
    if (!s.active) continue;
    unsigned long age = now - s.born;

    if (!s.drawn && age >= (unsigned long)s.drawMs) {
      // Draw the slash — bright white core with red afterglow lines
      tft.drawLine(s.x0, s.y0, s.x1, s.y1, tft.color565(255, 255, 255));
      // Offset parallel lines for afterglow/motion blur
      tft.drawLine(s.x0+1, s.y0,   s.x1+1, s.y1,   tft.color565(200, 40, 40));
      tft.drawLine(s.x0-1, s.y0,   s.x1-1, s.y1,   tft.color565(200, 40, 40));
      tft.drawLine(s.x0,   s.y0+1, s.x1,   s.y1+1, tft.color565(120, 20, 20));
      tft.drawLine(s.x0,   s.y0-1, s.x1,   s.y1-1, tft.color565(120, 20, 20));
      s.drawn = true;
    }

    if (s.drawn && age >= (unsigned long)(s.drawMs + s.holdMs)) {
      // Erase all lines
      tft.drawLine(s.x0+1, s.y0,   s.x1+1, s.y1,   BG_COLOR);
      tft.drawLine(s.x0-1, s.y0,   s.x1-1, s.y1,   BG_COLOR);
      tft.drawLine(s.x0,   s.y0+1, s.x1,   s.y1+1, BG_COLOR);
      tft.drawLine(s.x0,   s.y0-1, s.x1,   s.y1-1, BG_COLOR);
      tft.drawLine(s.x0,   s.y0,   s.x1,   s.y1,   BG_COLOR);
      s.active = false;
    }
  }
}

// ── Effect: horizontal glitch lines (String Theocracy) ─
static void fxTickGlitch() {
  static int glitchY[3]   = {-1,-1,-1};
  static unsigned long glitchErase[3] = {0,0,0};
  unsigned long now = millis();
  for (int g = 0; g < 3; g++) {
    if (glitchY[g] >= 0 && now > glitchErase[g]) {
      tft.drawFastHLine(0, glitchY[g], 320, BG_COLOR);
      glitchY[g] = -1;
    }
    if (glitchY[g] < 0 && random(5) == 0) {
      glitchY[g] = random(0, 240);
      int gx = random(0, 200), gw = random(40, 160);
      uint16_t gc = tft.color565(0, random(80,180), random(100,220));
      tft.drawFastHLine(gx, glitchY[g], gw, gc);
      // Second offset line for double-scan effect
      if (glitchY[g]+1 < 240)
        tft.drawFastHLine(gx+random(-8,8), glitchY[g]+1, gw/2, tft.color565(0,random(40,80),random(60,120)));
      glitchErase[g] = now + random(60, 250);
    }
  }
}

// ── Effect: heartbeat pulse (From a Place of Love) ─
static void fxTickHeartbeat() {
  static unsigned long beatLast = 0;
  static int beatPhase = 0;
  static int beatR = 0;
  static bool secondBeat = false;
  unsigned long now = millis();
  // Double heartbeat — lub-dub
  if (beatPhase == 0 && now - beatLast > 900) {
    beatLast = now; beatPhase = 1; beatR = 0; secondBeat = false;
  }
  if (beatPhase > 0) {
    if (beatR > 0) tft.drawCircle(160, 120, beatR-1, BG_COLOR);
    uint8_t br = max(0, 120 - beatR * 2);
    tft.drawCircle(160, 120, beatR, tft.color565(br, 0, br/4));
    // Second ring slightly behind for double-beat effect
    if (beatR > 15) {
      uint8_t br2 = max(0, 80 - (beatR-15)*3);
      tft.drawCircle(160, 120, beatR-15, tft.color565(br2, 0, br2/4));
    }
    beatR += 5;
    if (beatR > 100) {
      tft.drawCircle(160, 120, beatR-1, BG_COLOR);
      if (beatR > 115) tft.drawCircle(160, 120, beatR-16, BG_COLOR);
      beatPhase = 0;
    }
  }
}

// ── Effect: candle flame + stars (Pass On) ──────
static void fxTickPassOn() {
  // Distant stars — slow twinkle
  if (random(5) == 0) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = random(0, 320); p->y = random(0, 180);
      p->life = random(12, 32);
      p->vx = 0; p->vy = 0; p->type = 7;
    }
  }
  // Candle embers — rise from bottom centre
  if (random(4) == 0) {
    EffectParticle* p = fxSpawn();
    if (p) {
      p->x = 140 + random(-20, 20); p->y = 230;
      p->vx = random(-30, 30); p->vy = -random(40, 120);
      p->life = random(10, 22); p->type = 8;
    }
  }
  for (int i = 0; i < FX_MAX; i++) {
    EffectParticle& p = fxParts[i];
    if (!p.life) continue;
    if (p.type == 7) {
      // Twinkling star
      uint8_t phase = p.life % 8;
      uint8_t br = (phase < 4) ? 30 + phase * 40 : 190 - (phase-4)*40;
      tft.drawPixel(p.x, p.y, tft.color565(br, br, min(255,(int)br+60)));
      p.life--;
      if (!p.life) tft.drawPixel(p.x, p.y, BG_COLOR);
    } else if (p.type == 8) {
      // Candle ember
      tft.drawPixel(p.x, p.y, BG_COLOR);
      p.x += p.vx / 100;
      p.y += p.vy / 100;
      p.vy += 8;
      p.life--;
      if (p.y < 0 || p.y >= 240 || !p.life) { p.life = 0; continue; }
      uint8_t g = min(255, (int)(22 - p.life) * 12);
      tft.drawPixel(p.x, p.y, tft.color565(255, g, 0));
    }
  }
}

static void fxTickEffect() {
  if (millis() - fxLastTick < 40) return;
  fxLastTick = millis();
  switch (activeSongIdx) {
    case 0:  break;
    case 1:  fxTickFire();     break;
    case 2:  fxTickFeathers(); break;
    case 3:  fxTickStars();    break;
    case 4:  fxTickGlass();    break;
    case 5:  fxTickWaves();    break;
    case 6:  fxTickBubbles();  break;
    case 7:  fxTickSlash();    break;
    case 8:  fxTickGlitch();   break;
    case 9:  fxTickHeartbeat(); break;
    case 10: fxTickPassOn();   break;
  }
}

void tickLyricsScreen() {
  // Effects run every call at their own 40ms rate — not gated by lyric speed
  fxTickEffect();

  static unsigned long lastLyricTick = 0;
  if (millis() - lastLyricTick < (unsigned long)cfg_glyphSpinMs) return;
  lastLyricTick = millis();

  // Advance line index for blank lines
  const char* curLine = activeLyricsLines[lyricsLineIdx];
  if (strlen(curLine) == 0) {
    // Blank line = stanza break — clear both slots and pause
    static int blankTimer = 0;
    blankTimer += cfg_glyphSpinMs;
    if (blankTimer > 800) {
      blankTimer = 0;
      lyricsLineIdx = (lyricsLineIdx + 1) % activeLyricsCount;
      tft.fillScreen(BG_COLOR);
      lyricsSlots[0].active = false;
      lyricsSlots[1].active = false;
      lyricsActiveSlot = 0;
      lyricsVisibleLines = 0;
      lyricsLoadSlot(&lyricsSlots[0], activeLyricsLines[lyricsLineIdx], 0);
    }
    return;
  }

  tft.startWrite();

  // Draw both slots
  LyricsSlot* cur = &lyricsSlots[lyricsActiveSlot];
  LyricsSlot* old = &lyricsSlots[1 - lyricsActiveSlot];

  // Tick old slot erase
  if (old->active && old->erasing) {
    if (old->eraseMs == 0) {
      // One-time clear — compute actual rendered bounds including diagonal drift
      int minY = 240, maxY = 0;
      for (int r = 0; r < old->rowCount; r++) {
        int rlen = strlen(old->wrapped[r]);
        int rowBaseY = old->baseY + r * (old->ch + abs(old->dir.dy));
        // Per-char dy spreads the row vertically
        int rowTopY  = rowBaseY + min(0, (rlen-1) * old->dir.dy) - 3;
        int rowBotY  = rowBaseY + max(0, (rlen-1) * old->dir.dy) + old->ch + 3;
        minY = min(minY, rowTopY);
        maxY = max(maxY, rowBotY);
      }
      minY = max(0, minY);
      maxY = min(240, maxY);
      tft.fillRect(0, minY, 320, maxY - minY, BG_COLOR);
      int sn = (old == &lyricsSlots[0]) ? 0 : 1;
      lyricsWaveInit[sn] = false;
    }
    old->eraseMs += cfg_glyphSpinMs;
    if (old->eraseMs > 2200) {
      old->active = false;
      int sn = (old == &lyricsSlots[0]) ? 0 : 1;
      lyricsWaveInit[sn] = false;
    }
  }

  // Tick current slot reveal
  if (cur->active) {
    if (!cur->holding) {
      if (cur->reveal < cur->totalChars) cur->reveal++;
      else { cur->holding = true; cur->holdTimer = 0; }
    } else {
      cur->holdTimer += cfg_glyphSpinMs;
      bool isLast = (lyricsLineIdx == activeLyricsCount - 1);
      int hold = isLast ? 999999 : 1600;
      if (!isLast && cur->holdTimer > hold) {
        lyricsLineIdx = (lyricsLineIdx + 1) % activeLyricsCount;

        // Start erasing current slot
        cur->erasing = true;
        cur->eraseMs = 0;

        // Load next line into other slot
        int nextSlot = 1 - lyricsActiveSlot;
        lyricsLoadSlot(&lyricsSlots[nextSlot], activeLyricsLines[lyricsLineIdx], nextSlot);
        lyricsActiveSlot = nextSlot;
      }
    }
  }

  // Redraw both slots
  lyricsDrawSlot(old);
  lyricsDrawSlot(cur);

  tft.endWrite();
}


void drawIdleAnim();
void drawGamePicker();
void loadPrescript();
void drawSongSelect();
void lyricsSelectSong(int idx);
void showStoredPrescript();
void cyclePrescript(int menuIndex);
void tarotSwipeHistory(bool up);
void drawTimedCountdown(uint16_t bg);
void timedAssignNew();
void showResult(bool pass);
void saveState();
void saveStateFast();
void markStateDirty();
void markDeckDirty();
void applyBrightness();

// =================================================
// ROUTER
// =================================================
void goTo(Screen target) {
  previousScreen = screen;
  screen = target;
  lastNavTime = millis();
  _swallowNextRelease = true;   // discard the touch-release that triggered this navigation
  if (previousScreen == MENU_4 && target != MENU_4) timerBlinkCount = 0;

  bool toPreScript   = (screen == MENU_GAME || screen == MENU_1 ||
                        screen == MENU_2    || screen == MENU_3);

  if (toPreScript) {
    // Always show the stored prescript for this menu. It only changes
    // when passed/failed (via cyclePrescript -> loadPrescript on next entry).
    showStoredPrescript();
  }
  if (screen == MENU_4)               drawTimedMenu();
  if (screen == RESULT)               drawResult();
  if (screen == MAIN_MENU)            drawMenu();
  if (screen == SETTINGS)             drawSettings();
  if (screen == SETTINGS_TIMER)       drawSettingsTimer();
  if (screen == SETTINGS_IDLE)        drawSettingsIdle();
  if (screen == SETTINGS_BRIGHT)      drawSettingsBright();
  if (screen == SETTINGS_SPEED)       drawSettingsSpeed();
  if (screen == SETTINGS_DIFFICULTY)  drawSettingsDifficulty();
  if (screen == SETTINGS_CATEGORIES)  drawSettingsCategories();
  if (screen == RESET_CONFIRM)        drawResetConfirm();
  if (screen == HISTORY_SCREEN)       drawHistoryScreen();
  if (screen == DICE_ROLL)            startDiceRoll();
  if (screen == IDLE_ANIM)            drawIdleAnim();
  if (screen == GAME_PICKER)          gamePickerStart();
  if (screen == CYBER_MENU)           { cyberScrollY = 0; drawCyberMenu(); }
  if (screen == CYBER_WIFI)           cyberWifiScan();
  if (screen == CYBER_BLE)            cyberBLEDraw();
  if (screen == CYBER_BT)             cyberBTRun();
  if (screen == CYBER_QR)             cyberQRDraw();
  if (screen == TOOLS_MENU)           { toolsScrollY = 0; drawToolsMenu(); }
  if (screen == TOOL_COIN)            { toolCoinDraw(); }
  if (screen == TOOL_TAROT)           { toolTarotInit(); toolTarotDraw(); }
  if (screen == TOOL_ORACLE)          { toolOracleInit(); toolOracleDraw(); }
  if (screen == TOOL_TIMER)           toolCDDraw();
  if (screen == TOOL_NAMES)           { toolGenInit(); toolGenDraw(); }
  if (screen == TOOL_CIPHER)          { toolCipherInit(); toolCipherDraw(); }
  if (screen == LYRICS_SCREEN)        { drawLyricsScreen(); }
  if (screen == SONG_SELECT)          { drawSongSelect(); }
}

// =================================================
// BRIGHTNESS
// =================================================
void applyBrightness() {
  ledcWrite(TFT_BL, BL_LEVELS[brightnessLevel]);
}

// =================================================
// LOGO
// =================================================
void drawLogo() {
  const uint16_t* src = logoBuf ? logoBuf : index_565;
  Serial.println("drawLogo start");
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 96, src);
  tft.setSwapBytes(false);
  tft.fillRect(0, 88, 320, 8, BG_COLOR);
  logoBottomY = 88 + 4;
  Serial.println("drawLogo done");
}

// =================================================
// SESSION TIMER HELPER
// =================================================
void drawSessionTimer(uint16_t textColor) {
  unsigned long elapsed = (millis() - sessionStart) / 1000;
  unsigned int  hh = elapsed / 3600;
  unsigned int  mm = (elapsed % 3600) / 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02u:%02u", hh, mm);
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(50, 70, 100), BG_COLOR);
  tft.setCursor((320 - strlen(buf) * 6) / 2, 220);
  tft.print(buf);
}

// Draw the timed prescript countdown top-right on any prescript screen.
// Handles the 3-blink animation when the timer resets.
void drawTimedCountdown(uint16_t bg) {
  const int x = 240, y = 6, w = 76;   // right-aligned zone

  // Advance blink state
  if (timerBlinkCount > 0 && millis() - timerBlinkLast > 200) {
    timerBlinkLast = millis();
    timerBlinkOn   = !timerBlinkOn;
    timerBlinkCount--;
  }

  // During blink-off phase, erase and return
  if (timerBlinkCount > 0 && !timerBlinkOn) {
    tft.fillRect(x - 2, y - 1, w + 4, 10, bg);
    return;
  }

  if (!timedActive) {
    // No active timer  -  show dim dashes
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(40, 55, 75), bg);
    tft.setCursor(x, y); tft.print("--:--");
    return;
  }

  unsigned long dur     = timerDurationMs();
  unsigned long elapsed;
  if (timedPaused)      elapsed = dur - timedPauseRem;
  else                  elapsed = millis() - timedStart;

  unsigned long remSec = (elapsed < dur) ? (dur - elapsed) / 1000 : 0;
  unsigned int  rh = remSec / 3600;
  unsigned int  rm = (remSec % 3600) / 60;
  unsigned int  rs = remSec % 60;

  char tbuf[12];
  if (rh > 0) snprintf(tbuf, sizeof(tbuf), "%u:%02u:%02u", rh, rm, rs);
  else        snprintf(tbuf, sizeof(tbuf), "%u:%02u", rm, rs);

  // Colour: green when passed, urgent when <25%, normal otherwise
  uint16_t col;
  if (timedPassed)              col = tft.color565(0, 130, 100);
  else if (remSec < dur / 4000) col = tft.color565(220, 80, 40);
  else                          col = tft.color565(0, 180, 140);

  if (timedPaused) col = tft.color565(180, 140, 40);   // amber when paused

  tft.setTextSize(1);
  // Right-align within zone
  int tx = 316 - (int)strlen(tbuf) * 6;
  tft.fillRect(tx - 2, y - 1, 316 - tx + 4, 10, bg);   // erase old
  tft.setTextColor(col, bg);
  tft.setCursor(tx, y); tft.print(tbuf);
}

// Shared word-wrap into a char[rows][32] buffer.
// Returns number of lines. src = input string, maxCols = chars per line.
int wrapText(const char* src, char out[][32], int maxCols) {
  int lc = 0;
  char word[32] = "", line[32] = "";
  int slen = strlen(src);
  for (int i = 0; i <= slen; i++) {
    if (i == slen || src[i] == ' ') {
      char test[64];
      if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
      else         strncpy(test, word, 63);
      test[63] = '\0';
      if ((int)strlen(test) > maxCols) {
        strncpy(out[lc++], line, 31); out[lc-1][31]='\0';
        strncpy(line, word, 31);
      } else {
        strncpy(line, test, 31);
      }
      word[0] = '\0';
    } else {
      int wl = strlen(word);
      if (wl < 31) { word[wl]=src[i]; word[wl+1]='\0'; }
    }
  }
  if (line[0]) { strncpy(out[lc++], line, 31); out[lc-1][31]='\0'; }
  return lc;
}

// Wide variant  -  54-char buffers for full-width size-1 text (53 chars max at 6px/char)
int wrapTextWide(const char* src, char out[][54], int maxCols) {
  int lc = 0;
  char word[54] = "", line[54] = "";
  int slen = strlen(src);
  for (int i = 0; i <= slen; i++) {
    if (i == slen || src[i] == ' ') {
      char test[108];
      if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
      else         strncpy(test, word, 107);
      test[107] = '\0';
      if ((int)strlen(test) > maxCols) {
        strncpy(out[lc++], line, 53); out[lc-1][53]='\0';
        strncpy(line, word, 53);
      } else {
        strncpy(line, test, 53);
      }
      word[0] = '\0';
    } else {
      int wl = strlen(word);
      if (wl < 53) { word[wl]=src[i]; word[wl+1]='\0'; }
    }
  }
  if (line[0]) { strncpy(out[lc++], line, 53); out[lc-1][53]='\0'; }
  return lc;
}

// Helper  -  map a prescript Screen to its menu index 0-4
int screenToMenuIdx(Screen s) {
  switch (s) {
    case MENU_GAME: return 0;
    case MENU_1:    return 1;
    case MENU_2:    return 2;
    case MENU_4:    return 4;
    default:        return 3;
  }
}

// =================================================
// GENERATIVE PRESCRIPT ENGINES
// Each menu has a generator that builds a unique
// prescript from component pools by difficulty.
// Inspired by nyos.dev/prescript format:
//   [condition], [firstAct] and find the [Nth]
//   [adjective] [noun] you see and [secondAct].
//   [closing]
// =================================================

// ── Ordinal suffix helper ─────────────────────────
static void ordinal(int n, char* buf, int buflen) {
  const char* suf = "th";
  int m = n % 100;
  if (m < 11 || m > 13) {
    switch (n % 10) {
      case 1: suf="st"; break;
      case 2: suf="nd"; break;
      case 3: suf="rd"; break;
    }
  }
  snprintf(buf, buflen, "%d%s", n, suf);
}

// ─────────────────────────────────────────────────
// MENU 1: CITY — generative (nyos.dev format)
// ─────────────────────────────────────────────────
static const char* cityCondEasy[] = {
  "before noon","after noon","before your next meal",
  "upon waking up","within an hour","within a day",
  "before midnight","before sunset","after sunrise"
};
static const char* cityCondNorm[] = {
  "after midnight","before noon","after noon","before midnight",
  "after sunrise","before sunset","after sunset","within a week",
  "within a day","within an hour","within a minute",
  "when the clock strikes noon","upon waking up","before your next meal",
  "while standing in an alleyway","when the next train passes",
  "when you feel a cold breeze","while the sun is at its highest"
};
static const char* cityCondHard[] = {
  "while bleeding","upon hearing a scream","in total darkness",
  "at 3:33 AM","at exactly midnight","during the next rainstorm",
  "when you next see blood","while the sun is at its highest",
  "upon hearing a siren","while standing in an alleyway"
};
static const char* cityAct1Easy[] = {
  "look away","close your eyes","stop moving","hold your breath",
  "count to ten aloud","look up at the sky","smile widely",
  "drop what you are holding","take off your left shoe","empty your pockets"
};
static const char* cityAct1Norm[] = {
  "look away","close your eyes","cover your ears","hold your breath",
  "silence your tongue","stop moving","bite your tongue","clench your fists",
  "hum a forgotten song","drop what you are holding","take off your left shoe",
  "turn around three times","look up at the sky","spit on the ground",
  "count to ten aloud","kneel on the ground","smile widely","empty your pockets"
};
static const char* cityAct1Hard[] = {
  "draw blood from your palm","bite your tongue",
  "kneel on the ground","clench your fists",
  "cover your ears","spit on the ground",
  "hum a forgotten song","turn around twice"
};
static const char* cityAdjEasy[] = {
  "kind","sociable","amicable","optimistic","laughing","bright",
  "wealthy","singing","lost","sleeping","short","tall","thin"
};
static const char* cityAdjNorm[] = {
  "meaningful","hateful","beautiful","despicable","sincere","deceitful",
  "kind","abhorrent","loving","cruel","generous","selfish","compassionate",
  "callous","pitiful","taciturn","loquacious","sociable","reclusive",
  "amicable","hostile","optimistic","pessimistic","weeping","laughing",
  "short","tall","thin","thick","bright","one-eyed","starving","wealthy",
  "trembling","sleeping","lost","injured","blind","arrogant","exhausted",
  "singing","limping","silent","furious","terrified"
};
static const char* cityAdjHard[] = {
  "injured","blind","furious","terrified",
  "exhausted","one-eyed","starving",
  "trembling","limping","weeping","stained"
};
static const char* cityNounEasy[] = {
  "stranger","friend","acquaintance","passenger","merchant",
  "musician","cook","child","parent","beggar"
};
static const char* cityNounNorm[] = {
  "outsider","stranger","friend","acquaintance","Fixer",
  "passenger","child","parent","butcher","merchant","musician",
  "guard","beggar","informant","messenger","cook","thief","officer",
  "mechanic","doctor","stray dog"
};
static const char* cityNounHard[] = {
  "Fixer","boss","Sweeper","guard",
  "informant","Proxy","Proselyte","thief",
  "officer","corpse","butcher"
};
static const char* cityAct2Easy[] = {
  "befriend them","help them","sit down with them","bow to them",
  "ask for directions","feed them","play a game with them",
  "buy them a drink","sing them a lullaby","applaud them",
  "apologize to them"
};
static const char* cityAct2Norm[] = {
  "attack them","embrace them","play with them","harrass them",
  "discourage them","give a gift to them","steal from them",
  "help them","follow them","befriend them","stalk them","annoy them",
  "play a game with them","sit down with them","buy them a drink",
  "whisper a secret to them","hand them your weapon",
  "sing them a lullaby","take their shoes","bow to them",
  "swap clothes with them","applaud them","apologize to them",
  "ask for directions","feed them","force them to sing"
};
static const char* cityAct2Hard[] = {
  "attack them","maim them","bind them",
  "steal from them","stalk them","harrass them",
  "break their legs","take their shoes",
  "force them to sing","hand them a weapon"
};
static const char* cityCloseEasy[] = {
  "remember their face.","do not forget this.","then, walk away.",
  "wash your hands.","close your eyes.","pray for them.","smile.",
  "wait.","do not look back."
};
static const char* cityCloseNorm[] = {
  "remember their face.","remember their name.","regret this.",
  "do not forget this.","do not speak.","wait.",
  "etch this into your memory.","tell somebody what you did.",
  "let out a scream.","let out a laugh.","let out a sob.",
  "mourn this.","celebrate this.","do not look back.",
  "then, walk away.","leave no trace.","deny everything.",
  "the Prescript is absolute.","wash your hands.","close your eyes.",
  "never speak of this.","pretend nothing happened.",
  "pray for them.","smile."
};
static const char* cityCloseHard[] = {
  "etch this into your skin.","deny everything.",
  "leave no trace.","never speak of this.",
  "let out a scream.","regret this.",
  "do not speak.","do not look back."
};


void generateCityPresc(char* out, int outlen, uint8_t diff) {
  int numRange = (diff==0) ? 20 : 100;
  int num = random(1, numRange+1);
  char ord[8]; ordinal(num, ord, sizeof(ord));

  const char* cond;  const char* act1;
  const char* adj;   const char* noun;
  const char* act2;  const char* close;

  if (diff == 0) {
    cond  = cityCondEasy[random(9)];
    act1  = cityAct1Easy[random(10)];
    adj   = cityAdjEasy[random(13)];
    noun  = cityNounEasy[random(10)];
    act2  = cityAct2Easy[random(11)];
    close = cityCloseEasy[random(9)];
    snprintf(out, outlen, "%s, %s and find the %s %s %s you see and %s. %s",
      cond, act1, ord, adj, noun, act2, close);
  } else if (diff == 2) {
    // Hard uses shorter format without act1.
    // Retry up to 8 times to keep output under 120 chars (5 lines at 24 cols).
    for (int _try = 0; _try < 8; _try++) {
      cond  = cityCondHard[random(10)];
      adj   = cityAdjHard[random(11)];
      noun  = cityNounHard[random(11)];
      act2  = cityAct2Hard[random(10)];
      close = cityCloseHard[random(8)];
      snprintf(out, outlen, "%s, find the %s %s %s you see and %s. %s",
        cond, ord, adj, noun, act2, close);
      if ((int)strlen(out) <= 110) break;  // ~4-5 lines at 24 cols
    }
  } else {
    cond  = cityCondNorm[random(18)];
    act1  = cityAct1Norm[random(18)];
    adj   = cityAdjNorm[random(45)];
    noun  = cityNounNorm[random(21)];
    act2  = cityAct2Norm[random(26)];
    close = cityCloseNorm[random(24)];
    snprintf(out, outlen, "%s, %s and find the %s %s %s you see and %s. %s",
      cond, act1, ord, adj, noun, act2, close);
  }
}

// ─────────────────────────────────────────────────
// MENU 0: GAME — generative
// Challenge phrases assembled from game-context pools
// ─────────────────────────────────────────────────
static const char* gameCondEasy[] = {
  "in your next session","before the end of tonight",
  "in the next game you load","before you save",
  "within the next hour of play","before the next checkpoint"
};
static const char* gameCondNorm[] = {
  "in your next session","before the end of tonight",
  "in the next game you load","without reloading a save",
  "before you save","within the next hour of play",
  "in the next match","before the next checkpoint",
  "before any fast travel","across the next three sessions",
  "before leaving the current area"
};
static const char* gameCondHard[] = {
  "without dying","without using abilities",
  "without a map","on your first attempt",
  "without saving","without fast travel",
  "in a single sitting","on the hardest difficulty",
  "without spending resources","without retreating"
};
static const char* gameObjEasy[] = {
  "complete one quest","win one match","defeat one enemy",
  "reach a new area","collect one item","talk to one NPC",
  "use one ability","craft one item","explore one room",
  "finish one level"
};
static const char* gameObjNorm[] = {
  "complete a quest without fast travel","win a match using only one weapon",
  "defeat a boss using a strategy you have not used before",
  "reach an area you have not visited this session",
  "collect a full set of one item type","talk to every NPC in one area",
  "use only abilities you rarely use for one full encounter",
  "craft something using only found materials","clear a dungeon without retreating",
  "finish a level with a rank you have not achieved before",
  "complete an objective using only your weakest loadout",
  "defeat three enemies in a row without taking damage",
  "find a piece of lore you have not read before",
  "complete a side quest you have been ignoring"
};
static const char* gameObjHard[] = {
  "defeat the hardest enemy without dying",
  "complete a full run without consumables",
  "win without using your primary weapon",
  "clear an area using only unused abilities",
  "complete a mission with all optional objectives",
  "defeat a boss first attempt with no prior knowledge",
  "finish a session without retreating once",
  "complete a dungeon with the worst equipment",
  "win three consecutive matches unchanged",
  "complete an objective without the map"
};
static const char* gameCloseEasy[] = {
  "note the result.","do not reload if you fail.",
  "remember how it felt.","move on immediately after."
};
static const char* gameCloseNorm[] = {
  "note the result.","do not reload if you fail.",
  "remember how it felt.","move on immediately after.",
  "tell someone what happened.","do not attempt it again tonight.",
  "accept the outcome.","screenshot the result."
};
static const char* gameCloseHard[] = {
  "do not reload if you fail.","accept the outcome without complaint.",
  "do not attempt it again until tomorrow.",
  "if you fail, close the game immediately.",
  "do not use any assists or guides."
};

void generateGamePresc(char* out, int outlen, uint8_t diff) {
  const char* cond;  const char* obj;  const char* close;
  if (diff == 0) {
    cond  = gameCondEasy[random(6)];
    obj   = gameObjEasy[random(10)];
    close = gameCloseEasy[random(4)];
  } else if (diff == 2) {
    cond  = gameCondHard[random(10)];
    obj   = gameObjHard[random(10)];
    close = gameCloseHard[random(5)];
  } else {
    cond  = gameCondNorm[random(11)];
    obj   = gameObjNorm[random(14)];
    close = gameCloseNorm[random(8)];
  }
  snprintf(out, outlen, "%s, %s. %s", cond, obj, close);
}

// ─────────────────────────────────────────────────
// MENU 2: CON — generative
// Social/convention challenge format
// ─────────────────────────────────────────────────
static const char* conCondEasy[] = {
  "before the next panel","within the next hour",
  "before you leave today","before noon",
  "before your next meal","within the next 30 minutes"
};
static const char* conCondNorm[] = {
  "before the next panel","within the next hour",
  "before you leave today","before noon","before your next meal",
  "within the next 30 minutes","before you sit down again",
  "before speaking to anyone you already know",
  "before buying anything","within the next two hours"
};
static const char* conCondHard[] = {
  "before the day ends","without using your phone",
  "before you eat","within the next 20 minutes",
  "before leaving the current floor","without planning it in advance",
  "before the next hour mark","in the next 10 minutes"
};
static const char* conAct1Easy[] = {
  "find one cosplayer","locate one item","speak to one stranger",
  "observe one costume","photograph one display",
  "sit somewhere new","find one artist"
};
static const char* conAct1Norm[] = {
  "find a cosplayer from a series you do not recognise",
  "locate an item you did not plan to find",
  "speak to a stranger who is alone","observe a costume made entirely by hand",
  "photograph something that will not exist tomorrow",
  "sit somewhere you have not sat today",
  "find an artist whose work you have never seen",
  "locate the most elaborate prop you can see",
  "find someone in a group cosplay and identify all characters",
  "speak to a vendor without intending to buy anything"
};
static const char* conAct1Hard[] = {
  "find a cosplay from a niche series",
  "find someone who made every piece themselves",
  "learn a stranger's name without asking",
  "make an exhausted person smile",
  "find the oldest person here and speak to them"
};
static const char* conAct2Easy[] = {
  "compliment them","take a photograph","say hello","wave",
  "ask one question","sit near them","observe them for one minute"
};
static const char* conAct2Norm[] = {
  "compliment something specific about them",
  "take a photograph if they allow it",
  "ask what series they are from","learn one thing about their costume",
  "recommend something to them","share something about your own interest",
  "ask how long their costume took to make",
  "find out where they are from","ask for one piece of advice"
};
static const char* conAct2Hard[] = {
  "have a conversation lasting at least 5 minutes",
  "find a common interest before leaving","exchange contact information",
  "collaborate on something before the day ends",
  "bring them to meet someone else you have spoken to today"
};
static const char* conCloseEasy[] = {
  "then move on.","do not linger.","smile and leave.",
  "remember their costume."
};
static const char* conCloseNorm[] = {
  "then move on.","do not linger.","remember what they said.",
  "find one more before leaving.","tell someone about it.",
  "photograph the moment if you can.","remember their costume.",
  "note the series they were from."
};
static const char* conCloseHard[] = {
  "do not use your phone during this.","approach without hesitation.",
  "complete this before any other task.","do not plan what you will say."
};

void generateConPresc(char* out, int outlen, uint8_t diff) {
  const char* cond; const char* act1; const char* act2; const char* close;
  if (diff == 0) {
    cond  = conCondEasy[random(6)];
    act1  = conAct1Easy[random(7)];
    act2  = conAct2Easy[random(7)];
    close = conCloseEasy[random(4)];
  } else if (diff == 2) {
    cond  = conCondHard[random(8)];
    act1  = conAct1Hard[random(5)];
    act2  = conAct2Hard[random(5)];
    close = conCloseHard[random(4)];
  } else {
    cond  = conCondNorm[random(10)];
    act1  = conAct1Norm[random(10)];
    act2  = conAct2Norm[random(9)];
    close = conCloseNorm[random(8)];
  }
  snprintf(out, outlen, "%s, %s and %s. %s", cond, act1, act2, close);
}

// ─────────────────────────────────────────────────
// MENU 3: TASK — generative
// Productivity/habit challenge format
// ─────────────────────────────────────────────────
static const char* taskCondEasy[] = {
  "before noon","before your next break","within the next hour",
  "before checking your phone","before your next meal",
  "before leaving your current location"
};
static const char* taskCondNorm[] = {
  "before noon","before your next break","within the next hour",
  "before checking your phone","before your next meal",
  "before leaving your current location","before end of day",
  "within the next 30 minutes","before speaking to anyone",
  "before opening any application","before your next distraction"
};
static const char* taskCondHard[] = {
  "without any breaks","in one sitting","before anything else today",
  "without checking your phone","in complete silence",
  "without any music or audio","before you eat",
  "without planning in advance","in the next 15 minutes"
};
static const char* taskObjEasy[] = {
  "write down one thing you need to do",
  "clear one item from your task list",
  "reply to one message you have been putting off",
  "drink a glass of water",
  "spend five minutes tidying one surface",
  "close one tab you do not need",
  "write one sentence of something you have been avoiding",
  "spend two minutes doing nothing intentionally",
  "identify the most important thing you need to do today",
  "take three slow deliberate breaths"
};
static const char* taskObjNorm[] = {
  "complete the task you have been avoiding longest",
  "write for 15 uninterrupted minutes",
  "clear your inbox of everything older than a week",
  "spend 25 minutes on one task without switching",
  "identify three things you will not do today and write them down",
  "finish one thing that has been half-done for more than a day",
  "spend 10 minutes planning the rest of your day in writing",
  "delete or archive 20 things you do not need",
  "complete one task from start to finish without interruption",
  "write the first line of something you have been putting off",
  "spend 15 minutes on the thing with the nearest deadline",
  "identify what is blocking your most important task and address it"
};
static const char* taskObjHard[] = {
  "complete your most important task first",
  "spend 90 minutes on one project uninterrupted",
  "finish something postponed over a week",
  "produce something tangible in two hours",
  "complete a work session without notifications",
  "write 500 words without stopping to edit",
  "resolve the thing you have avoided longest",
  "do deep work for three hours before messages"
};
static const char* taskCloseEasy[] = {
  "then rest.","then continue.","note how it felt.",
  "do not skip this."
};
static const char* taskCloseNorm[] = {
  "then rest.","then continue.","note how it felt.",
  "do not skip this.","do not move on until it is done.",
  "tell someone when it is finished.","do not reward yourself until it is complete.",
  "record the time it took."
};
static const char* taskCloseHard[] = {
  "do not stop until it is done.","no exceptions.",
  "do not move on until it is complete.",
  "this takes priority over everything else today.",
  "if you fail, restart immediately."
};

void generateTaskPresc(char* out, int outlen, uint8_t diff) {
  const char* cond; const char* obj; const char* close;
  if (diff == 0) {
    cond  = taskCondEasy[random(6)];
    obj   = taskObjEasy[random(10)];
    close = taskCloseEasy[random(4)];
  } else if (diff == 2) {
    cond  = taskCondHard[random(9)];
    obj   = taskObjHard[random(8)];
    close = taskCloseHard[random(5)];
  } else {
    cond  = taskCondNorm[random(11)];
    obj   = taskObjNorm[random(12)];
    close = taskCloseNorm[random(8)];
  }
  snprintf(out, outlen, "%s, %s. %s", cond, obj, close);
}

// Choose text size and rewrap lines[] to fit the available area.
// Called after current[] and lines[]/lineCount are set.
// availTop = logoBottomY+4, availBot = 196 (above PASS/FAIL).
// Always wraps at size-2 column width (24 cols). Long text clips at availBot=214.
static void choosePrescriptSize() {
  prescriptTextSize = 2;
  lineCount = wrapText(current, lines, 24);
}

// =================================================
// LOAD ACTIVE PRESCRIPT (regular menus 0-3)
// Generates a NEW prescript for the current screen's menu and stores
// it in the per-menu slot. Called only on pass/fail (cycle) and on
// first entry to a menu that has no prescript yet.
// =================================================
void loadPrescript() {
  int menuIndex = screenToMenuIdx(screen);
  if (menuIndex > 3) menuIndex = 3;
  uint8_t diff  = menuDiffFor(menuIndex);

  bool rare = (diff > 0) && (random(20) == 0);
  menuIsRare[menuIndex] = rare;

  if (rare) {
    const char** rs; int rc;
    if      (menuIndex==0){rs=rareGame;rc=RARE_GAME_COUNT;}
    else if (menuIndex==1){rs=rareCity;rc=RARE_CITY_COUNT;}
    else if (menuIndex==2){rs=rareCon; rc=RARE_CON_COUNT;}
    else                  {rs=rareTask;rc=RARE_TASK_COUNT;}
    snprintf(menuCurrent[menuIndex], 256, "* %s *", rs[random(rc)]);
    histPush(menuIndex, -1);
  } else {
    // Use generative engine for each menu — infinite unique prescripts
    char generated[256];
    switch (menuIndex) {
      case 0: generateGamePresc(generated, sizeof(generated), diff); break;
      case 1: generateCityPresc(generated, sizeof(generated), diff); break;
      case 2: generateConPresc( generated, sizeof(generated), diff); break;
      default:generateTaskPresc(generated, sizeof(generated), diff); break;
    }
    // All prescripts display in uppercase
    for (int _i = 0; generated[_i]; _i++) generated[_i] = toupper((unsigned char)generated[_i]);
    snprintf(menuCurrent[menuIndex], 256, "_%s_", generated);
    histPush(menuIndex, 0);   // generative: no deck index, use 0 as placeholder
  }

  menuRevealIndex[menuIndex] = 0;
  menuLineCount[menuIndex]   = wrapText(menuCurrent[menuIndex], menuLines[menuIndex], 24);
  menuHasPrescript[menuIndex] = true;

  // Copy into the active draw buffers
  strncpy(current, menuCurrent[menuIndex], sizeof(current));
  current[sizeof(current)-1] = '\0';
  revealIndex = 0;
  lineCount   = menuLineCount[menuIndex];
  for (int l = 0; l < lineCount; l++) {
    strncpy(lines[l], menuLines[menuIndex][l], 32);
    lines[l][31] = '\0';
  }
  isRareEvent = rare;
  choosePrescriptSize();

  tft.fillScreen(BG_COLOR);
  drawLogo();
  drawPrescript();   // draw immediately — no blank flash
}

// Show the existing prescript for the current screen without regenerating.
// Called when swiping between menus or returning from result.
void showStoredPrescript() {
  int menuIndex = screenToMenuIdx(screen);
  if (menuIndex > 3) menuIndex = 3;

  if (!menuHasPrescript[menuIndex]) {
    loadPrescript();
    return;
  }

  // Restore the per-menu state into the draw buffers
  strncpy(current, menuCurrent[menuIndex], sizeof(current));
  current[sizeof(current)-1] = '\0';
  revealIndex = 0;    // always re-scramble when entering the screen
  lineCount   = menuLineCount[menuIndex];
  for (int l = 0; l < lineCount; l++) {
    strncpy(lines[l], menuLines[menuIndex][l], 32);
    lines[l][31] = '\0';
  }
  isRareEvent = menuIsRare[menuIndex];
  choosePrescriptSize();

  // Sync the activeSet pointer for any code that still reads it
  uint8_t diff = menuDiffFor(menuIndex);
  if (diff == 0) {
    const char** e[4] = {easyGame,easyCity,easyCon,easyTask};
    const int    c[4] = {EASY_GAME_COUNT,EASY_CITY_COUNT,EASY_CON_COUNT,EASY_TASK_COUNT};
    activeSet = e[menuIndex]; activeCount = c[menuIndex];
  } else if (diff == 2) {
    const char** h[4] = {hardGame,hardCity,hardCon,hardTask};
    const int    c[4] = {HARD_GAME_COUNT,HARD_CITY_COUNT,HARD_CON_COUNT,HARD_TASK_COUNT};
    activeSet = h[menuIndex]; activeCount = c[menuIndex];
  } else {
    const char** n[4] = {gamePrescripts,cityPrescripts,conPrescripts,taskPrescripts};
    const int    c[4] = {GAME_POOL_COUNT,CITY_COUNT,CON_COUNT,TASK_COUNT};
    activeSet = n[menuIndex]; activeCount = c[menuIndex];
  }

  tft.fillScreen(BG_COLOR);
  drawLogo();
  drawPrescript();   // draw immediately so no blank flash between logo and text
}
// the per-menu slot and trigger a new card.
void cyclePrescript(int menuIndex) {
  // Force a new draw on next entry
  menuHasPrescript[menuIndex] = false;
}

// =================================================
// TIMED MENU  -  assign new prescript
// =================================================
void timedAssignNew() {
  // Build list of enabled pools
  int enabledPools[4]; int enabledCount = 0;
  for (int i = 0; i < 4; i++) if (poolEnabled(i)) enabledPools[enabledCount++] = i;
  if (enabledCount == 0) { enabledPools[0]=0; enabledCount=1; }  // safety

  int pool     = enabledPools[random(enabledCount)];
  timedPool    = pool;

  // Apply difficulty  -  uses timed menu's own passCount[4] against global thresholds
  uint8_t diff = menuDiffFor(4);
  isRareEvent  = (diff > 0) && (random(20) == 0);
  timedIsRare  = isRareEvent;

  if (isRareEvent) {
    const char** rareSet; int rareCount;
    if      (pool == 0) { rareSet = rareGame; rareCount = RARE_GAME_COUNT; }
    else if (pool == 1) { rareSet = rareCity; rareCount = RARE_CITY_COUNT; }
    else if (pool == 2) { rareSet = rareCon;  rareCount = RARE_CON_COUNT; }
    else                { rareSet = rareTask; rareCount = RARE_TASK_COUNT; }
    strncpy(timedCurrent, rareSet[random(rareCount)], sizeof(timedCurrent)-1);
    timedCurrent[sizeof(timedCurrent)-1] = '\0';
    timedCardIndex = -1;
    histPush(4, -1);
  } else {
    char generated[256];
    switch (pool) {
      case 0: generateGamePresc(generated, sizeof(generated), diff); break;
      case 1: generateCityPresc(generated, sizeof(generated), diff); break;
      case 2: generateConPresc( generated, sizeof(generated), diff); break;
      default:generateTaskPresc(generated, sizeof(generated), diff); break;
    }
    for (int _i = 0; generated[_i]; _i++) generated[_i] = toupper((unsigned char)generated[_i]);
    strncpy(timedCurrent, generated, sizeof(timedCurrent)-1);
    timedCurrent[sizeof(timedCurrent)-1] = '\0';
    timedCardIndex = 0;
    histPush(4, 0);
  }

  timedStart  = millis();
  timedActive = true;
  timedPassed = false;
  timedPaused = false;
  timedRevealIndex = 0;
  timedLastReveal  = millis();
  // Start blink sequence  -  3 blinks visible on all prescript screens
  timerBlinkCount = 6;   // 3 on+off pairs = 6 transitions
  timerBlinkLast  = millis();
  timerBlinkOn    = true;
  // Pre-wrap the timed text for the scramble renderer
  {
    char dtBuf[256];
    snprintf(dtBuf, sizeof(dtBuf), timedIsRare ? "* %s *" : "_%s_", timedCurrent);
    timedLineCount = wrapText(dtBuf, timedLines, 24);
  }
  markStateDirty();
}

// =================================================
// TIMED MENU COLOUR PALETTE
// Distinct from the standard prescript screens.
// Deep teal background, warm amber text.
// =================================================
#define TM_BG       tft.color565(8,   18,  20)   // very dark teal
#define TM_HEADER   tft.color565(12,  45,  45)   // medium teal strip
#define TM_TEXT     tft.color565(255, 165, 80)   // warm amber
#define TM_DIM      tft.color565(40,  90,  80)   // muted teal
#define TM_BAR_ACT  tft.color565(0,   200, 160)  // teal-green (time remaining)
#define TM_BAR_PASS tft.color565(0,   110, 90)   // dimmer teal (wait after pass)
#define TM_BAR_URG  tft.color565(220, 80,  40)   // amber-red (< 25% left)
#define TM_BAR_RAIL tft.color565(20,  45,  42)   // empty bar track
#define TM_WARN     tft.color565(200, 120, 20)   // paused / warning amber

// Helper: compute bar colour and fraction for timed screen
static void tmBarCalc(unsigned long elapsed, unsigned long dur,
                      bool passed, float& frac, uint16_t& col) {
  if (passed) {
    frac = (dur > 0) ? (float)(dur - min(elapsed, dur)) / dur : 0;
    col  = TM_BAR_PASS;
  } else {
    frac = (dur > 0) ? 1.0f - (float)min(elapsed, dur) / dur : 0;
    col  = (frac < 0.25f) ? TM_BAR_URG : TM_BAR_ACT;
  }
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
}

// =================================================
// TIMED MENU  -  draw screen (full redraw)
// =================================================
void drawTimedMenu() {
  tft.fillScreen(TM_BG);

  unsigned long now     = millis();
  unsigned long dur     = timerDurationMs();
  unsigned long elapsed;
  if (timedPaused)       elapsed = dur - timedPauseRem;
  else if (timedActive)  elapsed = now - timedStart;
  else                   elapsed = 0;

  // ── Header strip at very top ─────────────────────
  tft.fillRect(0, 0, 320, 14, TM_HEADER);
  tft.setTextSize(1);
  tft.setTextColor(TM_TEXT, TM_HEADER);
  tft.setCursor(4, 3); tft.print("TIMED");

  // Difficulty badge inline after "TIMED"
  {
    uint8_t diff = menuDiffFor(4);
    int passes   = passCount[4];
    char diffBuf[18];
    uint16_t diffCol;
    if (diff == 0) {
      snprintf(diffBuf, sizeof(diffBuf), "[E %d/%d]", passes, diffMedThreshold);
      diffCol = tft.color565(0, 160, 80);
    } else if (diff == 1) {
      snprintf(diffBuf, sizeof(diffBuf), "[N %d/%d]", passes, diffHardThreshold);
      diffCol = TM_DIM;
    } else {
      snprintf(diffBuf, sizeof(diffBuf), "[H %d]", passes);
      diffCol = tft.color565(220, 80, 40);
    }
    tft.setTextColor(diffCol, TM_HEADER);
    tft.setCursor(4 + 5*6 + 3, 3); tft.print(diffBuf);
  }

  // Theme label centred
  if (timedActive) {
    const char* poolNames[] = {"GAME","CITY","CON","TASK"};
    const char* themeName = (timedPool >= 0 && timedPool <= 3) ? poolNames[timedPool] : "TIMED";
    int tl = strlen(themeName);
    tft.setTextColor(TM_DIM, TM_HEADER);
    tft.setCursor((320 - tl*6)/2, 3); tft.print(themeName);
  }

  // Paused / rare / countdown right-aligned in header
  if (timedPaused) {
    tft.setTextColor(TM_WARN, TM_HEADER);
    tft.setCursor(320 - 7*6 - 4, 3); tft.print("[PAUSE]");
  } else if (timedIsRare) {
    tft.setTextColor(RARE_COLOR, TM_HEADER);
    tft.setCursor((320 - 9*6)/2, 3); tft.print("[INDEX]");
  } else if (timedActive) {
    unsigned long remSec = (dur > elapsed) ? (dur - elapsed) / 1000 : 0;
    char tbuf[12];
    unsigned int rh = remSec/3600, rm = (remSec%3600)/60, rs = remSec%60;
    if (rh > 0) snprintf(tbuf, sizeof(tbuf), "%u:%02u:%02u", rh, rm, rs);
    else        snprintf(tbuf, sizeof(tbuf), "%u:%02u", rm, rs);
    float frac; uint16_t bc;
    tmBarCalc(elapsed, dur, timedPassed, frac, bc);
    // Overwrite pass/fail with countdown (they overlap at right edge  -  use second line if needed)
    tft.setTextColor(timedPaused ? TM_WARN : timedPassed ? TM_BAR_PASS : bc, TM_HEADER);
    tft.setCursor(320 - strlen(tbuf)*6 - 4, 3); tft.print(tbuf);
  }
  tft.drawFastHLine(0, 14, 320, TM_TEXT);

  // ── Pass/fail count row ──────────────────────────
  tft.fillRect(0, 15, 320, 10, TM_BG);
  tft.setTextSize(1);
  char pftxt[24];
  snprintf(pftxt, sizeof(pftxt), "PASS: %d   FAIL: %d", passCount[4], failCount[4]);
  tft.setTextColor(TM_DIM, TM_BG);
  tft.setCursor((320 - strlen(pftxt)*6)/2, 16);
  tft.print(pftxt);

  // ── Logo — pushImage for speed, rows 9-87 starting at screen y=26
  {
    const uint16_t* src = logoBuf ? logoBuf : index_565;
    tft.setSwapBytes(true);
    tft.pushImage(0, 26, 320, 79, src + 9 * 320);
    tft.setSwapBytes(false);
    logoBottomY = 26 + 79 + 2;
  }

  int cY = logoBottomY;

  // ── No active prescript ──────────────────────────
  if (!timedActive) {
    tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
    tft.setCursor((320-20*6)/2,cY+18); tft.print("TAP TO BEGIN TIMER");
    unsigned long tot=timerDurationMs()/1000;
    char dbuf[22];
    if (tot>=3600) snprintf(dbuf,sizeof(dbuf),"INTERVAL: %uh %um",(unsigned)(tot/3600),(unsigned)((tot%3600)/60));
    else           snprintf(dbuf,sizeof(dbuf),"INTERVAL: %um",(unsigned)(tot/60));
    tft.setCursor((320-strlen(dbuf)*6)/2,cY+32); tft.print(dbuf);
    unsigned long se=(millis()-sessionStart)/1000;
    char sb[12]; snprintf(sb,sizeof(sb),"%02u:%02u",(unsigned)(se/3600),(unsigned)((se%3600)/60));
    tft.setTextColor(TM_DIM,TM_BG);
    tft.setCursor(320-strlen(sb)*6-4,218); tft.print(sb);
    return;
  }

  // ── Auto-fail/advance ───────────────────────────
  if (!timedPaused) {
    // Expiry is handled by tickTimedBackground — no check needed here
  }

  // ── Timer bar at bottom ──────────────────────────
  const int bY=205,bH=8,bW=300,bX=10;
  float frac; uint16_t bc;
  tmBarCalc(elapsed,dur,timedPassed,frac,bc);
  if (timedPaused) bc=TM_WARN;
  // Clear full bar region + 1px above to prevent dotted trail artifact
  tft.fillRect(bX,bY-1,bW,bH+1,TM_BAR_RAIL);
  tft.fillRect(bX,bY,(int)(bW*frac),bH,bc);
  int be=bX+(int)(bW*frac);
  if (be>bX&&be<bX+bW) tft.fillRect(be-1,bY,2,bH,bc);  // end cap, no vertical bleed

  // ── Prescript text  -  scramble reveal ────────────
  uint16_t tc = timedIsRare ? RARE_COLOR : (timedPaused ? TM_DIM : TM_TEXT);
  if (timedLineCount==0 && strlen(timedCurrent)>0) {
    char dtBuf[256];
    snprintf(dtBuf,sizeof(dtBuf),timedIsRare?"* %s *":"_%s_",timedCurrent);
    timedLineCount = wrapText(dtBuf,timedLines,24);
  }
  {
    const int cw=12, ch=18, availBot=204;
    int tH = timedLineCount * ch;
    int sY = cY + max(0, (availBot - cY - tH) / 2);
    int totalChars=0;
    for (int l=0;l<timedLineCount;l++) totalChars+=strlen(timedLines[l])+1;
    tft.setTextSize(2);
    int idx2=0;
    for (int l=0;l<timedLineCount;l++) {
      int llen=strlen(timedLines[l]);
      int lx=(320-llen*cw)/2;
      int ly=sY+l*ch;
      if (ly+ch > availBot) break;
      for (int i=0;i<llen;i++) {
        bool locked = (idx2 < timedRevealIndex);
        char c  = locked ? timedLines[l][i] : (char)random(33,126);
        uint16_t col = locked
          ? (timedPassed ? TM_BAR_PASS : tc)
          : tft.color565(30,60,70);   // dim scramble
        tft.setTextColor(col, TM_BG);
        tft.setCursor(lx+i*cw,ly); tft.print(c);
        idx2++;
      }
      idx2++;
    }
  }

  // ── Buttons ──────────────────────────────────────
  tft.setTextSize(2);
  if (!timedPassed) {
    // Dim PASS/FAIL when paused — they can't be pressed
    uint16_t passCol = timedPaused ? TM_DIM : TM_BAR_ACT;
    uint16_t failCol = timedPaused ? TM_DIM : TM_BAR_URG;
    tft.setTextColor(passCol, TM_BG); tft.setCursor(30,  216); tft.print("PASS");
    tft.setTextColor(failCol, TM_BG); tft.setCursor(214, 216); tft.print("FAIL");
    tft.setTextSize(1); tft.setTextColor(TM_DIM, TM_BG);
    const char* pl = timedPaused ? "RESUME" : "PAUSE";
    tft.setCursor((320 - strlen(pl)*6)/2, 222); tft.print(pl);
  } else {
    unsigned long ws=(dur>elapsed)?(dur-elapsed)/1000+1:0;
    char wbuf[24]; snprintf(wbuf,sizeof(wbuf),"NEXT IN %um %us",(unsigned)(ws/60),(unsigned)(ws%60));
    tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
    tft.setCursor((320-strlen(wbuf)*6)/2,222); tft.print(wbuf);
  }
  unsigned long se2=(millis()-sessionStart)/1000;
  char sb2[12]; snprintf(sb2,sizeof(sb2),"%02u:%02u",(unsigned)(se2/3600),(unsigned)((se2%3600)/60));
  tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
  tft.setCursor(320-strlen(sb2)*6-4,218); tft.print(sb2);
}

// =================================================
// TIMED MENU  -  lightweight per-second tick
// =================================================
// Background tick  -  runs every second regardless of which screen is active.
// Handles timer expiry and assigns new prescript when time is up.
// No screen drawing  -  safe to call from any screen.
void tickTimedBackground() {
  if (!timedActive || timedPaused) return;
  unsigned long now = millis();
  unsigned long dur = timerDurationMs();
  unsigned long elapsed = now - timedStart;
  if (elapsed < dur) return;   // timer still running

  // Snapshot passed state BEFORE timedAssignNew resets it
  bool wasPassed = timedPassed;

  // If the prescript was not passed, record a fail
  if (!wasPassed) {
    failCount[4]++;
    streak[4] = 0;
    // No saveState here — timedAssignNew calls it
  }
  // If it was passed, passCount was already incremented when user hit PASS
  // Nothing extra to record

  timedAssignNew();   // resets timedPassed=false, saves state
  if (screen == MENU_4) _needsTimedRedraw = true;
}

void tickTimedMenu() {
  if (!timedActive || timedPaused) return;

  unsigned long now=millis(), dur=timerDurationMs();
  unsigned long elapsed=now-timedStart;
  if (elapsed >= dur) return;

  // Periodically resend Display ON command — recovers ILI9341 sync loss
  // without clearing screen content
  static unsigned long lastDisplaySync = 0;
  if (millis() - lastDisplaySync > 10000) {
    lastDisplaySync = millis();
    tft.writecommand(0x29);   // Display ON — resyncs ILI9341 output stage
    Serial.println("Display resync");
  }

  Serial.println("tick");
  tft.startWrite();
  Serial.println("tick draw");

  // Bar
  const int bY=205,bH=8,bW=300,bX=10;
  float frac; uint16_t bc;
  tmBarCalc(elapsed,dur,timedPassed,frac,bc);
  tft.fillRect(bX,bY-1,bW,bH+1,TM_BAR_RAIL);
  tft.fillRect(bX,bY,(int)(bW*frac),bH,bc);
  int be=bX+(int)(bW*frac);
  if (be>bX&&be<bX+bW) tft.fillRect(be-1,bY,2,bH,bc);

  // Countdown
  if (!timedIsRare) {
    unsigned long remSec=(dur>elapsed)?(dur-elapsed)/1000:0;
    char tbuf[12];
    unsigned int rh=remSec/3600,rm=(remSec%3600)/60,rs=remSec%60;
    if (rh>0) snprintf(tbuf,sizeof(tbuf),"%u:%02u:%02u",rh,rm,rs);
    else      snprintf(tbuf,sizeof(tbuf),"%u:%02u",rm,rs);
    tft.setTextSize(1);
    tft.fillRect(180, 0, 136, 14, TM_HEADER);
    tft.setTextColor(timedPassed?TM_BAR_PASS:bc, TM_HEADER);
    tft.setCursor(320-strlen(tbuf)*6-4, 3); tft.print(tbuf);
    tft.drawFastHLine(180, 14, 140, TM_TEXT);
  }

  // Bottom zone
  tft.fillRect(0,212,320,28,TM_BG);
  tft.setTextSize(2);
  if (!timedPassed) {
    tft.setTextColor(TM_BAR_ACT,TM_BG); tft.setCursor(30, 216); tft.print("PASS");
    tft.setTextColor(TM_BAR_URG,TM_BG); tft.setCursor(214,216); tft.print("FAIL");
    tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
    tft.setCursor((320-5*6)/2,222); tft.print("PAUSE");
  } else {
    unsigned long ws=(dur>elapsed)?(dur-elapsed)/1000+1:0;
    char wbuf[24]; snprintf(wbuf,sizeof(wbuf),"NEXT IN %um %us",(unsigned)(ws/60),(unsigned)(ws%60));
    tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
    tft.setCursor((320-strlen(wbuf)*6)/2,222); tft.print(wbuf);
  }
  unsigned long se=(millis()-sessionStart)/1000;
  char sb[12]; snprintf(sb,sizeof(sb),"%02u:%02u",(unsigned)(se/3600),(unsigned)((se%3600)/60));
  tft.setTextSize(1); tft.setTextColor(TM_DIM,TM_BG);
  tft.setCursor(320-strlen(sb)*6-4,218); tft.print(sb);

  tft.endWrite();
  Serial.println("tick done");
}
// =================================================
void checkRewards(int menuIndex, bool pass) {
  if (pass) {
    passCount[menuIndex]++;
    streak[menuIndex]++;
    newBest = false;
    if (streak[menuIndex] > bestStreak[menuIndex]) {
      bestStreak[menuIndex] = streak[menuIndex];
      newBest = true;
    }
  } else {
    failCount[menuIndex]++;
    streak[menuIndex] = 0;
    newBest = false;
  }

  rewardText[0] = '\0';
  // Timed menu (index 4) tracks pass/fail counts but shows no reward/punishment text
  if (menuIndex == 4) { markStateDirty(); return; }
  int ri  = min(menuIndex, 3);
  int col = pass ? 0 : 1;
  int cnt = pass ? passCount[menuIndex] : failCount[menuIndex];
  if (cnt % 5 == 0 && cnt > 0) {
    int idx = (cnt / 5 - 1) % rewardCounts[ri][col];
    strncpy(rewardText, rewards[ri][col][idx], sizeof(rewardText)-1);
    rewardText[sizeof(rewardText)-1] = '\0';
  }

  if (rewardText[0] != '\0') { rewardActive = true; rewardStart = millis(); }
  markStateDirty();
}

// =================================================
// RESULT SCREEN
// =================================================
void drawResultWord(uint16_t color) {
  tft.setTextSize(3);
  tft.setTextColor(TEXT_COLOR, color);
  int cw = 18;
  int x  = (320 - (int)strlen(resultText) * cw) / 2;
  tft.setCursor(x, 90);
  tft.print(resultText);
}

bool drawRewardReveal(uint16_t color, bool hold) {
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, color);
  int cw = 6, lineH = 14;

  // Wide buffers  -  reward strings can be 50+ chars, need 54-char lines
  char rlines[10][54]; int rlineCount = wrapTextWide(rewardText, rlines, 50);

  int totalChars = 0;
  for (int l = 0; l < rlineCount; l++) totalChars += strlen(rlines[l]) + 1;

  // Vertically centre the text block between y=120 and y=235
  int blockH = rlineCount * lineH;
  int availTop = 120, availBot = 235;
  int startY = availTop + max(0, (availBot - availTop - blockH) / 2);

  int idx = 0;
  for (int l = 0; l < rlineCount; l++) {
    int len = strlen(rlines[l]);
    int x = (320 - len * cw) / 2;
    int y = startY + l * lineH;
    if (y + lineH > availBot) break;
    for (int i = 0; i < len; i++) {
      char c = hold ? rlines[l][i] : ((idx < rewardRevealIndex) ? rlines[l][i] : (char)random(33, 126));
      tft.setCursor(x + i * cw, y); tft.print(c);
      idx++;
    }
    idx++;
  }

  if (!hold && millis() - lastReveal > 45) { rewardRevealIndex++; lastReveal = millis(); }
  return rewardRevealIndex >= totalChars;
}

// New-best streak banner
void drawStreakBanner(uint16_t color, int menuIndex) {
  char buf[24];
  snprintf(buf, sizeof(buf), "STREAK %d  BEST %d", streak[menuIndex], bestStreak[menuIndex]);
  tft.setTextSize(1);
  tft.setTextColor(GOOD_COLOR, color);
  tft.setCursor((320 - strlen(buf) * 6) / 2, 170);
  tft.print(buf);
  if (newBest) {
    tft.setTextColor(RARE_COLOR, color);
    tft.setCursor((320 - 9 * 6) / 2, 182);
    tft.print("NEW BEST!");
  }
}

void drawResult() {
  if (millis() - lastAnimFrame < 30) return;
  lastAnimFrame = millis();

  uint16_t color = resultPass ? PASS_COLOR : FAIL_COLOR;

  if (!resultInitialized) {
    resultRevealIndex = 0; rewardRevealIndex = 0;
    strncpy(resultText, resultPass ? "_CleAr_" : "_FaIL_", sizeof(resultText)-1);
    resultText[sizeof(resultText)-1] = '\0';
    resultFillY       = resultPass ? 240 : 0;
    resultState       = RESULT_FILL;
    resultInitialized = true; resultFullyDrawn = false;
    tft.fillScreen(BG_COLOR);
    checkRewards(screenToMenuIdx(previousScreen), resultPass);
  }

  if (resultState == RESULT_FILL) {
    int dir = resultPass ? -RESULT_FILL_PX : RESULT_FILL_PX;
    int oldY = resultFillY;
    resultFillY += dir;
    if (resultFillY <= 0)   { resultFillY = 0;   resultState = RESULT_REVEAL; }
    if (resultFillY >= 240) { resultFillY = 240;  resultState = RESULT_REVEAL; }
    tft.fillRect(0, min(oldY, resultFillY), 320, abs(oldY - resultFillY), color);
  }
  else if (resultState == RESULT_REVEAL) {
    if (!resultFullyDrawn) { tft.fillScreen(color); resultFullyDrawn = true; }
    tft.setTextSize(3); tft.setTextColor(TEXT_COLOR, color);
    int cw = 18, x = (320 - strlen(resultText) * cw) / 2, y = 90;
    tft.fillRect(x-10, y-10, strlen(resultText)*cw+20, 40, color);
    for (int i = 0; i < strlen(resultText); i++) {
      char c = (i < resultRevealIndex) ? resultText[i] : (char)random(33,126);
      tft.setCursor(x + i*cw, y); tft.print(c);
    }
    if (millis()-lastReveal > RESULT_REVEAL_MS) { resultRevealIndex++; lastReveal=millis(); }
    if (resultRevealIndex >= strlen(resultText)) resultState = RESULT_HOLD;
  }
  else if (resultState == RESULT_HOLD) {
    drawResultWord(color);
    if (resultPass) drawStreakBanner(color, screenToMenuIdx(previousScreen));
  }
  else if (resultState == RESULT_REWARD_REVEAL) {
    drawResultWord(color);
    if (drawRewardReveal(color, false)) resultState = RESULT_REWARD_HOLD;
  }
  else if (resultState == RESULT_REWARD_HOLD) {
    drawResultWord(color); drawRewardReveal(color, true);
  }
  else if (resultState == RESULT_DRAIN) {
    int dir = resultPass ? RESULT_DRAIN_PX : -RESULT_DRAIN_PX;
    int oldY = resultFillY;
    resultFillY += dir;
    bool done = resultPass ? (resultFillY >= 240) : (resultFillY <= 0);
    if (done) {
      resultInitialized = false; resultFullyDrawn = false;
      // Cycle the prescript for the menu we're returning to so a
      // new card appears (only for normal menus; timed handles its own).
      int returnMenu = screenToMenuIdx(previousScreen);
      if (returnMenu >= 0 && returnMenu <= 3) cyclePrescript(returnMenu);
      goTo(previousScreen); return;
    }
    int ry = min(oldY, resultFillY), rh = abs(oldY - resultFillY);
    tft.fillRect(0, ry, 320, rh, BG_COLOR);
  }
}

// Short name for swipe indicators
const char* prescriptScreenName(Screen s) {
  switch (s) {
    case MENU_GAME: return "GAME";
    case MENU_1:    return "CITY";
    case MENU_2:    return "CON";
    case MENU_3:    return "TASK";
    case MENU_4:    return "TIME";
    default:        return "";
  }
}

// =================================================
// MENU_GAME DRAW  (regular menus)
// =================================================
void drawPrescript() {
  tft.setTextSize(1);

  const char* label =
    (screen == MENU_1)    ? "CITY" :
    (screen == MENU_2)    ? "CON"  :
    (screen == MENU_3)    ? "TASK" : "GAME";

  int menuIdx =
    (screen == MENU_GAME) ? 0 :
    (screen == MENU_1)    ? 1 :
    (screen == MENU_2)    ? 2 : 3;

  if (isRareEvent) {
    tft.setTextColor(RARE_COLOR, BG_COLOR);
    tft.setCursor(6, 6); tft.print(label);
    const char* rTag = "[INDEX]";
    tft.setCursor(320 - strlen(rTag) * 6 - 4, 6); tft.print(rTag);
    drawTimedCountdown(BG_COLOR);
  } else {
    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(6, 6); tft.print(label);

    // Difficulty badge  -  always shown, shows current tier and progress to next
    uint8_t diff = menuDiffFor(menuIdx);
    int passes = passCount[menuIdx];
    char diffBuf[24];
    if (diff == 0) {
      // Easy  -  show passes needed to reach Normal
      snprintf(diffBuf, sizeof(diffBuf), "[EASY %d/%d]", passes, diffMedThreshold);
      tft.setTextColor(GOOD_COLOR, BG_COLOR);
    } else if (diff == 1) {
      // Normal  -  show passes needed to reach Hard
      snprintf(diffBuf, sizeof(diffBuf), "[NRM %d/%d]", passes, diffHardThreshold);
      tft.setTextColor(TEXT_COLOR, BG_COLOR);
    } else {
      // Hard  -  show total passes
      snprintf(diffBuf, sizeof(diffBuf), "[HARD %d]", passes);
      tft.setTextColor(tft.color565(220, 80, 40), BG_COLOR);
    }
    tft.setCursor(6 + strlen(label)*6 + 6, 6); tft.print(diffBuf);

    // Timed countdown top-right  -  shown on all prescript screens
    drawTimedCountdown(BG_COLOR);

    // Streak indicator below label
    char streakBuf[12];
    snprintf(streakBuf, sizeof(streakBuf), "s:%d", streak[menuIdx]);
    uint16_t sCol = (streak[menuIdx] > 0 && streak[menuIdx] == bestStreak[menuIdx])
                    ? GOOD_COLOR : tft.color565(80, 100, 130);
    tft.setTextColor(sCol, BG_COLOR);
    tft.setCursor(6, 16);
    tft.print(streakBuf);
  }

  // ── Swipe direction indicators at bottom ────────
  if (loopLen > 1) {
    int prevIdx = (loopIdx + loopLen - 1) % loopLen;
    int nextIdx = (loopIdx + 1) % loopLen;
    uint16_t arrowCol = tft.color565(40, 55, 80);
    tft.setTextSize(1); tft.setTextColor(arrowCol, BG_COLOR);
    char leftBuf[10];
    snprintf(leftBuf, sizeof(leftBuf), "< %s", prescriptScreenName(loopScreens[prevIdx]));
    tft.setCursor(2, 228); tft.print(leftBuf);
    char rightBuf[10];
    snprintf(rightBuf, sizeof(rightBuf), "%s >", prescriptScreenName(loopScreens[nextIdx]));
    tft.setCursor(318 - strlen(rightBuf)*6, 228); tft.print(rightBuf);
  }

  uint16_t txtCol = isRareEvent ? RARE_COLOR : TEXT_COLOR;
  tft.setTextSize(2);

  const int cw = 12, ch = 18;
  int availTop = logoBottomY + 4;
  int availBot = 214;

  // Total chars across all wrapped lines + inter-line gaps (one per line).
  // This matches the old idx2 counter exactly.
  int totalChars = 0;
  for (int l = 0; l < lineCount; l++) totalChars += strlen(lines[l]) + 1;

  bool fullyRevealed = (revealIndex >= totalChars);

  // Both phases use the same wrapped layout.
  // During reveal: chars before revealIndex draw real, rest draw scrambled dim.
  // During hold:   all chars draw real in full colour.
  int textH = lineCount * ch;
  int textY = availTop + max(0, (availBot - availTop - textH) / 2);

  tft.startWrite();
  int idx2 = 0;
  for (int l = 0; l < lineCount; l++) {
    int llen = strlen(lines[l]);
    int x = (320 - llen * cw) / 2;
    int y = textY + l * ch;
    if (y + ch > availBot) break;
    for (int i = 0; i < llen; i++) {
      char c;
      uint16_t col;
      if (fullyRevealed || idx2 < revealIndex) {
        c   = lines[l][i];
        col = txtCol;
      } else {
        c   = (char)random(33, 126);
        col = tft.color565(30, 50, 80);
      }
      tft.setTextColor(col, BG_COLOR);
      tft.setCursor(x + i * cw, y); tft.print(c);
      idx2++;
    }
    idx2++;   // inter-line gap
  }

  tft.setTextSize(2);
  tft.setTextColor(txtCol, BG_COLOR);
  tft.setCursor(60,  216); tft.print("PASS");
  tft.setCursor(220, 216); tft.print("FAIL");
  tft.endWrite();

  drawSessionTimer(TEXT_COLOR);
}

// =================================================
// DRAW HELPERS
// =================================================
void drawButton(int x, int y, int w, int h, const char* label, bool highlight = false) {
  uint16_t border = highlight ? TEXT_COLOR : tft.color565(60, 60, 80);
  tft.drawRect(x, y, w, h, border);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  int tx = x + (w - strlen(label) * 6) / 2;
  int ty = y + (h - 8) / 2;
  tft.setCursor(tx, ty); tft.print(label);
}

// =================================================
// DICE ROLL SCREEN
// =================================================
void drawD20(int cx, int cy, int r, int number, uint16_t faceColor, uint16_t numColor, bool drawFull) {
  const int sides = 20;
  float angleStep = 2.0 * 3.14159265 / sides;
  float offset    = -3.14159265 / 2.0;

  if (drawFull) {
    for (int i = 0; i < sides; i++) {
      float a1 = offset + i * angleStep, a2 = offset + (i+1) * angleStep;
      tft.fillTriangle(cx, cy,
        cx + (int)(r*cos(a1)), cy + (int)(r*sin(a1)),
        cx + (int)(r*cos(a2)), cy + (int)(r*sin(a2)), faceColor);
    }
  }
  for (int i = 0; i < sides; i++) {
    float a1 = offset + i * angleStep, a2 = offset + (i+1) * angleStep;
    tft.drawLine(cx+(int)(r*cos(a1)), cy+(int)(r*sin(a1)),
                 cx+(int)(r*cos(a2)), cy+(int)(r*sin(a2)), TEXT_COLOR);
  }
  int ri = r * 3 / 5;
  for (int i = 0; i < sides; i++) {
    float a1 = offset + i * angleStep, a2 = offset + (i+1) * angleStep;
    tft.drawLine(cx+(int)(ri*cos(a1)), cy+(int)(ri*sin(a1)),
                 cx+(int)(ri*cos(a2)), cy+(int)(ri*sin(a2)), tft.color565(40,60,90));
  }
  char buf[4]; snprintf(buf, sizeof(buf), "%d", number);
  int len = strlen(buf), tsize = (number >= 10) ? 3 : 4;
  int cw = tsize*6, ch = tsize*8;
  tft.setTextSize(tsize);
  tft.setTextColor(numColor, faceColor);
  tft.setCursor(cx - (len*cw)/2, cy - ch/2);
  tft.print(buf);
}

void startDiceRoll() {
  dicePhase = DICE_SPIN; diceFinal = random(1,21); diceCurrent = random(1,21);
  diceFrame = 0; diceSettleStep = 0; diceLastFrame = 0;
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(70,70,90), BG_COLOR);
  tft.setCursor((320 - 11*6)/2, 210); tft.print("TAP TO REROLL");
  // Back button  -  visible throughout the entire D20 screen
  drawButton(10, 200, 80, 22, "BACK");
}

void drawDiceRoll() {
  unsigned long interval;
  if (dicePhase == DICE_SPIN)        interval = DICE_SPIN_MS;
  else if (dicePhase == DICE_SETTLE) interval = DICE_SPIN_MS + (180UL * diceSettleStep) / diceSettleMax;
  else return;

  if (millis() - diceLastFrame < interval) return;
  diceLastFrame = millis();
  int cx = 160, cy = 110, r = 85;

  if (dicePhase == DICE_SPIN) {
    diceCurrent = random(1,21); diceFrame++;
    drawD20(cx + random(-3,4), cy + random(-3,4), r, diceCurrent, BG_COLOR, TEXT_COLOR, true);
    if (diceFrame >= diceSpinFrames) { dicePhase = DICE_SETTLE; diceSettleStep = 0; }
  }
  else if (dicePhase == DICE_SETTLE) {
    diceSettleStep++;
    diceCurrent = (diceSettleStep % 2 == 0) ? diceFinal : random(1,21);
    drawD20(cx, cy, r, diceCurrent, BG_COLOR, TEXT_COLOR, true);
    if (diceSettleStep >= diceSettleMax) {
      dicePhase = DICE_HOLD; diceCurrent = diceFinal;
      uint16_t fc = (diceFinal==20) ? tft.color565(180,140,0) :
                    (diceFinal==1)  ? tft.color565(160,20,20) : tft.color565(0,35,80);
      tft.fillCircle(cx, cy, r+6, BG_COLOR);
      drawD20(cx, cy, r, diceFinal, fc, BG_COLOR, true);
      tft.setTextSize(1);
      if (diceFinal==20) {
        tft.setTextColor(tft.color565(220,180,0), BG_COLOR);
        tft.setCursor((320-8*6)/2, 15); tft.print("NAT  20!");
      } else if (diceFinal==1) {
        tft.setTextColor(tft.color565(200,50,50), BG_COLOR);
        tft.setCursor((320-7*6)/2, 15); tft.print("NAT  1.");
      } else {
        tft.fillRect(0, 10, 320, 12, BG_COLOR);
      }
      tft.setTextColor(tft.color565(70,70,90), BG_COLOR);
      tft.setCursor((320-11*6)/2, 208); tft.print("TAP TO REROLL");
    }
  }
}

// =================================================
// MAIN MENU
// =================================================
void drawMenu() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  const char* title = "MAIN MENU";
  tft.setCursor((320 - strlen(title)*12)/2, 30);
  tft.print(title);
  drawButton(95, 70,  130, 26, "TOOLS");
  drawButton(95, 102, 130, 26, "CYBER TOOLS");
  drawButton(95, 134, 130, 26, "SETTINGS");

  // Separate game picker button outside the three primary boxes
  tft.setTextSize(1); tft.setTextColor(tft.color565(180,140,220), BG_COLOR);
  tft.setCursor((320-22*6)/2, 175); tft.print("PICK A GAME AT RANDOM");
  tft.drawRect(60, 188, 200, 28, tft.color565(120,90,160));
  tft.fillRect(61, 189, 198, 26, tft.color565(35,20,55));
  tft.setTextSize(2); tft.setTextColor(tft.color565(200,160,255), tft.color565(35,20,55));
  tft.setCursor((320-11*12)/2, 194); tft.print("GAME PICKER");

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(70,70,90), BG_COLOR);
  tft.setCursor((320-17*6)/2, 224);
  tft.print("SWIPE UP TO GO BACK");
}

// =================================================
// TOOLS MENU
// =================================================
// =================================================
// SCROLLABLE TOOLS MENU
// =================================================
void drawToolsMenu() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(20,30,55));
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, tft.color565(20,30,55));
  tft.setCursor((320-5*6)/2, 4); tft.print("TOOLS");

  const char* items[] = {
    "ROLL D20","COIN FLIP","TAROT DRAW",
    "ORACLE","COUNTDOWN TIMER","WORD GENERATOR","CIPHER WHEEL","CHILDREN OF THE CITY"
  };
  const int nItems = 8;
  const int itemH = 36, startY = 18, visH = 210;

  for (int i = 0; i < nItems; i++) {
    int y = startY + i * itemH - toolsScrollY;
    if (y + itemH < startY || y > startY + visH) continue;
    tft.drawRect(4, y+2, 278, itemH-4, tft.color565(40,50,80));
    tft.fillRect(5, y+3, 276, itemH-6, BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(14, y + (itemH-8)/2); tft.print(items[i]);
  }

  // Back cube bottom-RIGHT
  tft.drawRect(258, 220, 60, 20, tft.color565(40,50,80));
  tft.fillRect(259, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(100,120,160), BG_COLOR);
  tft.setCursor(267, 226); tft.print("BACK");
}

void drawCyberMenu() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(20,50,30));
  tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, tft.color565(20,50,30));
  tft.setCursor((320-10*6)/2, 4); tft.print("CYBER TOOLS");

  const char* items[] = {
    "WIFI SCANNER","BLE SCANNER","BLUETOOTH SCAN","QR DISPLAY"
  };
  const int nItems = 4;
  const int itemH = 40, startY = 18, visH = 210;

  for (int i = 0; i < nItems; i++) {
    int y = startY + i * itemH - cyberScrollY;
    if (y + itemH < startY || y > startY + visH) continue;
    tft.drawRect(4, y+2, 278, itemH-4, tft.color565(30,60,40));
    tft.fillRect(5, y+3, 276, itemH-6, BG_COLOR);
    tft.setTextSize(1); tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(14, y + (itemH-8)/2); tft.print(items[i]);
  }

  // Back cube bottom-left (60×20 for easy tapping)
  tft.drawRect(2, 220, 60, 20, tft.color565(30,60,40));
  tft.fillRect(3, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(80,140,100), BG_COLOR);
  tft.setCursor(12, 226); tft.print("BACK");
}

// =================================================
// CYBER MENU
// =================================================
// =================================================
// SETTINGS SCREEN
// =================================================
void drawSettings() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-strlen("SETTINGS")*12)/2, 10);
  tft.print("SETTINGS");

  drawButton(70, 38,  180, 22, "TIMER DURATION");
  drawButton(70, 66,  180, 22, "IDLE TIMEOUT");
  drawButton(70, 94,  180, 22, "ANIM SPEED");
  drawButton(70, 122, 180, 22, "DIFFICULTY");
  drawButton(70, 150, 180, 22, "CATEGORIES");
  drawButton(70, 178, 180, 22, "RESET DECKS");
  drawButton(70, 206, 180, 22, "HISTORY");

  // Back cube bottom-LEFT (60×20)
  tft.drawRect(2, 220, 60, 20, tft.color565(50,50,70));
  tft.fillRect(3, 221, 58, 18, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(80,90,120), BG_COLOR);
  tft.setCursor(12, 226); tft.print("BACK");
}

// =================================================
// SETTINGS  -  TIMER DURATION
// =================================================
// Layout: three fields: DAYS / HOURS / MINUTES
// Each has [+] value [-] on one row
void drawSettingsTimer() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-17*6)/2, 12);
  tft.print("SET TIMER DURATION");

  // Days row  y=40
  tft.setTextColor(tft.color565(80,100,130), BG_COLOR);
  tft.setCursor(14, 44); tft.print("DAYS");
  drawButton(10,  55, 30, 22, "+");
  char db[6]; snprintf(db, sizeof(db), "%u", editDays);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(46, 58); tft.print(db);
  drawButton(10,  79, 30, 22, "-");
  tft.setTextSize(1);

  tft.setTextColor(tft.color565(80,100,130), BG_COLOR);
  tft.setCursor(122, 44); tft.print("HOURS");
  drawButton(118, 55, 30, 22, "+");
  char hb[6]; snprintf(hb, sizeof(hb), "%u", editHours);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(154, 58); tft.print(hb);
  drawButton(118, 79, 30, 22, "-");
  tft.setTextSize(1);

  tft.setTextColor(tft.color565(80,100,130), BG_COLOR);
  tft.setCursor(226, 44); tft.print("MIN");
  drawButton(222, 55, 30, 22, "+");
  char mb[6]; snprintf(mb, sizeof(mb), "%u", editMinutes);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(258, 58); tft.print(mb);
  drawButton(222, 79, 30, 22, "-");
  tft.setTextSize(1);

  // Current total
  unsigned long totSec = ((unsigned long)editDays*86400)
                       + ((unsigned long)editHours*3600)
                       + ((unsigned long)editMinutes*60);
  unsigned int  td = totSec / 86400, th = (totSec % 86400) / 3600,
                tm = (totSec % 3600) / 60;
  char totBuf[32];
  snprintf(totBuf, sizeof(totBuf), "= %ud %uh %um", td, th, tm);
  tft.setTextColor(tft.color565(80,100,130), BG_COLOR);
  tft.setCursor((320 - strlen(totBuf)*6)/2, 112);
  tft.print(totBuf);

  // Warn if all zero
  if (editDays == 0 && editHours == 0 && editMinutes == 0) {
    tft.setTextColor(FAIL_COLOR, BG_COLOR);
    tft.setCursor((320-16*6)/2, 126);
    tft.print("MIN 1 MIN REQUIRED");
  }

  drawButton(20,  148, 130, 26, "SAVE");
  drawButton(170, 148, 130, 26, "CANCEL");
}

// =================================================
// SETTINGS  -  BRIGHTNESS
// =================================================
void drawSettingsBright() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-strlen("BRIGHTNESS")*12)/2, 40);
  tft.print("BRIGHTNESS");

  // Three buttons: LOW / MID / HIGH
  const char* labels[3] = {"LOW","MID","HIGH"};
  for (int i = 0; i < 3; i++) {
    bool sel = (brightnessLevel == i);
    uint16_t border = sel ? TEXT_COLOR : tft.color565(50,50,70);
    int bx = 20 + i * 100, by = 100;
    tft.drawRect(bx, by, 80, 36, border);
    tft.fillRect(bx+1, by+1, 78, 34, sel ? tft.color565(20,40,80) : BG_COLOR);
    tft.setTextSize(1);
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(80,100,130), BG_COLOR);
    tft.setCursor(bx + (80 - strlen(labels[i])*6)/2, by+14);
    tft.print(labels[i]);
  }

  drawButton(110, 170, 100, 26, "BACK");
}

// =================================================
// SETTINGS  -  IDLE TIMEOUT
// =================================================
void drawSettingsIdle() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-strlen("IDLE TIMEOUT")*12)/2, 20);
  tft.print("IDLE TIMEOUT");

  tft.setTextSize(1); tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  tft.setCursor((320-28*6)/2, 48);
  tft.print("TIME BEFORE IDLE ANIMATION");

  // Four options: 30s / 1min / 2min / 5min / NEVER
  struct { const char* lbl; unsigned long ms; } opts[] = {
    {"30 SEC", 30000UL}, {"1 MIN", 60000UL},
    {"2 MIN", 120000UL}, {"5 MIN", 300000UL}, {"NEVER", 0xFFFFFFFFUL}
  };
  for (int i = 0; i < 5; i++) {
    bool sel = (sleepMs == opts[i].ms);
    int bx = 20, by = 68 + i * 30;
    tft.drawRect(bx, by, 280, 24, sel ? TEXT_COLOR : tft.color565(50,50,70));
    tft.fillRect(bx+1, by+1, 278, 22, sel ? tft.color565(20,40,80) : BG_COLOR);
    tft.setTextSize(1);
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(70,80,100), BG_COLOR);
    tft.setCursor(bx + (280 - strlen(opts[i].lbl)*6)/2, by+8);
    tft.print(opts[i].lbl);
  }
  drawButton(110, 218, 100, 22, "BACK");
}
// Three rows: GLYPH / REVEAL / TRANSITION
// Each row has a label, a live preview bar, and
// SLOW / MED / FAST tap buttons.
// =================================================

// Helper: draw a 3-option selector row
// y = top of row, curVal and the three option values,
// plus their labels
static void drawSpeedRow(int y, const char* label,
                         uint8_t curVal,
                         uint8_t vSlow, uint8_t vMed, uint8_t vFast) {
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(80,100,130), BG_COLOR);
  tft.setCursor(4, y); tft.print(label);

  struct { const char* lbl; uint8_t val; } opts[3] = {
    {"SLOW", vSlow}, {"MED", vMed}, {"FAST", vFast}
  };

  for (int i = 0; i < 3; i++) {
    bool sel = (curVal == opts[i].val);
    int bx = 10 + i * 102;
    uint16_t border = sel ? TEXT_COLOR : tft.color565(50,50,70);
    tft.drawRect(bx, y+10, 96, 22, border);
    tft.fillRect(bx+1, y+11, 94, 20,
                 sel ? tft.color565(20,40,80) : BG_COLOR);
    tft.setTextSize(1);
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(70,80,100), BG_COLOR);
    tft.setCursor(bx + (96 - strlen(opts[i].lbl)*6)/2, y+17);
    tft.print(opts[i].lbl);
  }
}

void drawSettingsSpeed() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320 - strlen("ANIM SPEED")*12)/2, 8);
  tft.print("ANIM SPEED");

  // GLYPH row  -  also gates reveal. slow=270ms (3x default), med=90ms, fast=30ms
  drawSpeedRow(34,  "GLYPH SCRAMBLE",
               cfg_glyphSpinMs, 270, 90, 30);

  // REVEAL row  -  lower chars/tick = slower, higher = faster
  drawSpeedRow(100, "TEXT REVEAL",
               cfg_revealChars, 1, 2, 4);

  // TRANSITION row  -  fewer px/frame = slower wipe
  drawSpeedRow(166, "TRANSITIONS",
               cfg_transitionPx, 5, 10, 25);

  drawButton(95, 210, 130, 24, "BACK");
}

// =================================================
// SETTINGS  -  DIFFICULTY (progressive thresholds)
// =================================================
void drawSettingsDifficulty() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-strlen("DIFFICULTY")*12)/2, 8); tft.print("DIFFICULTY");

  tft.setTextSize(1); tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  tft.setCursor((320-38*6)/2, 36);
  tft.print("PASSES NEEDED TO UNLOCK EACH TIER");

  // Normal threshold
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(8, 62); tft.print("NORMAL UNLOCKS AFTER:");
  tft.setTextColor(GOOD_COLOR, BG_COLOR);
  char mbuf[8]; snprintf(mbuf,sizeof(mbuf),"%d",diffMedThreshold);
  tft.setCursor(200, 62); tft.print(mbuf); tft.print(" PASSES");
  drawButton(260, 56, 22, 18, "-");
  drawButton(284, 56, 22, 18, "+");

  // Hard threshold
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor(8, 94); tft.print("HARD UNLOCKS AFTER:");
  tft.setTextColor(tft.color565(200,80,40), BG_COLOR);
  char hbuf[8]; snprintf(hbuf,sizeof(hbuf),"%d",diffHardThreshold);
  tft.setCursor(200, 94); tft.print(hbuf); tft.print(" PASSES");
  drawButton(260, 88, 22, 18, "-");
  drawButton(284, 88, 22, 18, "+");

  // Current status per menu
  tft.drawFastHLine(0, 118, 320, tft.color565(40,50,70));
  const char* mnames[4] = {"GAME","CITY","CON","TASK"};
  const char* dnames[3] = {"EASY","NORMAL","HARD"};
  tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  tft.setCursor(8, 124); tft.print("CURRENT STATUS:");
  for (int m=0;m<4;m++) {
    int d = menuDiffFor(m);
    uint16_t col = (d==0)?tft.color565(60,180,60):(d==1)?TEXT_COLOR:tft.color565(200,80,40);
    tft.setTextColor(tft.color565(80,100,130),BG_COLOR);
    tft.setCursor(8, 136+m*18); tft.print(mnames[m]);
    tft.setTextColor(col,BG_COLOR);
    tft.setCursor(70, 136+m*18); tft.print(dnames[d]);
    char pbuf[16]; snprintf(pbuf,sizeof(pbuf),"(%d passed)",passCount[m]);
    tft.setTextColor(tft.color565(60,70,90),BG_COLOR);
    tft.setCursor(140, 136+m*18); tft.print(pbuf);
  }

  drawButton(110, 220, 100, 22, "BACK");
}

// =================================================
// SETTINGS  -  CATEGORIES (timed pool sources)
// =================================================
void drawSettingsCategories() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(2); tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-strlen("TIMED POOLS")*12)/2, 8); tft.print("TIMED POOLS");

  tft.setTextSize(1); tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  tft.setCursor((320-36*6)/2, 34);
  tft.print("WHICH MENUS FEED THE TIMED SCREEN");

  const char* catNames[4] = {"GAME","CITY","CON","TASK"};
  for (int i=0; i<4; i++) {
    bool en = poolEnabled(i);
    int by = 48 + i*38;
    tft.drawRect(30, by, 260, 30, en?TEXT_COLOR:tft.color565(50,50,70));
    tft.fillRect(31, by+1, 258, 28, en?tft.color565(15,30,65):BG_COLOR);
    tft.setTextSize(1);
    tft.setTextColor(en?TEXT_COLOR:tft.color565(60,70,90), BG_COLOR);
    tft.setCursor(50, by+11); tft.print(catNames[i]);
    tft.setTextColor(en?GOOD_COLOR:tft.color565(80,30,30), BG_COLOR);
    tft.setCursor(200, by+11); tft.print(en?"ACTIVE":"INACTIVE");
  }

  tft.setTextSize(1); tft.setTextColor(tft.color565(50,60,80), BG_COLOR);
  tft.setCursor((320-26*6)/2, 214);
  tft.print("SWIPE ALWAYS SHOWS ALL MENUS");
  drawButton(110, 224, 100, 20, "BACK");
}

// =================================================
// RANDOM GAME PICKER
// =================================================
static const char* GAME_LIST[] = {
  "7 DAYS TO DIE", "ABIOTIC FACTOR", "HALF LIFE 2",
  "BALDUR'S GATE 3", "BLADE AND SORCERY", "BLOONS TD 6",
  "BOMB RUSH CYBERFUNK", "BORDERLANDS 2", "COD 3 ZOMBIES",
  "DEAD BY DAYLIGHT", "DEADLOCK", "DEEP ROCK GALACTIC",
  "DYING LIGHT", "FEAR AND HUNGER", "FEAR AND HUNGER 2",
  "GARRY'S MOD", "GET TO THE ORANGE DOOR", "HALF LIFE ALYX",
  "PROJECT DIVA MEGA MIX", "KILLING FLOOR 2", "LIBRARY OF RUINA",
  "LOBOTOMY CORPORATION", "LETHAL COMPANY", "LIMBUS COMPANY",
  "NIER AUTOMATA", "NEON WHITE", "MARVEL RIVALS",
  "MONSTER HUNTER WILDS", "NOVA DRIFT", "VAMPIRE SURVIVORS",
  "PALADINS", "PEGGLE", "PERSONA 3", "PERSONA 4", "PERSONA 5",
  "POWERWASH SIMULATOR", "RAIN WORLD", "SEA OF THIEVES",
  "RISK OF RAIN 2", "SCP CONTAINMENT BREACH", "TEAM FORTRESS 2",
  "WARFRAME", "WEBFISHING", "VRCHAT", "BATTLEBLOCK THEATER", "OVERWATCH"
};
static const int GAME_COUNT = sizeof(GAME_LIST)/sizeof(GAME_LIST[0]);

int gamePickerIdx = 0;
unsigned long gamePickerLastRoll = 0;
int gamePickerRollFrame = 0;
bool gamePickerSettled = false;

void drawGamePicker() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 16, tft.color565(40, 20, 60));
  tft.setTextSize(1); tft.setTextColor(tft.color565(200,160,255), tft.color565(40,20,60));
  tft.setCursor((320-11*6)/2, 4); tft.print("GAME PICKER");

  // Show the current pick large
  const char* name = GAME_LIST[gamePickerIdx];
  int len = strlen(name);

  // Frame
  tft.drawRect(20, 60, 280, 80, tft.color565(80,60,120));
  tft.drawRect(22, 62, 276, 76, tft.color565(40,30,60));

  tft.setTextSize(2);
  // If name too wide, fall back to size 1
  if (len * 12 > 260) {
    tft.setTextSize(1);
    int x = (320 - len*6) / 2;
    tft.setTextColor(gamePickerSettled ? GOOD_COLOR : tft.color565(160,140,200), BG_COLOR);
    tft.setCursor(x, 95); tft.print(name);
  } else {
    int x = (320 - len*12) / 2;
    tft.setTextColor(gamePickerSettled ? GOOD_COLOR : tft.color565(160,140,200), BG_COLOR);
    tft.setCursor(x, 90); tft.print(name);
  }

  tft.setTextSize(1); tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  tft.setCursor((320-22*6)/2, 160);
  tft.print(gamePickerSettled ? "TAP ROLL FOR ANOTHER" : "ROLLING...");

  drawButton(40,  180, 110, 28, "ROLL", false);
  drawButton(170, 180, 110, 28, "BACK", false);

  tft.setTextSize(1); tft.setTextColor(tft.color565(40,55,80), BG_COLOR);
  char count[24]; snprintf(count, sizeof(count), "%d GAMES", GAME_POOL_COUNT);
  tft.setCursor((320 - strlen(count)*6)/2, 222); tft.print(count);
}

void tickGamePicker() {
  if (gamePickerSettled) return;
  if (millis() - gamePickerLastRoll < 80) return;
  gamePickerLastRoll = millis();
  gamePickerRollFrame++;
  gamePickerIdx = random(GAME_POOL_COUNT);
  if (gamePickerRollFrame > 18) gamePickerSettled = true;

  // Only redraw the name box interior  -  no fillScreen
  uint16_t boxBG = tft.color565(20,10,35);
  tft.fillRect(23, 63, 274, 74, boxBG);

  const char* name = GAME_LIST[gamePickerIdx];
  int len = strlen(name);
  uint16_t col = gamePickerSettled ? GOOD_COLOR : tft.color565(160,140,200);

  if (len * 12 > 260) {
    tft.setTextSize(1);
    tft.setTextColor(col, boxBG);
    tft.setCursor((320 - len*6)/2, 96); tft.print(name);
  } else {
    tft.setTextSize(2);
    tft.setTextColor(col, boxBG);
    tft.setCursor((320 - len*12)/2, 90); tft.print(name);
  }

  // Update rolling/settled label
  tft.fillRect(0, 155, 320, 14, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(tft.color565(60,80,110), BG_COLOR);
  const char* lbl = gamePickerSettled ? "TAP ROLL FOR ANOTHER" : "ROLLING...";
  tft.setCursor((320 - strlen(lbl)*6)/2, 160); tft.print(lbl);
}

void gamePickerStart() {
  gamePickerSettled = false;
  gamePickerRollFrame = 0;
  gamePickerLastRoll = millis();
  gamePickerIdx = random(GAME_POOL_COUNT);
  drawGamePicker();
}

// =================================================
// IDLE ANIMATION
// =================================================
// Logo bounds for idle animation
// Logo data stride=320, image=288x86 starting at column 0.
// Visual content inside the 288px wide data is shifted right ~8px and down ~4px
// so we nudge the draw position left and up to optically centre it.
static int idleLogoX0 = 0;
static int idleLogoY0 = 72;
static const int IDLE_LOGO_W = 320;
static const int IDLE_LOGO_H = 96;

// Restore pixels under an erased particle
static void idleRestorePixels(int tx, int ty, int len) {
  int cw = 6, ch = 8;
  const uint16_t* src = logoBuf ? logoBuf : index_565;
  tft.fillRect(tx, ty, len * cw + 1, ch, BG_COLOR);
  for (int row = 0; row < ch; row++) {
    int py = ty + row;
    int iy = py - idleLogoY0;
    if (iy < 0 || iy >= IDLE_LOGO_H) continue;
    for (int col = 0; col < len * cw + 1; col++) {
      int px = tx + col;
      int ix = px - idleLogoX0;
      if (ix < 0 || ix >= IDLE_LOGO_W) continue;
      uint16_t p = src[(iy * 320) + ix];
      if (p) {
        uint8_t r=(p>>11)&0x1F, g=(p>>5)&0x3F, b=p&0x1F;
        r=r*2/5; g=g*2/5; b=b*2/5;
        tft.drawPixel(px, py, (r<<11)|(g<<5)|b);
      }
    }
  }
}

void idleSpawnPart(int idx) {
  IdlePart& p = idleParts[idx];
  int cw = 6, ch = 8, maxLen = 26;
  int lx0 = idleLogoX0, lx1 = idleLogoX0 + IDLE_LOGO_W;
  int ly0 = idleLogoY0, ly1 = idleLogoY0 + IDLE_LOGO_H;

  // Pick a phrase not currently showing on any other active particle
  const char* chosen = nullptr;
  for (int attempt = 0; attempt < 20; attempt++) {
    const char* candidate = idleTexts[random(IDLE_TEXT_COUNT)];
    bool inUse = false;
    for (int j = 0; j < IDLE_N; j++) {
      if (j == idx) continue;
      if (idleParts[j].phase < 2 && strcmp(idleParts[j].text, candidate) == 0) {
        inUse = true; break;
      }
    }
    if (!inUse) { chosen = candidate; break; }
  }
  if (!chosen) chosen = idleTexts[random(IDLE_TEXT_COUNT)];   // fallback
  strncpy(p.text, chosen, 27); p.text[27] = '\0';
  int len = strlen(p.text);
  int pw = len * cw;   // pixel width of this particle

  // Try up to 10 positions, pick one that doesn't overlap any active particle
  for (int attempt = 0; attempt < 10; attempt++) {
    int tx, ty;
    int zone = random(4);
    switch (zone) {
      case 0: tx = random(4, 316); ty = random(4, max(5, ly0 - 10)); break;
      case 1: tx = random(4, max(5, lx0 - 4)); ty = random(4, 230); break;
      case 2: tx = random(min(316, lx1 + 2), 312); ty = random(4, 230); break;
      default:tx = random(4, 316); ty = random(ly1 + 2, 234); break;
    }
    // Clamp x so text stays on screen
    if (tx + pw > 316) tx = max(4, 316 - pw);

    // Check overlap against all other active particles
    bool overlaps = false;
    for (int j = 0; j < IDLE_N; j++) {
      if (j == idx || idleParts[j].phase > 1) continue;   // skip self and erasing particles
      int ow = strlen(idleParts[j].text) * cw;
      // Add 4px padding between particles
      if (tx < idleParts[j].x + ow + 4 &&
          tx + pw + 4 > idleParts[j].x &&
          ty < idleParts[j].y + ch + 2 &&
          ty + ch + 2 > idleParts[j].y) {
        overlaps = true;
        break;
      }
    }

    if (!overlaps) {
      p.x = tx; p.y = ty;
      break;
    }
    // Last attempt — use position anyway rather than failing to spawn
    if (attempt == 9) { p.x = tx; p.y = ty; }
  }

  p.colour    = idleParticleColour();
  p.revIdx    = 0;
  p.phase     = 0;
  p.phaseStart = millis();
}

void drawIdleAnim() {
  tft.fillScreen(BG_COLOR);

  // Logo: image is 320x96, content centre at y=48 within image.
  // Draw at y0=72 so content centre lands at y=120 (screen centre).
  // x0=0 because content is already horizontally centred in the 320px image.
  idleLogoX0 = 0;
  idleLogoY0 = 72;

  const uint16_t* src = logoBuf ? logoBuf : index_565;
  tft.setSwapBytes(true);
  tft.pushImage(idleLogoX0, idleLogoY0, IDLE_LOGO_W, IDLE_LOGO_H, src);
  tft.setSwapBytes(false);

  // Best streak / total passes ticker — quiet dim line below logo
  {
    int best = 0, total = 0;
    for (int i = 0; i < 5; i++) {
      total += passCount[i];
      if (bestStreak[i] > best) best = bestStreak[i];
    }
    char statBuf[32];
    snprintf(statBuf, sizeof(statBuf), "TOTAL: %d   BEST STREAK: %d", total, best);
    int sx = (320 - strlen(statBuf) * 6) / 2;
    int sy = 166;   // just below logo content (content ends at screen y=88+72=160)
    if (sy < 230) {
      tft.setTextSize(1);
      tft.setTextColor(tft.color565(25,40,60), BG_COLOR);
      tft.setCursor(sx, sy); tft.print(statBuf);
    }
  }

  // Scan line starts at top
  idleScanY    = 0;
  idleScanLast = millis();

  // Spawn particles staggered so they never all hit phase 2 at the same time
  for (int i = 0; i < IDLE_N; i++) {
    idleSpawnPart(i);
    // Artificially advance reveal index so particles finish at different times
    idleParts[i].phaseStart -= (unsigned long)(i * 1800);
  }
  idleLastSpawn = millis();
  idleActive    = true;
}

void tickIdleAnim() {
  if (!idleActive) return;

  // Periodically resend Display ON to recover ILI9341 sync loss
  static unsigned long lastIdleSync = 0;
  if (millis() - lastIdleSync > 10000) {
    lastIdleSync = millis();
    tft.writecommand(0x29);
  }

  // ── Scan line ──────────────────────────────────
  if (millis() - idleScanLast > IDLE_SCAN_INTERVAL) {
    idleScanLast = millis();

    // Track last glitch position so we can erase it next tick
    static int glitchEraseY = -1, glitchEraseX = 0, glitchEraseW = 0;

    // Erase last glitch burst
    if (glitchEraseY >= 0) {
      tft.drawFastHLine(glitchEraseX, glitchEraseY, glitchEraseW, BG_COLOR);
      glitchEraseY = -1;
    }

    // Erase primary scan line
    tft.drawFastHLine(0, idleScanY, 320, BG_COLOR);
    {
      int iy = idleScanY - idleLogoY0;
      if (iy >= 0 && iy < IDLE_LOGO_H) {
        const uint16_t* src = logoBuf ? logoBuf : index_565;
        for (int ix = 0; ix < IDLE_LOGO_W; ix++) {
          uint16_t px = src[(iy * 320) + ix];
          if (px) {
            uint8_t r=(px>>11)&0x1F,g=(px>>5)&0x3F,b=px&0x1F;
            r=r*2/5; g=g*2/5; b=b*2/5;
            tft.drawPixel(idleLogoX0+ix, idleScanY, (r<<11)|(g<<5)|b);
          }
        }
      }
    }

    // Advance — jitter occasionally
    idleScanY += (random(10) == 0) ? random(2, 5) : 1;
    if (idleScanY > 235) idleScanY = 0;

    // Draw primary line — variable brightness
    uint8_t br = 16 + random(14);
    tft.drawFastHLine(0, idleScanY, 320, tft.color565(br/3, br/2, br));

    // Occasional glitch burst — tracked for erase next tick
    if (random(14) == 0) {
      glitchEraseX = random(0, 260);
      glitchEraseW = random(10, 60);
      glitchEraseY = random(0, 235);
      tft.drawFastHLine(glitchEraseX, glitchEraseY, glitchEraseW,
        tft.color565(0, random(40,100), random(60,150)));
    }
  }

  // ── Particle tick ─────────────────────────────
  // Note: phase 2 (erase) immediately respawns — no separate respawn loop needed.
  for (int i = 0; i < IDLE_N; i++) {
    IdlePart& p = idleParts[i];
    int len = strlen(p.text);
    int cw = 6, ch = 8;

    if (p.phase == 0) {
      // Reveal — lock one char per cfg_glyphSpinMs, scramble remaining chars every tick.
      // Use BG_COLOR as text background so each cell is cleared before drawing.
      tft.setTextSize(1);
      for (int j = 0; j < len; j++) {
        char c;
        uint16_t col;
        if (j < p.revIdx) {
          c   = p.text[j];
          col = p.colour;
        } else {
          c   = (char)random(33, 126);
          col = tft.color565(20, 35, 55);
        }
        tft.setTextColor(col, BG_COLOR);
        tft.setCursor(p.x + j*cw, p.y); tft.print(c);
      }
      if (millis() - p.phaseStart > (unsigned long)cfg_glyphSpinMs) {
        if (p.revIdx < len) p.revIdx++;
        p.phaseStart = millis();
      }
      if (p.revIdx >= len) { p.phase = 1; p.phaseStart = millis(); }
    }
    else if (p.phase == 1) {
      // Hold — redraw every tick to survive scan line passes
      tft.setTextSize(1);
      tft.setTextColor(p.colour, BG_COLOR);
      tft.setCursor(p.x, p.y); tft.print(p.text);
      if (millis() - p.phaseStart > 2400) { p.phase = 2; p.phaseStart = millis(); }
    }
    else if (p.phase == 2) {
      // Erase — fill then restore logo pixels, then immediately respawn
      tft.fillRect(p.x, p.y, len * cw + 1, ch, BG_COLOR);
      idleRestorePixels(p.x, p.y, len);
      idleSpawnPart(i);   // respawn immediately — no waiting loop needed
    }
  }
}

// =================================================
// RESET CONFIRM SCREEN
// =================================================
void drawResetConfirm() {
  tft.fillScreen(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setCursor((320-19*6)/2, 40);
  tft.print("RESET WHICH DECK?");

  drawButton(20,  80, 120, 28, "GAME");
  drawButton(180, 80, 120, 28, "CITY");
  drawButton(20,  120, 120, 28, "CON");
  drawButton(180, 120, 120, 28, "TASK");
  drawButton(20,  160, 120, 28, "TIMED");
  drawButton(180, 160, 120, 28, "ALL");
  drawButton(95,  200, 130, 22, "CANCEL");
}

// =================================================
// HISTORY SCREEN
// =================================================
void drawHistoryScreen() {
  tft.fillScreen(BG_COLOR);

  const char* menuNames[5] = {"GAME","CITY","CON","TASK","TIMED"};
  const char** menuSrc[5]  = {gamePrescripts, cityPrescripts, conPrescripts,
                               taskPrescripts, gamePrescripts};
  int menuCnt[5] = {GAME_POOL_COUNT, CITY_COUNT, CON_COUNT, TASK_COUNT, GAME_POOL_COUNT};

  // Tab bar  -  24px tall so tabs are easy to tap
  for (int i = 0; i < 5; i++) {
    bool sel = (histViewMenu == i);
    int bx = i * 64, by = 0;
    tft.fillRect(bx, by, 64, 24, sel ? tft.color565(20,40,80) : tft.color565(10,14,24));
    tft.drawRect(bx, by, 64, 24, tft.color565(40,50,70));
    tft.setTextSize(1);
    tft.setTextColor(sel ? TEXT_COLOR : tft.color565(60,70,90), sel ? tft.color565(20,40,80) : tft.color565(10,14,24));
    tft.setCursor(bx + (64 - strlen(menuNames[i])*6)/2, by+8);
    tft.print(menuNames[i]);
  }
  tft.drawFastHLine(0, 24, 320, tft.color565(40,50,70));

  // List entries newest-first
  int head = histHead[histViewMenu];
  int y    = 30;
  bool anyEntry = false;

  for (int n = 0; n < HIST_LEN; n++) {
    int idx = (head - 1 - n + HIST_LEN) % HIST_LEN;
    int cardIdx = history[histViewMenu][idx];
    if (cardIdx == -2) continue; // empty

    anyEntry = true;
    tft.setTextSize(1);
    if (cardIdx == -1) {
      tft.setTextColor(RARE_COLOR, BG_COLOR);
      tft.setCursor(4, y); tft.print("[INDEX EVENT]");
    } else {
      // Show truncated prescript text using char buffer — no heap allocation
      int ci = cardIdx % menuCnt[histViewMenu];
      char hs[50];
      strncpy(hs, menuSrc[histViewMenu][ci], 49); hs[49] = '\0';
      if (strlen(hs) > 46) { hs[43]='.'; hs[44]='.'; hs[45]='.'; hs[46]='\0'; }
      tft.setTextColor(TEXT_COLOR, BG_COLOR);
      tft.setCursor(4, y); tft.print(hs);
    }
    y += 11;
    if (y > 220) break;
  }

  if (!anyEntry) {
    tft.setTextColor(tft.color565(50,60,80), BG_COLOR);
    tft.setTextSize(1);
    tft.setCursor((320-12*6)/2, 110);
    tft.print("NO HISTORY YET");
  }

  // Back button at bottom
  drawButton(110, 205, 100, 26, "BACK");
}

// =================================================
// RESULT TRIGGER
// =================================================
void showResult(bool pass) {
  resultPass = pass; resultInitialized = false;
  lastAnimFrame = 0; lastReveal = 0;
  goTo(RESULT);
}

// =================================================
// NAVIGATION
// =================================================
void goToLoopScreen(int idx) { goTo(loopScreens[idx % loopLen]); }

void nextScreen() { loopIdx=(loopIdx+1)%loopLen; goToLoopScreen(loopIdx); }
void prevScreen() { loopIdx=(loopIdx+loopLen-1)%loopLen; goToLoopScreen(loopIdx); }

void enterMainMenu() { goTo(MAIN_MENU); }
void exitMainMenu()  { goToLoopScreen(loopIdx); }

// =================================================
// HIT TEST
// =================================================
bool hit(int tx, int ty, int bx, int by, int bw, int bh) {
  return tx >= bx && tx <= bx+bw && ty >= by && ty <= by+bh;
}

// =================================================
// SETUP
// =================================================
void setup() {
  // Backlight via PWM  (ESP32 core 3.x API: ledcAttach replaces ledcSetup+ledcAttachPin)
  ledcAttach(TFT_BL, BL_PWM_FREQ, BL_PWM_RES);

  tft.init();
  tft.setRotation(1);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);

  // ESP32 has a hardware true RNG  -  use it instead of analogRead(0)
  // which reads a pin tied to the display SPI and returns the same
  // value every boot. Also discard the first few values to flush any
  // degenerate initial state from the PRNG warm-up.
  Serial.begin(115200);

  // Log reset reason — helps identify brownout vs watchdog vs panic
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d ", reason);
  switch(reason) {
    case ESP_RST_POWERON:   Serial.println("(power-on)"); break;
    case ESP_RST_BROWNOUT:  Serial.println("(BROWNOUT - power supply issue!)"); break;
    case ESP_RST_TASK_WDT:  Serial.println("(task watchdog)"); break;
    case ESP_RST_INT_WDT:   Serial.println("(interrupt watchdog)"); break;
    case ESP_RST_PANIC:     Serial.println("(panic/crash)"); break;
    default:                Serial.println("(other)"); break;
  }

  // Copy logo from PROGMEM (flash) to DRAM before anything else.
  // This prevents flash cache conflicts between pushImage reads and NVS writes.
  logoBuf = (uint16_t*)malloc(320 * 96 * sizeof(uint16_t));
  if (logoBuf) {
    memcpy_P(logoBuf, index_565, 320 * 96 * sizeof(uint16_t));
    Serial.println("Logo copied to DRAM.");
  } else {
    Serial.println("Logo DRAM alloc failed — will use PROGMEM.");
  }

  Serial.printf("Boot. Free heap: %u bytes\n", ESP.getFreeHeap());

  // Configure task watchdog — 30s timeout, don't panic on trigger
  esp_task_wdt_config_t wdt_cfg = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = false };
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL);

  // Disable WiFi and BT — not used, frees ~70KB heap and removes background tasks
  WiFi.mode(WIFI_OFF);
  btStop();

  // GPIO2 = TFT_DC on this board. Also an ESP32 strapping pin.
  // Set as output and drive high to prevent glitches from corrupting D/C line.
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  randomSeed(esp_random());
  for (int i = 0; i < 8; i++) random(256);  // flush degenerate initial values

  Serial.println("loadState start");
  loadState();
  Serial.println("loadState done");
  applyBrightness();

  editMinutes = timerMinutes;
  editHours   = timerHours;
  editDays    = timerDays;

  cyberQRInit("https://user.xmission.com/~emailbox/catstuff.htm");
  toolsLoad();

  loopIdx = 0;   // swipe loop is always all 5 screens
  sessionStart       = millis();
  lastTouchTime      = millis();
  lastInteractionTime = millis();

  // If a timed prescript was active, offset the start time so the
  // remaining duration is preserved across reboots
  // (timedStart is the epoch millis when assigned  -  we can't recover
  //  real-world time without RTC, so we just leave it; on reboot the
  //  timer effectively resets for the current boot session)
  if (timedActive) timedStart = millis();

  goTo(MENU_GAME);
}

// =================================================
// LOOP
// =================================================
void loop() {
  // Cap frame rate at 15fps to reduce sustained current draw.
  // The ILI9341 + ESP32 at full SPI speed draws significant current;
  // halving the frame rate roughly halves display power consumption.
  static unsigned long lastFrame = 0;
  unsigned long now = millis();
  if (now - lastFrame < 66) {   // 66ms = ~15fps
    esp_task_wdt_reset();
    yield();
    return;
  }
  lastFrame = now;
  esp_task_wdt_reset();
  yield();

  bool touched = touchscreen.tirqTouched() && touchscreen.touched();

  // ── SLEEP / WAKE ──────────────────────────────
  if (touched) {
    if (sleeping) {
      sleeping = false;
      applyBrightness();
      lastTouchTime = millis();
      lastTouch     = true;   // swallow the wake tap
      return;
    }
    lastTouchTime = millis();
  }

  // Don't sleep while timed menu is active — timer must stay visible
  if (!sleeping && millis() - lastTouchTime > blSleepMs && !(screen == MENU_4 && timedActive)) {
    sleeping = true;
    ledcWrite(TFT_BL, 0);   // backlight off
  }

  if (sleeping) { esp_task_wdt_reset(); lastTouch = false; return; }

  // ── HEARTBEAT (debug) ────────────────────────────
  static unsigned long lastBeat = 0;
  static unsigned long frameCount2 = 0;
  frameCount2++;
  if (millis() - lastBeat > 5000) {
    Serial.printf("ALIVE screen=%d frames=%lu heap=%u\n",
      (int)screen, frameCount2, ESP.getFreeHeap());
    lastBeat = millis();
    frameCount2 = 0;
  }

  // ── HEAP MONITOR ─────────────────────────────────
  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog > 30000) {
    lastHeapLog = millis();
    Serial.printf("Heap: %u free, %u min-ever\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }

  // ── DEBOUNCED STATE SAVE ─────────────────────────
  // Flush dirty state at most every 5 seconds to avoid NVS blocking the loop
  if (_stateDirty && millis() - _lastSave > SAVE_DEBOUNCE_MS) {
    Serial.printf("SAVE start heap=%u\n", ESP.getFreeHeap());
    if (_deckDirty) saveState();
    else            saveStateFast();
    Serial.printf("SAVE done heap=%u\n", ESP.getFreeHeap());
  }

  // Keep backlight alive on all actively animating screens —
  // prevents sleep from firing while content is being displayed
  {
    bool keepAlive =
      screen == IDLE_ANIM ||
      screen == LYRICS_SCREEN ||
      screen == SONG_SELECT ||
      (timedActive && !timedPaused &&
        (screen == MENU_GAME || screen == MENU_1 ||
         screen == MENU_2    || screen == MENU_3 || screen == MENU_4));
    if (keepAlive) lastTouchTime = millis();
  }

  // ── ILI9341 D/C LINE RESYNC ──────────────────────
  // GPIO2 is TFT_DC on this board AND an ESP32 strapping pin.
  // NVS flash writes and other internal events can briefly glitch GPIO2,
  // corrupting the ILI9341's command parser and freezing the display.
  // Sending NOP + Display-ON every 2 seconds resyncs it without clearing screen.
  static unsigned long lastDCResync = 0;
  if (millis() - lastDCResync > 2000) {
    lastDCResync = millis();
    tft.startWrite();
    tft.writecommand(0x00);   // NOP — resets command state machine
    tft.writecommand(0x29);   // Display ON — ensures output stage active
    tft.endWrite();
    // Also explicitly drive GPIO2 HIGH to reinforce D/C line state
    digitalWrite(2, HIGH);
  }
  // Runs every second on ANY screen  -  handles expiry while user is elsewhere
  static unsigned long lastTimedBgTick = 0;
  if (millis() - lastTimedBgTick > 1000) {
    lastTimedBgTick = millis();
    Serial.println("BG tick start");
    tickTimedBackground();
    Serial.println("BG tick done");
  }

  // ── TIMED MENU FOREGROUND TICK ────────────────────
  // Only redraws bar/countdown when the timed screen is visible
  static unsigned long lastTimedTick = 0;
  if (screen == MENU_4 && !_needsTimedRedraw && millis() - lastTimedTick > 1000) {
    lastTimedTick = millis();
    tickTimedMenu();
  }

  // ── BLE SCAN REDRAW TICK ──────────────────────
  static unsigned long lastBLETick = 0;
  if (screen == CYBER_BLE && cyberBLEIsScanning() && millis() - lastBLETick > 2000) {
    lastBLETick = millis();
    cyberBLEDraw();
  }

  // ── TOUCH HANDLING ────────────────────────────
  if (touched && !lastTouch) {
    TS_Point p = touchscreen.getPoint();
    sx = map(p.x, 200, 3700, 0, 320);
    sy = map(p.y, 200, 3700, 0, 240);
  }

  if (!touched && lastTouch) {
    // Discard the release event that triggered the last navigation —
    // it would otherwise fire as a tap or second swipe on the new screen
    if (_swallowNextRelease) {
      _swallowNextRelease = false;
      lastTouch = touched;
      return;
    }

    TS_Point p = touchscreen.getPoint();
    int x  = map(p.x, 200, 3700, 0, 320);
    int y  = map(p.y, 200, 3700, 0, 240);
    int dx = x - sx;
    int dy = y - sy;

    // ── SWIPE ────────────────────────────────────
    if (abs(dx) > 30 || abs(dy) > 30) {
      lastInteractionTime = millis();

      // Settings screens: no swipe navigation  -  use back buttons
      bool inSettings = (screen == SETTINGS || screen == SETTINGS_TIMER ||
                         screen == SETTINGS_IDLE || screen == SETTINGS_BRIGHT ||
                         screen == SETTINGS_SPEED || screen == SETTINGS_DIFFICULTY ||
                         screen == SETTINGS_CATEGORIES || screen == RESET_CONFIRM ||
                         screen == HISTORY_SCREEN);
      if (inSettings) return;

      // Tools menus and tool screens: no swipe navigation  -  use back buttons
      bool inToolArea = (screen == TOOLS_MENU || screen == CYBER_MENU ||
                         screen == TOOL_COIN   || screen == TOOL_TAROT ||
                         screen == TOOL_ORACLE || screen == TOOL_TIMER ||
                         screen == TOOL_NAMES  || screen == TOOL_CIPHER ||
                         screen == DICE_ROLL   || screen == GAME_PICKER);
      if (inToolArea) return;

      // Lyrics screen: swipe down exits back to prescript, swipe up restarts
      if (screen == SONG_SELECT) {
        if (dy > 30) goTo(loopScreens[loopIdx]);   // swipe down = back to prescript
        return;
      }
      if (screen == LYRICS_SCREEN) {
        if (dy > 30) goTo(SONG_SELECT);   // swipe down = back to song select
        return;   // all other swipes ignored
      }

      // Cyber tool screens: no swipe navigation  -  use back buttons
      bool inCyberTool = (screen == CYBER_WIFI || screen == CYBER_BLE ||
                          screen == CYBER_BT   || screen == CYBER_QR);
      if (inCyberTool) return;

      // MAIN_MENU: swipe-up exits to prescript carousel
      if (screen == MAIN_MENU) {
        if (dy < -30) exitMainMenu();
        return;
      }

      // IDLE_ANIM: any swipe wakes device
      if (screen == IDLE_ANIM) {
        idleActive = false;
        lastInteractionTime = millis();
        goToLoopScreen(loopIdx);
        return;
      }

      // Prescript carousel: horizontal = change menu, down = main menu, up = lyrics
      if (abs(dx) > abs(dy)) {
        if (dx > 0) nextScreen();
        else        prevScreen();
      } else if (dy > 30) {
        enterMainMenu();
      } else if (dy < -30) {
        // Swipe up from prescript menus → lyrics screen
        bool onPreScript = (screen == MENU_GAME || screen == MENU_1 ||
                            screen == MENU_2    || screen == MENU_3);
        if (onPreScript) goTo(SONG_SELECT);
      }
    }

    // ── TAP ──────────────────────────────────────
    else {
      lastInteractionTime = millis();

      // Any tap on idle wakes to prescript
      if (screen == IDLE_ANIM) {
        idleActive = false;
        lastInteractionTime = millis();
        goToLoopScreen(loopIdx);
      }

      // Regular prescript screens
      else if ((screen == MENU_GAME || screen == MENU_1 ||
           screen == MENU_2    || screen == MENU_3) && y > 150) {
        lastInteractionTime = millis();
        showResult(x < 160);
      }

      // Timed menu taps
      else if (screen == MENU_4) {
        lastInteractionTime = millis();
        if (!timedActive) {
          timedAssignNew(); drawTimedMenu();
        } else if (!timedPassed) {
          // PASS — left third, only when not paused
          if (y > 213 && x < 130 && !timedPaused) {
            checkRewards(4, true);
            timedPassed = true;
            markStateDirty();
            showResult(true);
          }
          // FAIL — right third, only when not paused
          else if (y > 213 && x > 200 && !timedPaused) {
            checkRewards(4, false);
            // Reset timer to full so player must wait it out before getting a new prescript
            timedStart  = millis();
            timedPaused = false;
            timedPauseRem = 0;
            timedRevealIndex = 0;   // re-scramble the same prescript
            markStateDirty();
            showResult(false);
          }
          // PAUSE/RESUME  -  centre bottom
          else if (y > 210) {
            if (timedPaused) {
              timedStart = millis() - (timerDurationMs() - timedPauseRem);
              timedPaused = false;
            } else {
              timedPauseRem = timerDurationMs() - (millis() - timedStart);
              timedPaused = true;
            }
            drawTimedMenu();
          }
        }
      }

      // Result screen
      else if (screen == RESULT && resultState == RESULT_HOLD) {
        // After timed pass, go back to timed screen (not result drain)
        if (previousScreen == MENU_4 && resultPass) {
          resultInitialized = false; resultFullyDrawn = false;
          goTo(MENU_4);
        } else if (rewardActive) {
          rewardRevealIndex = 0;
          resultState = RESULT_REWARD_REVEAL;
          rewardActive = false;
        } else {
          resultState = RESULT_DRAIN;
          if (resultPass) resultFillY = 0;
          else            resultFillY = 240;
        }
      }
      else if (screen == RESULT && resultState == RESULT_REWARD_HOLD) {
        resultState = RESULT_DRAIN;
        if (resultPass) resultFillY = 0;
        else            resultFillY = 240;
      }

      // Main menu
      else if (screen == MAIN_MENU) {
        if (hit(x, y, 95, 70,  130, 26)) goTo(TOOLS_MENU);
        if (hit(x, y, 95, 102, 130, 26)) goTo(CYBER_MENU);
        if (hit(x, y, 95, 134, 130, 26)) goTo(SETTINGS);
        if (hit(x, y, 60, 188, 200, 28)) goTo(GAME_PICKER);
      }

      // Game picker
      else if (screen == GAME_PICKER) {
        if (hit(x, y, 40,  180, 110, 28)) gamePickerStart();
        if (hit(x, y, 170, 180, 110, 28)) goTo(MAIN_MENU);
      }

      // Tools menu  -  tap item or scroll, small BACK cube
      else if (screen == TOOLS_MENU) {
        if (hit(x, y, 258, 220, 60, 20)) { goTo(MAIN_MENU); }
        else {
          const int itemH = 36, startY = 18;
          Screen targets[] = {DICE_ROLL, TOOL_COIN, TOOL_TAROT,
                               TOOL_ORACLE, TOOL_TIMER, TOOL_NAMES, TOOL_CIPHER, LYRICS_SCREEN};
          for (int i = 0; i < 8; i++) {
            int iy = startY + i * itemH - toolsScrollY;
            if (hit(x, y, 4, iy+2, 278, itemH-4)) { goTo(targets[i]); break; }
          }
        }
      }

      // Cyber menu  -  tap item or scroll, small BACK cube
      else if (screen == CYBER_MENU) {
        if (hit(x, y, 2, 220, 60, 20)) { goTo(MAIN_MENU); }
        else {
          const int itemH = 40, startY = 18;
          Screen targets[] = {CYBER_WIFI, CYBER_BLE, CYBER_BT, CYBER_QR};
          for (int i = 0; i < 4; i++) {
            int iy = startY + i * itemH - cyberScrollY;
            if (hit(x, y, 4, iy+2, 278, itemH-4)) { goTo(targets[i]); break; }
          }
        }
      }

      // Tool screens
      else if (screen == TOOL_COIN) {
        if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU);
        else toolCoinTap(x, y);
      }
      else if (screen == TOOL_TAROT) {
        // History view: all taps go to toolTarotTap (has its own buttons)
        // Card view: BACK is at (4,208,80,22), HISTORY at (236,208,80,22)
        if (!tarotInHistory() && hit(x, y, 4, 208, 80, 22)) goTo(TOOLS_MENU);
        else toolTarotTap(x, y);
      }
      else if (screen == TOOL_ORACLE) {
        if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU);
        else toolOracleTap(x, y);
      }
      else if (screen == TOOL_TIMER) {
        if (cdState == CD_SET  && hit(x, y, 170, 130, 90, 28)) goTo(TOOLS_MENU);
        else if (cdState != CD_SET && hit(x, y, 210, 170, 80, 28)) goTo(TOOLS_MENU);
        else toolCDTap(x, y);
      }
      else if (screen == TOOL_NAMES) {
        if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU);
        else toolGenTap(x, y);
      }
      else if (screen == TOOL_CIPHER) {
        if (hit(x, y, 95, 210, 130, 24)) goTo(TOOLS_MENU);
        else toolCipherTap(x, y);
      }
      else if (screen == SONG_SELECT) {
        // Tap on a song to play it
        const int itemH = 22, startY = 18;
        for (int i = 0; i < SONG_COUNT; i++) {
          int iy = startY + (i - songSelectScroll) * itemH;
          if (y >= iy && y < iy + itemH && iy >= 14 && iy < 230) {
            lyricsSelectSong(i);
            goTo(LYRICS_SCREEN);
            break;
          }
        }
      }
      else if (screen == LYRICS_SCREEN) {
        // Tap during or after song — back to song select
        if (lyricsSongDone || true) {   // tap any time returns to menu
          goTo(SONG_SELECT);
        }
      }

      // Cyber tool screens  -  BACK button + tool-specific taps
      else if (screen == CYBER_WIFI) {
        if (hit(x, y, 95, 207, 130, 24)) { cyberCleanup(); goTo(CYBER_MENU); }
        else cyberWifiTap(x, y);
      }
      else if (screen == CYBER_BLE) {
        if (hit(x, y, 95, 207, 130, 24)) { cyberCleanup(); goTo(CYBER_MENU); }
        else cyberBLETap(x, y);
      }
      else if (screen == CYBER_BT) {
        if (hit(x, y, 95, 207, 130, 24)) { cyberCleanup(); goTo(CYBER_MENU); }
        else cyberBTTap(x, y);
      }
      else if (screen == CYBER_QR) {
        if (hit(x, y, 95, 207, 130, 24)) { cyberCleanup(); goTo(CYBER_MENU); }
      }

      else if (screen == DICE_ROLL) { 
        if (hit(x, y, 10, 200, 80, 22)) goTo(TOOLS_MENU);
        else startDiceRoll();
      }

      // Settings
      else if (screen == SETTINGS) {
        if (hit(x,y,70, 38, 180,22)) { editMinutes=timerMinutes;editHours=timerHours;editDays=timerDays; goTo(SETTINGS_TIMER); }
        if (hit(x,y,70, 66, 180,22)) goTo(SETTINGS_IDLE);
        if (hit(x,y,70, 94, 180,22)) goTo(SETTINGS_SPEED);
        if (hit(x,y,70,122, 180,22)) goTo(SETTINGS_DIFFICULTY);
        if (hit(x,y,70,150, 180,22)) goTo(SETTINGS_CATEGORIES);
        if (hit(x,y,70,178, 180,22)) goTo(RESET_CONFIRM);
        if (hit(x,y,70,206, 180,22)) { histViewMenu=0; goTo(HISTORY_SCREEN); }
        if (hit(x,y,2,220,60,20)) goTo(MAIN_MENU);
      }

      // Idle timeout settings
      else if (screen == SETTINGS_IDLE) {
        unsigned long opts[] = {30000UL,60000UL,120000UL,300000UL,0xFFFFFFFFUL};
        for (int i = 0; i < 5; i++) {
          if (hit(x,y,20,68+i*30,280,24)) {
            sleepMs = opts[i]; saveState(); drawSettingsIdle();
          }
        }
        if (hit(x,y,110,218,100,22)) goTo(SETTINGS);
      }

      // Difficulty settings  -  progressive thresholds
      else if (screen == SETTINGS_DIFFICULTY) {
        // Normal threshold
        if (hit(x,y,260,56,22,18)) { if(diffMedThreshold>1)diffMedThreshold--; saveState(); drawSettingsDifficulty(); }
        if (hit(x,y,284,56,22,18)) { diffMedThreshold++; if(diffMedThreshold>=diffHardThreshold)diffHardThreshold=diffMedThreshold+1; saveState(); drawSettingsDifficulty(); }
        // Hard threshold
        if (hit(x,y,260,88,22,18)) { if(diffHardThreshold>diffMedThreshold+1)diffHardThreshold--; saveState(); drawSettingsDifficulty(); }
        if (hit(x,y,284,88,22,18)) { diffHardThreshold++; saveState(); drawSettingsDifficulty(); }
        if (hit(x,y,110,220,100,22)) goTo(SETTINGS);
      }

      // Category settings (timed pool sources)
      else if (screen == SETTINGS_CATEGORIES) {
        for (int i=0; i<4; i++) {
          int by=48+i*38;
          if (hit(x,y,30,by,260,30)) {
            poolToggle(i);
            saveState();
            drawSettingsCategories();
          }
        }
        if (hit(x,y,110,224,100,20)) goTo(SETTINGS);
      }

      // Animation speed settings
      else if (screen == SETTINGS_SPEED) {
        // GLYPH row  y=34  buttons at y+10=44, h=22
        if (hit(x, y, 10,  44, 96, 22)) { cfg_glyphSpinMs=270; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 112, 44, 96, 22)) { cfg_glyphSpinMs=90;  saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 214, 44, 96, 22)) { cfg_glyphSpinMs=30;  saveState(); drawSettingsSpeed(); }
        // REVEAL row  y=100  buttons at y+10=110
        if (hit(x, y, 10,  110, 96, 22)) { cfg_revealChars=1; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 112, 110, 96, 22)) { cfg_revealChars=2; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 214, 110, 96, 22)) { cfg_revealChars=4; saveState(); drawSettingsSpeed(); }
        // TRANSITION row  y=166  buttons at y+10=176
        if (hit(x, y, 10,  176, 96, 22)) { cfg_transitionPx=5;  saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 112, 176, 96, 22)) { cfg_transitionPx=10; saveState(); drawSettingsSpeed(); }
        if (hit(x, y, 214, 176, 96, 22)) { cfg_transitionPx=25; saveState(); drawSettingsSpeed(); }
        // BACK
        if (hit(x, y, 95, 210, 130, 24)) goTo(SETTINGS);
      }

      // Timer settings
      else if (screen == SETTINGS_TIMER) {
        // Days
        if (hit(x, y, 10, 55, 30, 22)) { editDays++;            drawSettingsTimer(); }
        if (hit(x, y, 10, 79, 30, 22)) { if (editDays>0) editDays--; drawSettingsTimer(); }
        // Hours
        if (hit(x, y, 118, 55, 30, 22)) { editHours = (editHours+1) % 24; drawSettingsTimer(); }
        if (hit(x, y, 118, 79, 30, 22)) { editHours = (editHours+23) % 24; drawSettingsTimer(); }
        // Minutes
        if (hit(x, y, 222, 55, 30, 22)) { editMinutes = (editMinutes+1) % 60; drawSettingsTimer(); }
        if (hit(x, y, 222, 79, 30, 22)) { editMinutes = (editMinutes+59) % 60; drawSettingsTimer(); }
        // Save
        if (hit(x, y, 20, 148, 130, 26)) {
          if (editDays==0 && editHours==0 && editMinutes==0) editMinutes=1;
          timerDays=editDays; timerHours=editHours; timerMinutes=editMinutes;
          saveState(); goTo(SETTINGS);
        }
        // Cancel
        if (hit(x, y, 170, 148, 130, 26)) goTo(SETTINGS);
      }

      // Brightness settings
      else if (screen == SETTINGS_BRIGHT) {
        for (int i = 0; i < 3; i++) {
          if (hit(x, y, 20 + i*100, 100, 80, 36)) {
            brightnessLevel = i;
            applyBrightness();
            saveState();
            drawSettingsBright();
          }
        }
        if (hit(x, y, 110, 170, 100, 26)) goTo(SETTINGS);
      }

      // Reset confirm
      else if (screen == RESET_CONFIRM) {
        if (hit(x, y, 20,  80,  120, 28)) { resetDeck(0, true); goTo(SETTINGS); }
        if (hit(x, y, 180, 80,  120, 28)) { resetDeck(1, true); goTo(SETTINGS); }
        if (hit(x, y, 20,  120, 120, 28)) { resetDeck(2, true); goTo(SETTINGS); }
        if (hit(x, y, 180, 120, 120, 28)) { resetDeck(3, true); goTo(SETTINGS); }
        if (hit(x, y, 20,  160, 120, 28)) { resetDeck(4, true); timedActive=false; timedPassed=false; saveState(); goTo(SETTINGS); }
        if (hit(x, y, 180, 160, 120, 28)) {
          for (int i=0; i<5; i++) resetDeck(i, true);
          timedActive=false; timedPassed=false; saveState(); goTo(SETTINGS);
        }
        if (hit(x, y, 95, 200, 130, 22)) goTo(SETTINGS);
      }

      // History screen  -  tab bar + back
      else if (screen == HISTORY_SCREEN) {
        for (int i = 0; i < 5; i++) {
          if (hit(x, y, i*64, 0, 64, 24)) {
            histViewMenu = i; drawHistoryScreen();
          }
        }
        if (hit(x, y, 110, 205, 100, 26)) goTo(SETTINGS);
      }
    }
  }

  lastTouch = touched;

  // drawTimedMenu is expensive — only call it from the main loop, never from ticks
  if (_needsTimedRedraw) {
    _needsTimedRedraw = false;
    drawTimedMenu();
    return;
  }

  // ── PER-FRAME DRAWS ──────────────────────────
  if (screen == IDLE_ANIM && idleActive) {
    static unsigned long lastIdleTick = 0;
    if (millis() - lastIdleTick > 100) {
      lastIdleTick = millis();
      tickIdleAnim();
    }
  }

  if (screen == LYRICS_SCREEN) {
    tickLyricsScreen();
  }

  if (screen == GAME_PICKER && !gamePickerSettled) {
    tickGamePicker();
  }

  // Idle trigger  -  only on prescript screens, after configured idle timeout
  if ((screen == MENU_GAME || screen == MENU_1 ||
       screen == MENU_2    || screen == MENU_3) &&
      !idleActive &&
      sleepMs != 0xFFFFFFFFUL &&
      millis()-lastInteractionTime > sleepMs) {
    idleActive = true;
    goTo(IDLE_ANIM);
  }

  if (screen == MENU_GAME || screen == MENU_1 ||
      screen == MENU_2    || screen == MENU_3) {
    int totalChars = 0;
    for (int l = 0; l < lineCount; l++) totalChars += strlen(lines[l]) + 1;

    bool wasComplete = (revealIndex >= totalChars);

    static unsigned long lastRevealTick = 0;
    if (millis() - lastRevealTick >= GLYPH_SPIN_MS) {
      lastRevealTick = millis();
      int prevIdx = revealIndex;
      for (int _r = 0; _r < REVEAL_CHARS_PER_TICK; _r++)
        if (revealIndex < totalChars) revealIndex++;
      if (revealIndex >= totalChars && prevIdx < totalChars)
        tft.fillRect(0, logoBottomY + 4, 320, 214 - (logoBottomY + 4), BG_COLOR);
      int mi = screenToMenuIdx(screen);
      if (mi >= 0 && mi <= 3) menuRevealIndex[mi] = revealIndex;
    }

    bool nowComplete = (revealIndex >= totalChars);
    // Only draw prescript text during reveal, or on the single frame reveal just completed
    if (!nowComplete || !wasComplete) {
      drawPrescript();
    }

    // Always update the timed countdown top-right regardless of reveal state
    if (timedActive) drawTimedCountdown(BG_COLOR);
  }

  // Timed prescript reveal  -  identical mechanics to regular menus.
  // Reveal advances once per GLYPH_SPIN_MS; glyph redraw happens every frame
  // so unrevealed chars scramble at the same rate as regular prescript menus.
  if (screen == MENU_4 && timedActive && timedLineCount > 0) {
    int totalTimedChars = 0;
    for (int l = 0; l < timedLineCount; l++) totalTimedChars += strlen(timedLines[l]) + 1;

    bool wasComplete = (timedRevealIndex >= totalTimedChars);

    static unsigned long lastTimedRevealTick = 0;
    if (millis() - lastTimedRevealTick >= GLYPH_SPIN_MS) {
      lastTimedRevealTick = millis();
      for (int _r = 0; _r < REVEAL_CHARS_PER_TICK; _r++)
        if (timedRevealIndex < totalTimedChars) timedRevealIndex++;
    }

    bool nowComplete = (timedRevealIndex >= totalTimedChars);
    // Only draw text during reveal, or on the single frame reveal just completed
    bool shouldDraw = !nowComplete || !wasComplete;

    if (shouldDraw) {
      uint16_t tc = timedIsRare ? RARE_COLOR : TM_TEXT;
      const int cw=12, ch=18, availBot=204;
      int tH = timedLineCount * ch;
      int cY = logoBottomY;
      int sY = cY + max(0, (availBot - cY - tH) / 2);
      int idx2=0;
      tft.startWrite();
      tft.setTextSize(2);
      for (int l=0;l<timedLineCount;l++) {
        int llen=strlen(timedLines[l]);
        int lx=(320-llen*cw)/2;
        int ly=sY+l*ch;
        if (ly+ch > availBot) break;
        for (int i=0;i<llen;i++) {
          bool locked = (idx2 < timedRevealIndex);
          char c  = locked ? timedLines[l][i] : (char)random(33,126);
          uint16_t col = locked
            ? (timedPassed ? TM_BAR_PASS : tc)
            : tft.color565(30,60,70);
          tft.setTextColor(col, TM_BG);
          tft.setCursor(lx+i*cw,ly); tft.print(c);
          idx2++;
        }
        idx2++;
      }
      tft.endWrite();
    }
  }

  if (screen == RESULT)    drawResult();
  if (screen == DICE_ROLL) drawDiceRoll();

  // Tool screen ticks
  if (screen == TOOL_COIN)   toolCoinTick();
  if (screen == TOOL_TAROT)  toolTarotTick();
  if (screen == TOOL_ORACLE) toolOracleTick();
  if (screen == TOOL_TIMER)  toolCDTick();
  if (screen == TOOL_NAMES)  toolGenTick();

  frameCount++;
}