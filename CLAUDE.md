# beeper — Codebase Reference

Arduino/ESP32 sketch for the **ESP32-2432S028R "Cheap Yellow Display"** (320×240 ILI9341 TFT
via TFT_eSPI + XPT2046 resistive touch). It's a handheld novelty device: a touchscreen
"prescript" card game/habit-nudge tool, plus a grab-bag of side tools (tarot, oracle, cipher,
coin flip, countdown timer, word generator), passive WiFi/BLE/BT scanners, a QR display, and
a lyrics viewer for a set of original songs.

No RTOS framework beyond stock Arduino `setup()`/`loop()`. Single-threaded, polls touch every
frame, capped at ~15fps.

## File map

| File | Lines | Contents |
|---|---|---|
| `beeper.ino` | ~6060 | Everything: hardware setup, all screens, state, prescript data, game logic, persistence, `setup()`/`loop()` |
| `cyber.h` | ~560 | WiFi/BLE/BT scanners + QR code generator (self-contained, `#include`d into the .ino) |
| `tools.h` | ~1085 | Coin flip, tarot, oracle, countdown timer, word generator, cipher wheel |
| `songs.h` | ~560 | Lyrics text data for 11 songs, consumed by the lyrics screen in `beeper.ino` |
| `index.h` | 2565 | One `const uint16_t index_565[] PROGMEM` array — 320×96 RGB565 boot-logo bitmap. No logic. |

`cyber.h`, `tools.h`, `songs.h` all rely on `extern` globals/functions defined in `beeper.ino`
(`tft`, `prefs`, `drawButton`, `hit`, `wrapText`, `cfg_glyphSpinMs`), so they can't be compiled
standalone — order of `#include` in `beeper.ino` matters (`index.h`, `cyber.h`, `tools.h` near
the top; `songs.h` is included later, inside the lyrics section, around line 2063).

## Hardware / boot gotchas (don't remove these without understanding why)

- **GPIO2 = TFT_DC and an ESP32 strapping pin.** Forced `OUTPUT`/`HIGH` in `setup()`, and
  re-driven HIGH every 2s in `loop()` (the "D/C LINE RESYNC" block, ~line 5499) alongside a
  NOP + Display-ON command pair. This works around flash-write glitches corrupting the display
  command state and freezing the screen.
- **Logo bitmap is copied PROGMEM→DRAM at boot** (`logoBuf`, `setup()` ~line 5364). Reading
  flash via `pushImage` while NVS (Preferences) writes flash simultaneously can stall the flash
  cache and hang the chip indefinitely — so the logo is read from RAM instead at runtime.
- **Backlight PWM is inverted**: `BL_LEVELS[3] = {180, 80, 0}` — duty 0 = full brightness, 255 =
  off, because GPIO21 drives the backlight through an inverting NPN transistor.
- **Frame rate capped at ~15fps** (66ms) in `loop()` to reduce sustained current draw.
- **Task watchdog**: 30s timeout, non-panicking, fed every loop iteration.
- WiFi/BT are explicitly disabled at boot (`WiFi.mode(WIFI_OFF); btStop();`) to free heap, and
  re-enabled on demand only inside the cyber tools (which call `cyberCleanup()` on exit to shut
  radios back off).

## Global navigation model

### `enum Screen` (beeper.ino ~line 231)

```
MENU_GAME, MENU_1(CITY), MENU_2(CON), MENU_3(TASK), MENU_4(TIMED),   // prescript carousel
MAIN_MENU, SETTINGS, SETTINGS_TIMER, SETTINGS_IDLE, SETTINGS_BRIGHT,
SETTINGS_SPEED, SETTINGS_DIFFICULTY, SETTINGS_CATEGORIES, RESET_CONFIRM,
HISTORY_SCREEN, DICE_ROLL, RESULT, IDLE_ANIM, GAME_PICKER,
CYBER_MENU, CYBER_WIFI, CYBER_BLE, CYBER_BT, CYBER_QR,
TOOLS_MENU, TOOL_COIN, TOOL_TAROT, TOOL_ORACLE, TOOL_TIMER, TOOL_NAMES, TOOL_CIPHER,
LYRICS_SCREEN, SONG_SELECT
```

