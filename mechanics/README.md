# Mechanics — reference architecture

Reference mechanical architecture for **Stepper-Bowed-Strings-GMB**
(SPECIFICATION.md §5), and how each mechanical parameter maps to the instrument-profile
fields (`firmware/src/core/motion/StepperAxis.h`, `instrument-profiles/`).

## 1. One independent channel per string (§5.1)

Every string is a self-contained channel. A stepper motor moves a **single**
finger along the string to select the note:

```text
Stepper motor
      ↓
Mechanical transmission
      ↓
Longitudinal carriage
      ↓
Single finger
      ↓
Position on the string
```

Hard invariant (§4, §6): **one movable finger per string, one stepper per
string, never shared**. Active strings = active stepper axes = movable fingers.

Per string the reference build carries (§1):

```text
1 stepper motor        1 finger-press mechanism
1 linear axis          1 bow-wheel head (motor + friction wheel)
1 carriage             1 bow-press (descent) servo
1 single finger        1 HOME reference sensor
```

## 2. Finger press (§5.2)

Once the carriage is positioned, the finger must be able to press onto the
string, hold it, lift, stay lifted while moving, and stay lifted for an open
string. The reference mechanism is **one servo per string** (PCA9685 channels
0–5). In the profile this is a servo with `function: "finger"`, using `restUs`
(lifted) and `activeUs` (pressed), plus `travelMs`/`settleMs` timing.

Open string (position 0): finger stays lifted; the wheel still lowers and bows
it. An advanced option can instead press "position 0" for specific mechanics.

## 3. Bowing the string (§5.3)

Each string is excited **continuously** by its own motorised **friction wheel**
(the "bow"), lowered onto the string by a **descent servo**:

* **Bow motor** — a DC gear motor turning a friction wheel (rosin-coated or a
  rubber/silicone O-ring), driven through an **H-bridge**. Its PWM duty is the
  **bow speed**. The GPIOs are `BOWA{n}`/`BOWB{n}` in the pin table, plus a shared
  `MOTOR_EN`. Two drive modes: `IN/IN` (two PWM inputs) or `PH/EN` (direction +
  PWM). Optional direction reversal changes the bow direction.
* **Bow-press (descent) servo** — a mandatory per-string servo
  (`function: "bowPress"`, PCA9685 channels 4–7) that lowers the wheel onto the
  string and sets the **bow pressure**: `restUs` lifts the wheel clear,
  `contactUs` is the lightest audible touch, `activeUs` is full pressure.

A note is engaged by lowering the wheel and spinning the motor, sustained while
the wheel turns, and ended by lifting the wheel — there is no separate damper.
Per string, two servo roles are defined — `finger` (press) and `bowPress`
(descent) — plus a shared `aux` role. Each string also has its own endstop: the
`HOME` reference sensor, plus an optional `LIMIT` switch at the far end.

## 3.1 Servo signal source: PCA9685 or direct GPIO

Every servo picks its own source, so an instrument can be built **with or without
a PCA9685**, or with a mix of both:

* **PCA9685** — up to **four boards** (`pcaBoard` 0–3, I²C 0x40–0x43 = 64
  channels). Use this once you exceed the ESP32's free PWM pins.
* **Direct GPIO** — the servo hangs off a free ESP32-S3 pin (LEDC 50 Hz PWM),
  handy when there is no PCA or only a couple of servos.

The web interface exposes this choice per servo and prevents channel/pin
conflicts (see [`../docs/CALIBRATION.md`](../docs/CALIBRATION.md) §4).

## 4. Transmission options (§5.1)

The carriage can be driven by any of:

* **GT2 belt** (`transmission: "beltGt2"`)
* **Trapezoidal / ball lead screw** (`transmission: "screw"`)
* **Rack and pinion** — model via `custom`
* **Cable drive** — model via `custom`
* **Experimental** — model via `custom`

## 5. The millimetre abstraction

The firmware never thinks in the transmission's native units. Everything happens
in **millimetres**, converted to motor steps by a transmission-dependent
`stepsPerMm` factor (§5.1, §12.1):

```text
position in millimetres → conversion → position in motor steps
```

This keeps note allocation, motion planning and the profiles independent of the
mechanical type. Theoretical fret positions come from the equal-temperament
formula (§14.2):

```text
position(fret) = scaleLengthMm × (1 − 2^(−fret / 12))
```

A calibrated table (`calibratedFretMm`) overrides theory when present (§14.3).

## 6. Parameter → profile-field mapping

`stepsPerMm` is computed from the transmission (SPECIFICATION.md §12.1):

**Belt (GT2):**

```text
stepsPerMm = (stepsPerRevolution × microsteps) / (pulleyTeeth × beltPitchMm)
```

**Screw:**

```text
stepsPerMm = (stepsPerRevolution × microsteps) / leadPerRevolutionMm
```

**Custom:** use `customStepsPerMm` directly.

| Mechanical quantity | Profile field (`strings[]`) | Used when |
| ------------------- | --------------------------- | --------- |
| Vibrating (scale) length | `scaleLengthMm` | always (fret geometry) |
| Transmission type | `transmission` (`beltGt2`/`screw`/`custom`) | always |
| Motor full steps per revolution | `stepsPerRevolution` (e.g. 200 for 1.8°) | always |
| Driver microstepping | `microsteps` (e.g. 16) | always |
| Pulley tooth count | `pulleyTeeth` | belt |
| Belt tooth pitch | `beltPitchMm` (GT2 = 2 mm) | belt |
| Screw lead per revolution | `leadPerRevolutionMm` (e.g. 8 mm) | screw |
| Explicit steps/mm override | `customStepsPerMm` | custom |
| Direction sense | `invertDirection` | always |
| Soft travel limits | `minPositionMm`, `maxPositionMm` | always |
| Motion profile | `maxSpeedMmS`, `maxAccelMmS2` | always |
| Open-string note | `openNote` (MIDI) | always |
| Highest reachable fret | `maxFret` | always |
| Calibrated fret table | `calibratedFretMm[]` (index = fret) | overrides theory |

### Worked example (GT2 belt)

`stepsPerRevolution = 200`, `microsteps = 16`, `pulleyTeeth = 20`,
`beltPitchMm = 2`:

```text
stepsPerMm = (200 × 16) / (20 × 2) = 3200 / 40 = 80 steps/mm
```

At 16 microsteps and 80 steps/mm the position resolution is 1/80 mm = 12.5 µm,
comfortably finer than fret spacing on every example instrument.

## 7. Homing (§13) — mechanical reference

Each axis references itself against its HOME sensor with a non-blocking state
machine (`CHECK_SENSOR → SEEK_FAST → BACKOFF → SEEK_SLOW → SET_ZERO →
MOVE_TO_OFFSET → READY`). Mechanically relevant profile fields per string
(`strings[].homing`):

| Field | Meaning |
| ----- | ------- |
| `direction` | travel direction toward the sensor (+1 / −1) |
| `fastSpeedMmS` / `slowSpeedMmS` | seek and re-approach speeds |
| `backoffMm` | retreat distance after the first trigger |
| `offsetMm` | final resting offset past zero |
| `timeoutMs` / `maxSearchMm` | fault guards |
| `sensorActiveHigh` | electrical active level (NO/NC support) |

A failing axis is disabled without disturbing the others (§13.2).
