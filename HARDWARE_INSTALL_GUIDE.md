# Installing Battery + Speaker/Amp Hardware onto the CYD

This covers physically installing your four purchased parts onto the ESP32-2432S028R
("Cheap Yellow Display" / CYD) that `beeper.ino` runs on: the LiPo battery, the PAM8403
amplifier boards, the small speakers, and the breadboard/jumper kit.

**Read this whole doc before touching a soldering iron or plugging anything in** — the
battery step in particular has a gap in your parts list that matters for safety.

## What the CYD already has on board

Research on your exact board (ESP32-2432S028R, resistive touch, ILI9341 — confirmed against
`beeper.ino`'s pin usage) turned up two things that change the plan:

1. **It already has a small onboard audio amplifier.** There's an SC8002B class-D amp chip
   wired to **GPIO26**, driving a **2-pin 1.25mm JST connector** (often silkscreened "SPEAK"
   or similar), rated around 1W into 8Ω. That's an exact match for your MECCANIXITY 8Ω 1W 40mm
   speakers. **Before wiring anything else, check your physical board for this 2-pin JST
   connector** (usually near the edge, close to the ESP32 module). If it's there, you can plug
   one speaker straight into it — no amplifier board needed for a single, basic speaker.
2. **It has no battery input, charge circuit, or "BAT+/BAT-" pads.** The official pinout for
   this board (10 broken-out pins total: VIN/5V, 3V3, GND, GPIO1, GPIO3, GPIO21, GPIO22,
   GPIO27, GPIO35, GPIO0) does not include a battery connector. USB is the only power input
   the board expects. This matters a lot for the battery step below.

GPIO26 isn't broken out to any header (it only goes to the onboard amp), and `beeper.ino`
doesn't touch it — so it's free either way.

## Part 1 — Battery: what's missing before you wire it in

**Your LiPo has a protection circuit, but that circuit only guards the cell against
over-discharge/over-current/short-circuit — it is not a charger.** Since the CYD has no
onboard charge management, wiring the raw battery straight to the board has two problems:

- **Nothing will ever charge it.** There's no CC/CV charge circuit anywhere in this system
  yet.
- **A bare 3.7–4.2V LiPo cannot reliably power the board anyway.** The CYD's onboard regulator
  needs a real 5V-ish input (it's designed for USB power); a LiPo sagging to 3.6–3.7V under
  load will brown out the display and Wi-Fi/BLE radio, or not power the board at all as the
  cell discharges below about 3.8–4V.

**You need one more part: a 3.7V→5V charger/boost module** (commonly sold as "TP4056 charging
boost module" or "lithium battery charging boost board, 3.7V to 5V, USB output," a few dollars
on Amazon/AliExpress). This lets you charge the cell via USB *and* boosts it to a clean 5V to
feed the CYD's VIN pin.

**Connector mismatch to plan for:** your battery's JST connector is **1.25mm pitch**. Most
cheap charge/boost modules use the far more common **2.0mm JST-PH** pitch — physically
different, the plugs won't mate. Practically, the simplest fix is to **cut the 1.25mm JST plug
off the battery leads and solder the bare wires directly to the module's B+/B- pads**, rather
than hunting for a pitch-matched connector. If you do this:

1. Cut and strip **one wire at a time** — never cut both simultaneously, to avoid shorting the
   pack across your cutters.
2. **Verify polarity with a multimeter before soldering.** JLJLUP doesn't guarantee red=+ /
   black=- lead colors on every batch; check continuity/voltage against the connector's own
   marking or datasheet rather than assuming.
3. Solder red/+ to the module's `B+` pad and black/- to `B-`. Reversed polarity into a charge
   module can damage the battery or start a fire — this is the one step in this whole job
   worth double-checking twice.

Once wired: battery → charge/boost module → module's 5V/GND output → CYD's `VIN` (5V, on the
P1 header) and `GND`. Plug the module into USB to charge; unplug USB and it runs off the
battery.

## Part 2 — PAM8403 amplifier + speakers

If your board's onboard SC8002B/GPIO26 speaker jack covers your needs for a single speaker,
you can skip this section entirely. Use the PAM8403 boards if you want more volume, a second
independent speaker, or to reserve GPIO26 for something else later.

**PAM8403 pin labels** (HiLetgo board): `VCC`, `GND` on the power side; `L`/`R` (sometimes
`L_IN`/`R_IN`) audio inputs; `L+`/`L-` and `R+`/`R-` speaker outputs.

1. **Power:** `VCC` → the same 5V rail feeding the CYD's VIN (straight off the boost module
   or USB), `GND` → shared ground with the CYD and the battery module. All three grounds
   (battery module, CYD, PAM8403) must be tied together, or the audio will be noisy/silent.
2. **Audio input:** the CYD doesn't have a real DAC output free on its header pins, so drive
   one of the two genuinely free GPIOs — **GPIO22 or GPIO27** (both on the CN1 header) — as a
   PWM "tone" pin from firmware, then clean it up with a simple **RC low-pass filter** on the
   breadboard before it reaches the PAM8403's `L` input: a **1kΩ resistor in series, then a
   0.1µF capacitor from that junction to GND**. This is the standard cheap way to get
   listenable audio out of a plain PWM pin. Without new firmware to actually output tones on
   that pin, this will be silent — see "What this doesn't include" below.
3. **Speaker output:** `L+`/`L-` → one speaker's two terminals. **Never connect either output
   terminal to ground** — PAM8403 outputs are bridge-tied (floating differential pairs), and
   grounding one side can damage the chip. If you want a second speaker, repeat with `R+`/`R-`
   and feed the same (or a second) filtered signal into `R_IN`.
4. Leave the unused L/R side's input floating or tied to GND through its own cap if you're
   only using one channel — don't leave speaker outputs unconnected while driving that
   channel's input, but it's fine to leave a whole unused channel (input and output both
   disconnected).

See the diagram above for how all of this connects together.

## Breadboard layout suggestion

Use one of your BOJACK breadboards purely for the RC filter + PAM8403 prototyping (not the
battery — solder that connection directly, since it's permanent and current-carrying):

- Power rail rows: run 5V and GND from the boost module across the top rail; jump GND to a
  second row if you need more ground tie-points (PAM8403 GND, filter cap GND, CYD GND all
  land here).
- Place the PAM8403 board straddling the centre gap so its two rows of pins are addressable
  from both sides.
- Build the resistor + capacitor filter in a few free columns near GPIO22/27's jumper wire,
  feeding the PAM8403's `L` pin.
- Once you're happy with the sound, this is a good candidate to solder onto perfboard for a
  permanent build — breadboard connections work loose easily in a handheld device.

## What this doesn't include

This is a hardware-only guide. `beeper.ino` currently has **no audio code at all** — no
`tone()`, no LEDC PWM setup, nothing touches GPIO22/26/27 for sound. Once the hardware above
is wired up, making it actually beep/play tones is a firmware task (adding an LEDC PWM tone
generator on your chosen GPIO, e.g. as a chime on PASS/FAIL in the prescript game). Happy to
build that once the hardware's in place — just say the word.

## Quick safety recap

- Verify LiPo polarity with a multimeter before soldering — don't trust wire color alone.
- Cut battery leads one at a time, never both at once.
- Common ground between battery module, CYD, and PAM8403 is required, not optional.
- Never tie a PAM8403 speaker output terminal to ground.
- Don't wire the bare battery straight to CYD's VIN — it needs the boost module in between.

## Sources

- [ESP32 Cheap Yellow Display (CYD) Pinout (ESP32-2432S028R) – Random Nerd Tutorials](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)
- [ESP32-2432S028 (CYD): high-resolution pinout, datasheet, schema and specs – Mischianti](https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs/)
- [CYD ESP32-2432S028 Pinout, Specs & Features – ESPboards](https://www.espboards.dev/esp32/cyd-esp32-2432s028/)
- [Speaker for CYD – witnessmenow/ESP32-Cheap-Yellow-Display Discussion #99](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/discussions/99)
- [How to enable sound/Speaker for ESP32-2432S028 CYD – schreibfaul1/ESP32-audioI2S Issue #1011](https://github.com/schreibfaul1/ESP32-audioI2S/issues/1011)
- [Understanding Connectors and Pinout, CYD ESP32-2432S028R – Kafkar Projects](https://kafkar.com/projects/smart-home/understanding-connectors-and-pinout-cheap-yellow-display-boardcyd-esp32-2432s028r/)
- [PAM8403 Connection: Speaker and Audio Input Wiring Guide – Soldering Mind](https://solderingmind.com/pam8403-connection/)
- [PAM8403 IC: PinOut, Datasheet, Equivalents & Its Working – WatElectronics](https://www.watelectronics.com/pam8403-ic/)
- [PWM Audio – ESP-IoT-Solution documentation (Espressif)](https://espressif-docs.readthedocs-hosted.com/projects/espressif-esp-iot-solution/en/latest/audio/pwm_audio.html)
- [ESP32 and PWM – Simple DIY Electronic Music Projects](https://diyelectromusic.com/2024/03/31/esp32-and-pwm/)