`loopScreens[5] = {MENU_GAME, MENU_1, MENU_2, MENU_3, MENU_4}` is the swipe carousel (always
all 5, regardless of category toggles — those only filter what TIMED draws from).

### Router: `goTo(Screen target)` (~line 2941)

Central navigation function. Sets `previousScreen`/`screen`, sets `_swallowNextRelease` (so the
touch-release that triggered navigation doesn't also register as a tap on the new screen), and
then does one giant if-chain calling each screen's `drawX()`/`initX()` entry point. **Any new
screen must be added to both `Screen` enum and this dispatch chain**, or it'll render blank.

### `loop()` structure (~line 5421)

Runs every ~66ms:
1. Frame-rate gate, watchdog reset.
2. Touch read + sleep/wake handling (backlight off after `blSleepMs` idle, except while a TIMED
   prescript is active).
3. Debug heartbeat + heap logging to Serial every 5s/30s.
4. Debounced state save (`_stateDirty` flushed at most every 5s via `SAVE_DEBOUNCE_MS`).
5. `keepAlive` check — IDLE_ANIM, LYRICS_SCREEN, SONG_SELECT, and any active TIMED prescript
   reset the sleep timer every frame so they never blank mid-animation.
6. GPIO2/D-C resync every 2s (see gotchas above).
7. Background timed-menu tick every 1s (`tickTimedBackground`, runs regardless of visible screen
   so the timer can flip pass/fail eligibility even while you're elsewhere).
8. Touch dispatch: **swipe** (>30px movement) vs **tap** (release without much movement),
   branched by `screen`. This is the largest block (~line 5539–5934) — a long `if/else if`
   chain per screen handling button hit-tests (`hit(x,y,bx,by,bw,bh)`) and screen-specific taps
   delegated to `tool*Tap()` / `cyber*Tap()` functions from the header files.
9. Per-frame draws/ticks: idle animation, lyrics ticker, game picker ticker, prescript
   glyph-reveal animation (regular menus + TIMED menu each have their own reveal loop), result
   screen, dice roll, and tool-screen ticks (`toolCoinTick`, `toolTarotTick`, etc.).

Swipe rules are screen-dependent: settings/tools/cyber screens disable swipe nav entirely (back
buttons only); the prescript carousel uses horizontal swipe to change category, down-swipe to
main menu, up-swipe to lyrics; lyrics/song-select only respond to swipe-down (go back).

## Prescript / game system

This is the core "gameplay" — four themed menus (GAME, CITY, CON, TASK) each present a random
prompt card; user swipes/taps PASS or FAIL; stats track streaks and history.

### Data (all `const char*[]` arrays, beeper.ino lines ~400–1600)

Each menu has 4 pools: `easy*`, `*Prescripts` (= "normal" pool, awkwardly named), `hard*`,
`rare*` — 100/100/100/6 entries respectively, e.g. `easyGame`/`gamePrescripts`/`hardGame`/
`rareGame` for MENU_GAME. Counts are `EASY_GAME_COUNT`, `GAME_POOL_COUNT`, `HARD_GAME_COUNT`,
`RARE_GAME_COUNT` (same naming pattern for CITY/CON/TASK). MENU_4 (TIMED) reuses the GAME pools
plus whichever of the other three are enabled via `timedPoolEnabled` (bitmask, Settings →
Categories).

Rewards/punishments (~line 1584–1691): `rewards[4][2]` = `{menuRewards, menuPunishments}` per
menu, 6 entries each, shown on the RESULT screen occasionally via `checkRewards()`.

### Difficulty

`menuDiffFor(menuIdx)` (~line 300) derives 0=Easy/1=Normal/2=Hard from `passCount[menuIdx]`
against `diffMedThreshold`/`diffHardThreshold` (both configurable, Settings → Difficulty,
global across all menus). Rare cards are drawn independently of this (see generative engines).

### Deck mechanic

`decks[5][MAX_DECK]` + `deckPos[5]`: each menu's normal-difficulty pool is shuffled
(`shuffleDeck`) and drawn without replacement (`drawCard`) until exhausted, then reshuffled —
a no-repeat-until-exhausted card draw, not pure random.

### Generating a prescript

`generateGamePresc` / `generateCityPresc` / `generateConPresc` / `generateTaskPresc`
(~lines 3288–3616) — each picks difficulty via `menuDiffFor`, rolls a small chance for "rare",
and otherwise draws from the appropriate easy/normal/hard pool (normal pool via `drawCard`'s
deck, easy/hard via plain `random()`). `loadPrescript()` (~3628) calls the right generator based
on `screen`, stores into the per-menu persistent buffers (`menuCurrent[4]`, `menuLines[4][20]`,
etc. — **each of the 4 menus keeps its own currently-displayed card independently**, so
switching menus via swipe doesn't lose your place). `cyclePrescript(menuIndex)` forces a new
draw (called after pass/fail). `showStoredPrescript()` just redisplays what's already stored
for the current menu without generating anything new.

### PASS/FAIL → RESULT screen

Tapping bottom-left (PASS) / bottom-right (FAIL) on a prescript screen (`y > 150`) calls
`showResult(bool pass)` → `goTo(RESULT)`. `drawResult()` runs a small state machine
(`enum ResultAnimState`, ~line 1928):

```
RESULT_FILL → RESULT_REVEAL → RESULT_HOLD → (tap) → [RESULT_REWARD_REVEAL/HOLD] → RESULT_DRAIN → back to prescript/TIMED
```

Fill/drain speeds are user-configurable (`cfg_transitionPx`, Settings → Animation Speed).
`checkRewards()` decides whether a reward/punishment banner interjects before the drain.

### TIMED menu (MENU_4)

Distinct subsystem — a prescript with a countdown (`timerDays/Hours/Minutes` → `timerDurationMs()`).
`timedAssignNew()` picks a card from GAME + whichever pools are toggled on; `drawTimedMenu()` is
the expensive full redraw (only ever called from `loop()`, never from a tick, per an explicit
comment ~line 5938); `tickTimedMenu()` is the lightweight per-second foreground update (bar +
countdown text) and `tickTimedBackground()` runs every second on *any* screen so timer expiry /
pass-eligibility is tracked even when you're not looking at MENU_4. Pause/resume math is in the
tap handler (~line 5665) and mirrored in `loadState()`/`saveStateFast()` (timer always persists
paused across reboot since there's no RTC to recover elapsed wall time).

## Idle animation

After `sleepMs` (configurable) of no interaction on a prescript screen, `screen` flips to
`IDLE_ANIM` (not to be confused with backlight sleep, which is a separate `blSleepMs` timer that
turns the backlight off but doesn't change `screen`). `idleTexts[]` (99 short phrases, themed:
mindfulness / "Index"-flavoured spy fiction / system-status) spawn as `IdlePart` particles
(`IDLE_N = 4` concurrent) plus a scanline sweep. Any touch wakes back to the carousel.

## Lyrics / songs screen

`songs.h` defines `lyrics_<SONG>[]` string arrays (one line per array entry; `""` = stanza
break, `"!prefix"` = emphasis line, rendered larger/centred) plus `LYRICS_<SONG>_COUNT`. The
`.ino` (lines ~1980–2938) wraps each line into a `LyricsSlot` with a random drift direction,
handles reveal/hold/erase phases per line, and layers a per-song **visual effect** chosen by
name match in `fxTickEffect()` (~2810): stars, fire, feathers, cracking glass, waves, bubbles,
a sword-slash sting, glitch, heartbeat pulse, or a "PASS ON"-specific effect. `SONG_SELECT`
screen lists all songs (`SONG_COUNT`, catalogue built in `.ino` around line 2058); tapping one
calls `lyricsSelectSong(idx)` then `goTo(LYRICS_SCREEN)`.

## Persistence (NVS via `Preferences`, namespace `"pscript"`)

- `saveStateFast()` — counters, streaks, history ring buffers, all settings, TIMED runtime state,
  tarot deck position. Called on every settings change and debounced (max 1/5s) for gameplay
  state via `markStateDirty()`.
- `saveState()` — also writes the 5 shuffled deck arrays (`deck0`..`deck4`); only called when a
  deck actually changes (`markDeckDirty()`), since these are the largest writes.
- `loadState()` — reverse of the above; first-boot (`!hasData`) path double-shuffles all 5 decks
  and initializes history to `-2` sentinel (distinguishes "never played" from "rare event", which
  is stored as `-1`).
- Key prefixes: `pos/pas/fai/str/bst/hhd` + digit 0–4 = per-menu deck position / pass / fail /
  streak / best-streak / history-head; `hist0`–`hist4` = 10-entry history ring buffers;
  `tmin/thr/tdy` = timer duration; `brt/slp` = brightness/idle sleep; `spg/spr/spt` = animation
  speed (glyph spin / reveal chars / transition px); `dmed/dhrd` = difficulty thresholds; `tpol`
  = category bitmask; `tact/tpass/tpaus/trem/tcur/trare/tpool/tcidx` = TIMED menu runtime state;
  `trtd/trtp` = tarot deck + position (also saved separately by `tools.h`'s `toolsSave()`).

## `tools.h` — fun tools (all self-contained state machines, `tool<Name>Draw/Tap/Tick/Init`)

| Tool | Screen | Notes |
|---|---|---|
| Coin flip | `TOOL_COIN` | Simple spin animation, 24-frame flip |
| Tarot | `TOOL_TAROT` | 22 Major Arcana, no-repeat shuffle deck (persisted), scramble-reveal text identical timing to main prescript reveal, 10-entry history sub-screen |
| Oracle | `TOOL_ORACLE` | 20-response "magic 8-ball", orb visual |
| Countdown timer | `TOOL_TIMER` | Separate from the TIMED prescript menu; H/M/S set → run → flash under 10s remaining |
| Word generator | `TOOL_NAMES` | 4 categories (noun/adj/verb/name), 20 words each |
| Cipher wheel | `TOOL_CIPHER` | Caesar shift, on-screen A–Z + space + backspace keypad, live encode/decode |

Shared helpers: `toolReveal`/`toolWrap`/`toolWrapWide` mirror the main sketch's scramble-reveal
and word-wrap logic (duplicated rather than shared — if you fix a wrapping bug in one, check the
other implementations in `beeper.ino` too, e.g. `wrapText`/`wrapTextWide` ~line 3081).

## `cyber.h` — passive network tools

All explicitly read-only / no association / no pairing / no injection (commented as such in the
header). `cyberCleanup()` must be called on exit from any cyber screen to stop BLE scan + turn
WiFi off again (already wired into the tap handlers, ~line 5790–5804).

| Tool | Screen | Notes |
|---|---|---|
| WiFi scan | `CYBER_WIFI` | `WiFi.scanNetworks`, sorted by RSSI, shows SSID/channel/encryption |
| BLE scan | `CYBER_BLE` | Continuous async scan, flags Apple manufacturer-data patterns for AirTag (0x12) / FindMy (0x0F) and Tile (mfg ID 0x00D4) as trackers, dedup by MAC |
| BT Classic scan | `CYBER_BT` | 5s `BluetoothSerial` inquiry, device class decoded from COD bits |
| QR display | `CYBER_QR` | Hand-rolled QR v1 ECC-L encoder (GF(256) Reed-Solomon tables included) — 17-char max, fixed mask pattern 0. Text set once via `cyberQRInit()` in `setup()` (currently `https://user.xmission.com/~emailbox/catstuff.htm`) |

## Known duplication / things to watch when editing

- Word-wrap and scramble-reveal logic exists in **three** places (main `.ino`, `tools.h`,
  implicitly re-derived in `cyber.h`'s row helpers) — not shared via a common header.
- `Screen` enum, `goTo()` dispatch, and the giant tap-handling `if/else if` chain in `loop()`
  all need to stay in sync manually when adding a screen — nothing enforces this at compile time.
- The doc-comment at `.ino` line ~4611 (`\ Layout: three fields...`) has a stray leading
  backslash that starts a line-continuation in some compilers — harmless (it's still inside a
  comment block context) but worth noting if you ever see a weird preprocessor warning there.
- `EASY_GAME_COUNT`/`GAME_POOL_COUNT`/`HARD_GAME_COUNT`/`RARE_GAME_COUNT` naming is inconsistent
  across menus in spirit (the "normal" pool has no "MED"/"NORMAL" in its name, e.g.
  `gamePrescripts`/`GAME_POOL_COUNT` vs `easyGame`/`hardGame`/`rareGame`) — easy to mis-pick the
  wrong array by pattern-matching name alone; double check against `deckCounts[]` (~line 1697)
  when in doubt.
