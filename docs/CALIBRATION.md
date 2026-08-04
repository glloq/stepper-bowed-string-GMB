# Calibration procedure — Stepper-Bowed-Strings-GMB

> Sources: `SPECIFICATION.md` §12, §13, §14, §15 · Code: `core/motion/{StepperAxis.*, HomingController.*}`, `core/Types.*`, `core/configuration/Profile.h`.
> Related documents: [`WEB_INTERFACE.md`](WEB_INTERFACE.md) (wizard §10) · [`SAFETY.md`](SAFETY.md) · [`FIRST_CONFIGURATION.md`](FIRST_CONFIGURATION.md).

This document describes calibration: motor steps/mm, homing, fret positions
(theoretical and manual), and servos.

---

## 1. Assisted steps/mm calculation (§12.1)

The firmware works in **millimeters** and converts to motor steps via a
`steps/mm` factor that depends on the transmission (`StepperAxis::stepsPerMm()`),
which abstracts the mechanical type away from the rest of the code.

```cpp
enum class Transmission { BeltGt2, Screw, Custom };
```

### 1.1 Belt (GT2)

```text
                stepsPerRevolution × microsteps
stepsPerMm = ──────────────────────────────────
                    pulleyTeeth × beltPitch
```

Example: 1.8° motor (200 steps/rev), 16 microsteps, 20-tooth pulley, GT2 pitch
2 mm → `(200 × 16) / (20 × 2) = 80 steps/mm`.

### 1.2 Screw

```text
                stepsPerRevolution × microsteps
stepsPerMm = ──────────────────────────────────
                    leadPerRevolution
```

Example: 200 steps/rev, 16 microsteps, screw with 8 mm/rev lead → `3200 / 8 = 400 steps/mm`.

### 1.3 Custom value

`Custom` transmission: `customStepsPerMm` is used directly.

Relevant parameters in `AxisConfig`: `stepsPerRevolution`, `microsteps`,
`pulleyTeeth`, `beltPitchMm`, `leadPerRevolutionMm`, `customStepsPerMm`,
`invertDirection`, `minPositionMm`/`maxPositionMm`, `maxSpeedMmS`, `maxAccelMmS2`.
Conversions: `mmToSteps(mm)`, `stepsToMm(steps)`, clamping `clampToLimits(mm)`.

---

## 2. Homing (§13)

Homing is **non-blocking** and **independent** for each string
(`HomingController`, one instance per axis). On each tick it reads the sensor and
the position, and returns the motion command to apply (`HomingCommand`).

### 2.1 State machine

```text
Idle → CheckSensor → SeekFast → (SensorDetected) → Backoff →
SeekSlow → SetZero → MoveToOffset → Ready
                                       └─(fault)─► Fault
```

| State (`HomingState`) | Role |
| -------------------- | ---- |
| `Idle` | inactive |
| `CheckSensor` | verify the sensor is not already active |
| `SeekFast` | fast approach toward the sensor (`fastSpeedMmS`) |
| `Backoff` | back off after detection (`backoffMm`) |
| `SeekSlow` | slow, precise re-approach (`slowSpeedMmS`) |
| `SetZero` | set the origin |
| `MoveToOffset` | move to the rest offset (`offsetMm`) |
| `Ready` | axis ready |
| `Fault` | axis disabled |

Configuration (`HomingConfig`): `direction` (±1 toward the sensor), `fastSpeedMmS`,
`slowSpeedMmS`, `backoffMm`, `offsetMm`, `timeoutMs` (default 8000), `maxSearchMm`
(default 500), `sensorActiveHigh` (the raw electrical level is normalized
internally).

### 2.2 Detected faults (§13.2, `HomingFault`)

| Fault | Cause |
| ------ | ----- |
| `SensorActiveAtStart` | sensor active at startup |
| `SensorNotReleased` | sensor cannot be released |
| `SensorNeverReached` | sensor never reached |
| `Timeout` | timeout exceeded |
| `MaxDistanceExceeded` | maximum search distance exceeded |
| — | inconsistent activation of HOME and LIMIT (detected upstream) |

A faulty axis is disabled **without causing any unexpected movement** on the
other axes.

### 2.3 Parallel homing (§13.1)

Options: **simultaneous**, **sequential** (if power supply is limited), or **by
groups**. Each axis keeps its own `HomingController` instance.

