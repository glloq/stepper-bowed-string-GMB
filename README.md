# Stepper-Bowed-Strings-GMB

**Turn a real bowed string instrument into a MIDI-controlled robot.**

Send it MIDI notes over Wi-Fi and it plays a violin, viola, cello or double bass
for you — a stepper motor slides a "finger" along each string to pick the pitch,
a small servo lowers a spinning **friction wheel** onto the string, and a DC
motor turns that wheel to **bow** the note. Everything is configured from a **web
page in your browser**; no app to install.

> Built for the **ESP32-S3**. The brain is a portable, unit-tested C++ core; the
> ESP32 part is just the hardware glue.

---

## How it works

One **string** = one **stepper** + a **finger servo** + a **bow-press servo** +
a **friction-wheel motor** (via an H-bridge):

```
        ┌──────────────────── one string ────────────────────┐

   nut                      moving finger        spinning wheel (the bow)
    │                            ▼                      ◍  ← DC motor + H-bridge
    ╞════════════════════════════●══════════════════════╪══════════╡  ← the string
    0    1    2    3    4    5   position (mm)           ▲
    │                            ▲              descent servo lowers the
    │                     stepper motor slides  wheel onto the string and
    │                     the finger to pitch   sets the bow PRESSURE
    └─ servos:  [finger] press the string   [bowPress] press the wheel down  ┘
```

To play a note the firmware:

1. **moves** the carriage so the finger sits at the right pitch,
2. **presses** the finger with a servo,
3. **engages the bow** — lowers the wheel (descent servo) and spins the motor,
4. **sustains** the note by keeping the wheel turning,
5. **lifts** the wheel and spins the motor down when the note ends.

Up to **4 strings** run independently and in parallel, so it can play chords.

Unlike a plucked note (a single impulse), a bowed note is excited *continuously*,
so its dynamics can be shaped **while it sounds**: MIDI expression (CC11),
volume (CC7), the modulation wheel (CC1) and channel aftertouch all modulate the
**bow speed** (motor PWM) and **bow pressure** (descent servo) of every sounding
string in real time — crescendo on a held note, exactly like a real bow.

### The signal path

```
MIDI over Wi-Fi ─▶ parse ─▶ pick string & position ─▶ assign notes to strings
                                                             │
                                                             ▼
                                  per-string state machine (move → press → bow)
                                                             │
                                                             ▼
              stepper motors  +  servos (PCA9685)  +  bow motors (H-bridge / LEDC)
```

A controller such as **General-MIDI-Boop** can also ask the instrument, over MIDI
SysEx, *"how many strings do you have, what's your range, which CCs do you
understand?"* and adapt automatically.

---

## Features

- 🎻 **1–4 strings**, each with its own stepper, finger, bow-press servo and
  motorised friction-wheel bow.
- 🎵 **Automatic note allocation** — send plain MIDI notes and it spreads chords
  across the strings, or **force an exact string/position** with MIDI CC (tablature).
- 🪕 **Continuous, expressive bowing** — velocity sets the attack; CC7 / CC11 /
  CC1 / aftertouch shape the bow speed and pressure of a *sustained* note live.
- ⚙️ **One bow motor per string via an H-bridge** — `IN/IN` (two-PWM) or
  `PH/EN` (direction + PWM) drivers (DRV8871, TB6612, L298N, DRV8833, MX1508…).
- 🛰️ **Wi-Fi MIDI** — plays notes received over the network (UDP, port 5006).
- 🖥️ **Local web interface** — setup wizard, live dashboard, MIDI monitor, SysEx
  tester. Runs entirely on the ESP32, no cloud.
- 🧩 **Capability announcement (SysEx)** so a host discovers the instrument.
- 🛡️ **Safety first** — homing before any play, emergency-stop handling, endstop
  monitoring, a shared motor-enable cut, and a fail-safe boot.
- 🔧 **Servo driving your way** — a PCA9685 board over I²C (recommended, it frees
  the LEDC channels for the bow motors) *or* direct ESP32 pins.

### Instruments it already knows

Ready-made profiles live in [`instrument-profiles/`](instrument-profiles/):
**violin**, **viola**, **cello**, **double bass**. Each is a JSON file you can
tweak or copy from the web wizard.

---

## Quick start

### 1. Try the logic on your PC (no hardware)

The whole musical brain is plain C++ and runs on your laptop:

```bash
cd firmware/test
make            # builds and runs the unit-test suite
```

You should see `155 tests, … checks, 0 failures`.

