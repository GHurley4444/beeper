# Beeper

A handheld cybersecurity/recon gadget built on the ESP32-2432S028R "Cheap Yellow Display"
(320x240 ILI9341 TFT + XPT2046 resistive touch), running fully custom firmware — no off-the-shelf
app framework, no RTOS beyond stock Arduino `setup()`/`loop()`.

## What it does

- **Passive recon tools** — WiFi scanner (sorted by RSSI), BLE scanner with AirTag/Tile tracker
  detection via manufacturer-data pattern matching, and a Bluetooth Classic scanner. All strictly
  read-only: no association, no pairing, no injection.
- **Hand-rolled QR engine** — a from-scratch QR v1 (ECC-L) encoder with its own Reed-Solomon
  GF(256) tables, rendered directly to the touchscreen.
- **Prescript game engine** — a touchscreen-driven card system across four themed menus (GAME,
  CITY, CON, TASK) plus a TIMED mode, with Easy/Normal/Hard/Rare difficulty pools, no-repeat
  shuffled decks, streak tracking, and persistent run history.
- **Lyrics viewer** — an original song catalogue with per-song visual effects (particles, glitch,
  wave, etc.) driven by a scramble-reveal text renderer.
- **Side tools** — coin flip, tarot draw, 8-ball oracle, countdown timer, word generator, and a
  Caesar cipher wheel.

## Hardware

Battery-powered build: a 3000mAh LiPo through a load-sharing charge/boost circuit (to safely
charge and power simultaneously, since the CYD has no native battery input), driving a Class-D
amp into small speakers through an RC filter, breadboard-prototyped end to end. See
`HARDWARE_INSTALL_GUIDE.md` for the full wiring walkthrough.

## Files

| File | Contents |
|---|---|
| `beeper.ino` | Main sketch — hardware setup, all screens, state, game logic, persistence |
| `cyber.h` | WiFi/BLE/BT scanners + QR code generator |
| `tools.h` | Coin flip, tarot, oracle, countdown timer, word generator, cipher wheel |
| `songs.h` | Lyrics data for the on-screen lyrics viewer |
| `index.h` | Boot logo bitmap (RGB565, PROGMEM) |
| `CLAUDE.md` | Internal architecture reference (navigation model, persistence keys, gotchas) |
| `HARDWARE_INSTALL_GUIDE.md` | Battery/amp/speaker wiring guide |

## Build

Arduino IDE (or arduino-cli) targeting the ESP32-2432S028R board definition, with the
`TFT_eSPI` library configured for this board's pinout.

## Status

Actively developed personal project — firmware and hardware build documented as they progress.