---

## 3. Note / fret calibration (§14)

### 3.1 Tuning

Each string has: open MIDI note (`openNote`), maximum position included
(`maxFret`), and the location of each note position. Predefined tunings are
provided (violin, viola, cello, contrabass, custom), all fully editable.

### 3.2 Theoretical calculation (§14.2)

```text
position = scale length × (1 − 2^(−fret / 12))
```

Implemented in `core/Types.cpp` (`fretPositionMm(scaleLengthMm, fret)`) and
exposed by `StepperAxis::fretPositionMm(fret)`. `scaleLengthMm` = the string's
vibrating length. The note produced at a fret: `note = openNote + fret + capo + transpose`.

Example (length 330 mm): fret 12 → `330 × (1 − 2^(−1)) = 165 mm` (octave at the
middle of the string).

### 3.2a Position relative to the FDC — per-string offset

The theoretical spacing and the calibrated table are both measured **from the nut**
(fret 0 = 0). Each string then carries a single **`fretOffsetMm`** — the distance
from its HOME endstop (the FDC) to the nut — and the axis target is:

```text
absolute position (from FDC) = fretOffsetMm + (calibrated[fret]  or  theory(fret))
```

So `fretOffsetMm` places a whole fretboard relative to its FDC and shifts every
fret of that string at once; it is decoupled from the travel limit `minPositionMm`
(which is a pure clamp). In the web editor the fret table is entered nut-relative,
an **Abs (FDC)** column shows `fretOffsetMm + value`, and **Capture position**
records the live motor position (absolute) minus the offset, so it stores a
nut-relative value that stays correct if the offset is later changed.

### 3.3 Manual calibration (§14.3)

For each fret: (1) select the fret, (2) move the motor with buttons, (3) test the
note, (4) adjust the position, (5) save the exact position. **The calibrated
table takes priority over the theoretical position**: if
`AxisConfig::calibratedFretMm[fret]` is set, `fretPositionMm()` returns the
calibrated value rather than the theoretical one.

### 3.4 Compensation (§14.4)

The system allows: individual correction of a fret, correction according to the
direction of travel (mechanical play / backlash), a global offset for the string,
and forward and backward software limits (`minPositionMm` / `maxPositionMm`,
applied by `clampToLimits`).

---

## 4. Servo, bow-press & bow-motor calibration (§15)

A bowed string has two servos — a **finger** (pitch stop) and a mandatory
**bow-press** descent servo — plus one **bow motor** (a friction wheel on an
H-bridge).

### 4.0 Servo config (`ServoConfig`)

Each servo uses pulses calibrated in microseconds:

```cpp
enum class ServoSource : uint8_t { Pca = 0, DirectGpio = 1 };

struct ServoConfig {
    bool enabled;
    std::string function;         // "finger" / "bowPress" / "aux"
    int8_t stringIndex;           // owner string, -1 = shared/global

    ServoSource source;           // PCA9685 OR direct ESP32 GPIO
    uint8_t pcaBoard;             // 0..3 : up to four PCA9685 (0x40..0x43)
    uint8_t channel;              // PCA9685 channel 0..15   (source == Pca)
    int8_t  gpio;                 // ESP32 GPIO            (source == DirectGpio)

    uint16_t pulseMinUs, pulseMaxUs;
    uint16_t restUs;              // finger up / bow wheel lifted
    uint16_t activeUs;            // finger pressed / full bow pressure
    uint16_t contactUs;           // bowPress: lightest audible contact (intensity 0)
    bool inverted;
    uint16_t travelMs, settleMs;
    bool disableAtRest;
    uint16_t engageDelayMs;       // bowPress: pause after the wheel is down, before the motor spins up
};
```

The **finger** is a binary press (`restUs` up, `activeUs` down). The
**bow-press / descent servo** is proportional: `restUs` lifts the wheel clear of
the string, `contactUs` is the lightest audible touch and `activeUs` is maximum
pressure. While a note sounds the pulse is mapped between `contactUs` and
`activeUs` by the note's intensity (`bowPressureTargetUs`, unit-tested), so
`contactUs` should sit between `restUs` and `activeUs`.

### 4.0a Bow-motor config (`BowMotorConfig`)

One friction-wheel motor per string, driven through an H-bridge:

