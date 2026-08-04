# GPIO configuration — Stepper-Bowed-Strings-GMB

> Source: `SPECIFICATION.md` §11 · Code: `firmware/src/core/board/BoardProfile.{h,cpp}`, `PinManager.{h,cpp}`.
> Related documents: [`ARCHITECTURE.md`](ARCHITECTURE.md) · [`WEB_INTERFACE.md`](WEB_INTERFACE.md) · [`CALIBRATION.md`](CALIBRATION.md).

The firmware must **never** use a single global pin list that is identical for
all boards. Each board has a profile describing the capabilities of every exposed
GPIO, which lets the Web interface filter choices by signal and by board variant,
and lets the validator block conflicts.

---

## 1. Capability model (§11.1)

Each board provides a `BoardProfile`; each GPIO in it is described by a
`PinCapability`.

```cpp
struct BoardProfile {
    std::string identifier;              // e.g. "esp32-s3-devkitc-1"
    std::string displayName;
    std::vector<PinCapability> pins;
    const PinCapability* find(int8_t gpio) const;
    std::vector<const PinCapability*> candidatesFor(SignalKind kind) const;
    bool supports(int8_t gpio, SignalKind kind) const;
};

struct PinCapability {
    int8_t gpio = -1;
    bool exposed;            // broken out on the board's connector
    bool input;
    bool output;
    bool interrupt;
    bool highSpeedOutput;    // suitable for STEP / fast switching
    bool internalPullUp;
    bool internalPullDown;
    bool adc;
    bool reserved;           // reserved by firmware policy (e.g. future USB)
    bool strapping;          // strapping pin (boot)
    bool usb;                // USB-JTAG / native USB
    bool onboardPeripheral;  // wired to an onboard peripheral (LED, UART…)
    PinPreference preference;
    std::string note;        // human-readable reason, shown in the UI
};
```

---

## 2. Color categories (§11.2)

The `PinPreference` enumeration drives the display:

| `PinPreference` | Color | Meaning | UI access |
| --------------- | ------- | ------------- | -------- |
| `Recommended` (0) | 🟢 Green | recommended | visible to everyone (beginner included) |
| `Caution` (1) | 🟡 Yellow | usable with caution | **advanced mode only**, with explanation |
| `Reserved` (2) | 🔴 Red | reserved or incompatible | **not selectable** |
| `Used` (3) | ⚪ Gray | already used (runtime state) | not selectable while assigned |

Rule: by default a beginner sees only the **green** GPIOs. Yellow GPIOs appear
only in advanced mode, with an explanation. Red GPIOs can never be selected.

---

## 3. Filtering by signal (§11.3)

The requested signal type is described by `SignalKind`. `BoardProfile::candidatesFor(kind)`
returns only the legal pins, in order of preference (recommended first); reserved
or non-exposed pins are never offered.

| `SignalKind` | Requires | Only offers GPIOs… |
| ------------ | ----- | ------------------------ |
| `Step` | `output` + `highSpeedOutput` | capable of **fast** output, not reserved, not assigned, compatible with the step generator |
| `Dir` | `output` | simple output |
| `Enable` | `output` | simple output |
| `Home` | `input` + `interrupt` + biasing | interrupt-capable input, with suitable pull or external resistor, not assigned |
| `Limit` | `input` + `interrupt` + biasing | same as `Home` (opposite end stop) |
| `Diag` | `input` | input (TMC2209 DIAG) |
| `I2cSda` | I²C | pins usable for I²C, recommended pair by default, none already used |
| `I2cScl` | I²C | same |
| `ServoOe` | `output` | safety output to the PCA9685's `/OE` |
| `BowA` | `output` + `highSpeedOutput` | bow-motor H-bridge input A (PWM), like `Step` |
| `BowB` | `output` + `highSpeedOutput` | bow-motor H-bridge input B (PWM in IN/IN, direction level in PH/EN) |
| `BowEnable` | `output` | shared H-bridge enable / `nSLEEP` (`MOTOR_EN`), a safety cut |
| `Generic` | `output` | any usable output |

For a future USB interface, GPIO19 and GPIO20 are automatically reserved.

---

## 4. ESP32-S3 restrictions (§11.4)

The pin manager knows at minimum these restrictions:

| GPIO | Restriction | Default consequence |
| ---- | ----------- | ---------------------- |
| **0, 3, 45, 46** | strapping pins (boot) | 🔴 reserved |
| **19, 20** | USB-JTAG / native USB | 🔴 reserved (future USB) |
| **26 – 32** | normally tied to Flash / PSRAM | 🔴 reserved |
| **33 – 37** | may be tied to memory depending on the variant | 🔴/🟡 depending on variant |
| **35, 36, 37** | Flash/PSRAM on some DevKitC-1 variants | 🔴 not offered without variant verification |
| **43, 44** | main UART (programming/diagnostics) | 🔴 reserved |
| **48** | onboard RGB LED (DevKitC-1) | 🔴 reserved |