### 2. Build and flash the firmware

You can use **PlatformIO** or the **Arduino IDE** — same source.

**PlatformIO**

```bash
cd firmware
./sync_web_data.sh          # copy the web UI into the LittleFS image
pio run                     # build for the ESP32-S3-DevKitC-1
pio run -t uploadfs         # upload the web interface
pio run -t upload           # flash the firmware
```

**Arduino IDE** — open `firmware/firmware.ino` (the `src/` folder is compiled
recursively). Full guide: [`docs/ARDUINO_IDE.md`](docs/ARDUINO_IDE.md).

### 3. First configuration

On first boot the ESP32 creates a Wi-Fi access point called
**`Stepper-Bowed-Strings-GMB`**. Connect to it, open the device's address in a
browser, and the **setup wizard** walks you through pins, strings, servos and bow
motors. See [`docs/FIRST_CONFIGURATION.md`](docs/FIRST_CONFIGURATION.md).

---

## Repository layout

```text
Stepper-Bowed-Strings-GMB/
├── firmware/            ESP32-S3 firmware
│   ├── src/core/        Portable C++ logic (MIDI, allocation, motion, bow, safety) — unit-tested
│   ├── src/platform/    ESP32 adapters (Wi-Fi, web server, drivers, storage)
│   ├── src/main.cpp     Hardware integration / entry point
│   └── test/            Native test suite (runs with g++)
├── web-interface/       Local web app (wizard, dashboard, MIDI monitor, SysEx tester)
├── instrument-profiles/ Example instruments (violin, viola, cello, double bass)
├── board-profiles/      Board pin maps (ESP32-S3-DevKitC-1)
├── hardware/            Reference electronics, wiring, bill of materials
├── mechanics/           Per-string mechanical design (carriage, finger, bow wheel)
└── docs/                Guides and reference (see below)
```

**Software design in one line:** a pure C++17 core (`firmware/src/core/`, no
Arduino dependency, tested on a PC) plus thin ESP32 adapters
(`firmware/src/platform/esp32/`). Details in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Documentation

| Guide | What's inside |
| ----- | ------------- |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured |
| [First configuration](docs/FIRST_CONFIGURATION.md) | Setup wizard walkthrough |
| [Web interface](docs/WEB_INTERFACE.md) | Every page of the local UI |
| [MIDI protocol](docs/MIDI_PROTOCOL.md) | Notes, continuous CC, CC string/position selection, SysEx |
| [Pin configuration](docs/PIN_CONFIGURATION.md) | GPIO assignment, H-bridge pins & the LEDC budget |
| [Calibration](docs/CALIBRATION.md) | Positions, homing, finger, bow pressure & bow speed |
| [Safety](docs/SAFETY.md) | Homing, E-stop, spinning-wheel hazards, fault handling |
| [Arduino IDE](docs/ARDUINO_IDE.md) | Building without PlatformIO |

The original specifications are the three markdown files at the repository root:
the full requirements ([`SPECIFICATION.md`](SPECIFICATION.md)), the string/position
CC selection spec ([`STRING_FRET_SELECTION.md`](STRING_FRET_SELECTION.md)), and
the SysEx capability protocol ([`SYSEX_CAPABILITIES.md`](SYSEX_CAPABILITIES.md)).

---

## Project status

**What is done and verified in CI:**

- Complete, unit-tested logic core (155 native tests, 0 failures).
- Real ESP32-S3 firmware build (PlatformIO) and a fast host compile-check.
- Every shipped instrument profile is loaded through the real firmware parser.
- Web interface (vanilla JS, no build step) and JSON profiles validated.

**Not yet done — hardware validation.** The firmware has **not** been run against
a physical instrument. STEP timing on a logic analyzer, four simultaneous axes,
bow-wheel friction/rosin behaviour, MIDI endurance, and faulty/missing/inverted
sensor behavior still need a real test bench. Treat the current state as **ready
for bench bring-up**, not for an unattended, fully-strung instrument under power.

Known limitations and roadmap are listed at the bottom of
[`docs/SAFETY.md`](docs/SAFETY.md) and throughout the docs.

### Safety note

The software emergency-stop is a convenience, **not** a substitute for a hardware
cut of the driver `ENABLE` / motor power. A friction-wheel bow is a small
spinning machine near a taut string — wire a physical E-stop that cuts stepper
**and** H-bridge motor power before putting the instrument under power. See
[`docs/SAFETY.md`](docs/SAFETY.md).