```cpp
enum class BowDriveMode : uint8_t { InIn = 0, PhaseEnable = 1 };

struct BowMotorConfig {
    bool enabled;
    int8_t stringIndex;
    BowDriveMode driveMode;       // InIn: BOWA/BOWB both PWM; PhaseEnable: BOWA=PWM, BOWB=direction
    uint32_t pwmFreqHz;           // 20 kHz default (inaudible)
    uint8_t pwmResolutionBits;    // LEDC duty resolution (10)
    uint8_t minDutyPercent;       // duty floor so the wheel turns past its dead-band (25)
    uint8_t maxDutyPercent;       // top bow speed (100)
    bool reverse;                 // invert the default rotation direction
    bool brakeOnStop;             // brake (both inputs high) vs coast on note-off
    uint16_t spinUpMs, spinDownMs;// speed slew on engage / release
};
```

The motor's GPIOs live in the pin table (`BOWA{n}`/`BOWB{n}` + the shared
`MOTOR_EN`), not here — see [`PIN_CONFIGURATION.md`](PIN_CONFIGURATION.md). The
note's intensity is mapped into `[minDutyPercent, maxDutyPercent]`
(`bowSpeedDuty`, unit-tested): a non-zero floor keeps the wheel turning even at
pianissimo.

### 4.0b Bow dynamics (velocity, and continuous modulation)

A bowed note is excited continuously, so unlike a plucked one it is shaped
**while it sounds**, along two axes:

* **Bow speed** — the wheel's PWM duty (louder / brighter when faster).
* **Bow pressure** — the descent servo's position (louder, and grittier when
  heavier; too much chokes the string).

MIDI **velocity** sets the note's *attack* intensity (through the velocity
curve). When `midi.continuousDynamics` is on, **CC7** (volume), **CC11**
(expression), **CC1** (modulation) and **channel aftertouch** re-evaluate that
intensity for every sounding string in real time — the bow speed and pressure
follow, giving a true bowed crescendo on a held note. Turn `continuousDynamics`
off to freeze each note's dynamics at its attack velocity.

### 4.0c Playback timing / latency (global, `MidiConfig`)

Three global knobs manage the delay between a MIDI Note On and the sound:

* **`noteExecutionDelayMs`** — a **fixed** delay from Note On to the note actually
  sounding; the carriage move, finger press and bow descent all happen inside this
  window, for a predictable, constant latency. `0` = play as soon as ready.
* **`fingerLeadMs`** — begin the finger descent up to this long **before** the
  carriage is estimated to reach the position (`0` = press on arrival, the safe
  default; too large it can drag the finger).
* **`bowLeadMs`** — begin lowering the wheel to a light contact up to this long
  **before** the note starts, so it is already touching when bowing begins.

All three default to `0` (strictly sequential, safe).

### 4.0d Signal source: PCA9685 or direct GPIO

Each servo independently chooses **PCA9685** (up to four boards, 64 channels) or a
**direct ESP32 GPIO** (LEDC 50 Hz), and the two can be mixed. On a full 4-string
bowed build the eight LEDC channels are taken by the bow motors, so the servos
ride the PCA9685 — the recommended layout. The validator rejects a PCA channel
used twice, a direct GPIO reserved or clashing with a motor/servo signal, a
per-string role pointing at a nonexistent string, and any configuration whose
direct servos + bow-motor PWM channels exceed the eight LEDC channels.

### 4.1 Per-string roles

| Role       | Function                                                     |
| ---------- | ------------------------------------------------------------ |
| `finger`   | press/stop the string at the position (pitch)                |
| `bowPress` | lower the friction wheel and set the bow pressure (required) |

`aux` (`stringIndex = -1`) is a shared / auxiliary actuator. A positioned string
(`maxFret > 0`) needs a `finger`; every enabled string needs a `bowPress`. A note
ends by lifting the wheel — there is no separate damper.

### 4.2 Finger & open string (§15.1 / §15.3)

Lifted / pressed positions, delay after pressing / releasing. An open string
(position 0) plays with the finger lifted; the wheel still lowers and bows it.

Recommended layout on a first PCA9685 board (16 channels): 0–3 finger presses,
4–7 bow-press (descent) servos, 8–15 auxiliaries. Each PCA9685's `/OE` output is
wired to a safety pin — see [`SAFETY.md`](SAFETY.md).