> These pins are not necessarily unusable in all cases: they must be classified
> according to the **exact profile** of the selected board.

---

## 5. Recommended ESP32-S3-DevKitC-1 profile (§11.5)

`makeEsp32S3DevKitC1()` provides the reference profile. Automatic assignment
(`PinManager::autoAssign`) follows this initial plan:

| Function | Proposed GPIOs (4 strings) |
| -------- | ------------- |
| STEP 1 to 4 | 4, 5, 6, 7 |
| DIR 1 to 4 | 17, 18, 8, 9 |
| HOME 1 to 4 | 12, 13, 14, 21 |
| BOWA 1 to 4 (bow motor PWM A) | 15, 16, 1, 2 |
| BOWB 1 to 4 (bow motor PWM B / dir) | 10, 11, 38, 39 |
| I²C SDA | 40 |
| I²C SCL | 41 |
| Global ENABLE | 42 |
| PCA9685 safety output (`/OE`) | 47 |
| Bow H-bridge enable (`MOTOR_EN`) | 33 |

> This plan is an **initial software profile**, not a universal rule: it can be
> replaced from the interface (advanced mode). A full four-string build uses every
> recommended pin, so `MOTOR_EN` lands on a caution pin (33) — reassign it if your
> board variant needs 33 for memory.

### The LEDC (PWM) channel budget

The ESP32-S3 has **8 LEDC channels**, shared between the bow motors and any
direct-GPIO servos. A bow motor in the default `inIn` mode drives **two** PWM
inputs (BOWA + BOWB) = 2 channels; `phaseEnable` drives **one** (BOWA; the
direction pin is a plain level) = 1 channel. Four `inIn` motors therefore use all
eight channels, which is why:

* the string count is capped at **4** (`kMaxStrings`), and
* on a full 4-string build the finger and bow-press servos ride the **PCA9685**
  (I²C), leaving the LEDC pool for the motors.

`ProfileValidator` rejects any profile where `directServos + bowMotorPWMchannels
> 8`. Choosing `phaseEnable` for the motors frees channels for direct-GPIO servos
on a smaller build.

### Pins kept reserved by default

| GPIO | Reservation |
| ---- | ----------- |
| 19, 20 | future USB |
| 43, 44 | UART programming and diagnostics |
| 0 | startup / BOOT |
| 3, 45, 46 | strapping |
| 48 | onboard LED |
| 35, 36, 37 | dependence on the Flash/PSRAM variant |

---

## 6. Automatic assignment (`PinManager` engine)

Assignment is driven by a `PinRequest`:

```cpp
struct PinRequest {
    int stringCount = 1;
    bool useI2cServos = true;   // PCA9685 present
    bool globalEnable = true;   // a single ENABLE line for all drivers
    bool servoSafetyOe = true;  // PCA9685 /OE wired to a safety pin
    bool reserveUsb = true;     // keep GPIO19/20 free for native USB
    bool useLimitSwitches = false;
    bool useBowMotors = true;   // one H-bridge friction-wheel motor per string
    bool bowMotorEnable = true; // shared H-bridge enable / nSLEEP (MOTOR_EN)
};
```

`autoAssign()` builds a **conflict-free** configuration by following the
recommended profile, and falls back to any compatible candidate when a preferred
pin is already taken. It returns `false` if a required signal could not be placed.
Each assignment is a `PinAssignment { signal, kind, gpio }`
(e.g. `"STEP1"`, `"HOME3"`, `"SDA"`).

---

## 7. Conflict detection (§11.6)

`PinManager::validate(reserveUsb)` returns a list (empty = valid configuration)
of `PinError`. The validator prevents:

* two signals using the same GPIO;
* an input assigned to an unavailable pin;
* a `STEP` signal on a pin that is not compatible (fast output);
* the use of a reserved GPIO;
* the use of GPIO19/20 when USB is reserved;
* the selection of a Flash/PSRAM pin;
* the simultaneous use of the onboard LED and the same GPIO;
* the inadvertent override of the diagnostic port (UART).

Each error is explicit:

```cpp
struct PinError {
    std::string signal;        // e.g. "STEP3"
    int8_t gpio;
    std::string reason;        // why the pin is incompatible
    std::string suggestion;    // which pin to choose instead
    std::string conflictWith;  // which signal already uses the pin
};
```

So each error indicates: **why** the pin is incompatible, **which** pin to
choose, and **which function** already occupies the pin.

The corresponding Web API (`POST /api/pins/auto`, `POST /api/pins/validate`,
`GET /api/board/{id}`) is described in [`WEB_INTERFACE.md`](WEB_INTERFACE.md).
