# Beeper — phone app

The Beeper handheld (ESP32 "Cheap Yellow Display") recreated as an iPhone home-screen app.
It uses the same 320×240 screens, the same 5×7 pixel font, the same touch and swipe controls, and the same content.

## Stack: a web app (PWA), no build step

| | Web app / PWA (chosen) | Native Swift |
|---|---|---|
| Needs a Mac / Xcode | No | Yes |
| App Store / $99 developer account | No | Yes for keeping it installed for more than 7 days (free accounts re-sign weekly) |
| Install on iPhone | Safari → Share → Add to Home Screen | Xcode sideload or TestFlight |
| Works offline | Yes (service worker) | Yes |
| WiFi network scanning | No | No (iOS blocks this for all apps) |
| BLE scanning | No (Safari has no Web Bluetooth) | Yes, partly (CoreBluetooth hides MAC addresses; scanning stops in the background) |
| Bluetooth Classic | No | No (only MFi accessories) |

The only thing native Swift would add is a partial BLE scanner. That isn't worth requiring a Mac and yearly re-signing,
so the app is plain HTML/CSS/JavaScript: no framework, no dependencies, no build step.

## How it works

- `js/tft.js` is a software copy of the TFT_eSPI calls the firmware uses. It keeps a persistent RGB565 framebuffer and
  uses the Adafruit GFX line, circle and triangle algorithms plus the GLCD 5×7 font. Because the firmware never clears
  the screen unless it means to, the screen code ports almost line for line and comes out pixel-identical.
- Everything else mirrors a firmware section: `core.js` (globals, persistence), `prescript.js` (card game, TIMED,
  result screen), `screens.js` (router, menus, settings, D20, game picker, idle, history), `lyrics.js`, `tools.js`,
  `cyber.js`, `main.js` (`setup()`/`loop()`/touch).
- The loop runs at the device's ~15 fps, so every animation keeps its on-device speed. A release that moved more than
  30 screen pixels counts as a swipe; anything else is a tap, the same rule as the firmware.
- **Content is generated, not hand-copied.** `tools/extract_firmware.py` reads `beeper.ino`, `songs.h`, `tools.h` and
  `index.h` and writes `js/data.js` (every prescript pool, generator pool, reward, idle phrase, song, tarot card and word
  list), `js/logo.js` and the icons. After changing content in the firmware, run:

  ```
  python3 phone-app/tools/extract_firmware.py
  ```

  Then bump `VERSION` in `sw.js` so installed phones pick up the change.

## Put it on your iPhone

The app has to be served over HTTPS. Pick one:

1. **GitHub Pages:** in repo Settings → Pages, deploy from the branch, then open `https://<user>.github.io/<repo>/phone-app/`.
   Note that Pages sites are public.
2. **Any static host** (Netlify drop, Cloudflare Pages, etc.): upload the `phone-app/` folder.

Then in **Safari** open the URL, tap **Share → Add to Home Screen**, and launch it from the icon. It runs full screen
and works offline after the first load. Rotate the phone to landscape for a full-screen display.

To test on a computer: `cd phone-app && python3 -m http.server`, then open http://localhost:8000.

## Controls (same as the device)

- **Prescript carousel:** swipe left/right to switch GAME · CITY · CON · TASK · TIMED. Swipe down for the main menu,
  swipe up for songs. Tap the bottom half: left = PASS, right = FAIL.
- **Main menu:** swipe up to go back. Tools, cyber tools and settings screens use their BACK buttons.
- **Songs:** tap a song to play it. Tap or swipe down to go back.

## Saved data

State is saved in `localStorage` under `beeper.pscript`, using the same key names as the device's NVS namespace
(`pas0`, `str0`, `hist0`, `deck0`, `tmin`, `spg`, `trtd`, …). It saves within half a second of a change and again when
the app goes to the background, because iOS can close a web app without warning. Deleting the home-screen icon
deletes the saved data.

## What changed on the phone, and why

**iPhone limits, replaced with substitutes:**
- WIFI SCANNER → **NETWORK STATUS**: online state, connection info (where iOS reveals it), and round-trip time to the host.
- BLE SCANNER → **BLE / TRACKERS**: if the browser has Web Bluetooth (for example Bluefy), you can pick devices one at
  a time from the system chooser. In Safari, the screen explains how to use iOS's built-in AirTag and tracker alerts.
- BLUETOOTH SCAN → **DEVICE SCAN**: a read-only list of what the phone reveals to a web page.

**Deliberate improvements:**
- **TIMED countdown keeps running while the app is closed.** The device has no clock, so it resumes paused after a reboot.
- **Each menu keeps its current card across relaunches.** iOS restarts web apps often, and a relaunch shouldn't reroll
  your card the way a reboot does.
- **QR DISPLAY supports versions 1–4 (up to 78 characters) and has an EDIT button.** The firmware encoder is version 1
  only (17 characters), so the device shows "TEXT TOO LONG" for its own URL. The output was checked module-for-module
  against a reference encoder.
- **BRIGHTNESS** is reachable from a BRIGHT button in Settings. The firmware has this screen but no way to open it.

**Firmware bugs not carried over** (worth fixing on the device too):
- A TIMED **PASS/FAIL is counted twice**: once in the tap handler (`checkRewards(4, …)`) and again when the result
  screen starts (`drawResult` → `checkRewards(screenToMenuIdx(previousScreen), …)`).
- **Game picker** draws `random(GAME_POOL_COUNT)` (100) from a 46-entry `GAME_LIST`, so it reads past the array and
  displays "100 GAMES". The app uses the real count.
- **Song list:** 22 px rows fit only 9 songs and the list never scrolls, so FROM A PLACE OF LOVE is invisible and
  PASS ON can't be selected. The app shrinks the rows so all 11 fit.
- **History** stores only a card index, and generated cards always store 0, so every entry shows the first line of
  `gamePrescripts`. The app also saves the text you actually got and shows that.
- Several static pools hold fewer entries than their `*_COUNT` (for example `easyCon` has 92 vs 100, `easyTask` 91 vs 100).
  The generators don't use these pools, but the history screen's lookup can read past them.
- The tools menu's CHILDREN OF THE CITY item opens the lyrics screen before any song is chosen (a null pointer on the
  device). The app defaults to song 1.
- A swipe on the result screen falls through to carousel navigation. The app ignores swipes there.
- The firmware swallows the next touch release after every navigation. That compensates for the resistive panel's
  bounce, but on a phone it would eat every first tap, so it isn't copied.

## Licences

`js/font.js` is the Adafruit classic GLCD font (MIT, © Adafruit Industries). The song lyrics in `js/data.js` are copied
from the firmware. If you host the app publicly (GitHub Pages is public), you are publishing those lyrics.
