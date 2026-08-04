# Instrument profiles

Example configuration profiles for **Stepper-Bowed-Strings-GMB**. Each file is a
full, valid instrument profile matching the JSON format of SPECIFICATION.md §20
and the firmware `gmb::Profile` (`firmware/src/core/configuration/Profile.h`).

They are meant as realistic starting points: import one from the web interface,
then calibrate homing, positions, the bow-press (descent) servos and the bow
motors for your physical build.

## Files

| File | Instrument | Strings | Tuning (MIDI) | Positions | Scale |
| ---- | ---------- | :-----: | ------------- | :-------: | :---: |
| `violin-4string.json` | Violin | 4 | G3 D4 A4 E5 — 55 62 69 76 | 24 | 328 mm |
| `viola-4string.json` | Viola | 4 | C3 G3 D4 A4 — 48 55 62 69 | 24 | 370 mm |
| `cello-4string.json` | Cello | 4 | C2 G2 D3 A3 — 36 43 50 57 | 24 | 690 mm |
| `doublebass-4string.json` | Double bass | 4 | E1 A1 D2 G2 — 28 33 38 43 | 24 | 1060 mm |

MIDI note reference: 60 = C4 (middle C). Bowed strings are **fretless**
(`instrument.fretless = true`): the "positions" column is the number of
equal-tempered semitone steps the carriage can reach along the string, not
physical frets.

## What every profile carries

Unlike a plucked build, each string has **two** actuators plus a motor:

* **A stepper axis** — slides the carriage to the pitch position (unchanged from
  the plucked design; positions are computed from the equal-tempered formula
  §14.2).
* **A finger servo** (`function: "finger"`) on PCA9685 channels `0 … N−1` —
  stops the string at the selected position.
* **A bow-press / descent servo** (`function: "bowPress"`) on channels
  `4 … 4+N−1` — lowers the friction wheel onto the string and sets the **bow
  pressure**. `restUs` = wheel fully lifted, `contactUs` = lightest audible
  contact, `activeUs` = maximum pressure. Mandatory on every enabled string.
* **A bow motor** (`bowMotors[]`) — one DC friction wheel per string, driven
  through an H-bridge. Its speed PWM sets the **bow speed**. The GPIOs live in
  the pin table as `BOWA{n}` / `BOWB{n}` (two PWM inputs in the default `inIn`
  mode) plus one shared `MOTOR_EN`.

## Conventions shared by every profile

* **Pins** follow the recommended ESP32-S3-DevKitC-1 table (§11.5) for four
  axes: `STEP` on 4/5/6/7, `DIR` on 17/18/8/9, `HOME` on 12/13/14/21, bow motors
  `BOWA` on 15/16/1/2 and `BOWB` on 10/11/38/39, I²C `SDA=40` / `SCL=41`, global
  driver `ENABLE=42`, PCA9685 `SERVO_OE=47`, and the shared H-bridge `MOTOR_EN`.
* **Servos on the PCA9685** — a bowed build fills the ESP32-S3's eight LEDC
  channels with the bow-motor PWM (4 × `inIn`), so the finger and bow-press
  servos are driven over the PCA9685, not direct GPIO.
* **Continuous dynamics on** — `midi.continuousDynamics = true`, so CC7/CC11/CC1
  and channel aftertouch shape a note's bow speed and pressure while it sounds.
* **One `strings[]` entry per string**, each with its own `homing` block.
* **Selection ranges track the instrument** — `stringFretSelection.string.maximum`
  equals the string count and `.fret.maximum` equals the largest position count.
* `calibratedFretMm` is left empty (`[]`); positions are computed from the
  theoretical formula (§14.2) until you run manual calibration (§14.3), after
  which the calibrated table takes priority.

## Editing / validating

These are plain JSON. After editing, confirm the file still parses:

```sh
python3 -m json.tool instrument-profiles/cello-4string.json > /dev/null
```

The firmware `ProfileValidator` performs the full semantic check (pin conflicts,
LEDC budget, a bow-press servo and a bow motor per string, selection bounds) when
a profile is imported.
