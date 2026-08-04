# Hardware — reference electronics

Reference electronics for **Stepper-Bowed-Strings-GMB**, the ESP32-S3 MIDI
machine that positions a stepper-driven finger per string and bows each string
with a motorised friction wheel (1–4 strings, fretless). This document describes
the reference architecture of SPECIFICATION.md §7; the wiring guide, bill of
materials and Phase 5 CAD deliverables live alongside it.

## Directory

```
hardware/
├── README.md          ← this file (electronics overview, §7)
├── BOM.md             ← bill of materials / nomenclature (§26)
├── wiring/
│   └── WIRING.md      ← connection guide, pinout, power rails (§7 / §22)
├── schematics/
│   └── README.md      ← Phase 5 placeholder (§24)
└── pcb/
    └── README.md      ← Phase 5 placeholder (§24)
```

## Block diagram (§7)

```text
                         Wi-Fi
                           │
               MIDI + web configuration
                           │
                           ▼
                       ESP32-S3
                           │
        ┌───────────┬──────────┬─────────┬────────────┐
        │           │          │         │            │
   STEP/DIR/EN   BOWA/BOWB    I²C     Sensors      MOTOR_EN
        │           │          │         │            │
   1–4 TMC2209  1–4 H-bridge PCA9685  HOME/LIMIT  shared H-bridge
        │        drivers       │                    enable
   1–4 steppers 1–4 bow     1–16 servos
                DC motors
```

## Major blocks

### Main controller — ESP32-S3 (§7.1)

The reference controller is an **ESP32-S3-DevKitC-1**. It handles Wi-Fi MIDI
transport, hosts the web configurator, allocates notes, plans motion, runs the
per-string state machines, drives the PCA9685, monitors the sensors, stores
profiles, and enforces safety. Its GPIO matrix lets peripheral signals be routed
to many pins, which is what makes the configurable board profiles and pin
assignment possible (`board-profiles/esp32-s3-devkitc-1.json`).

### Stepper drivers — 1–4 × TMC2209 (§7.2)

One STEP/DIR-compatible driver per string; the reference is the **TMC2209**.
Each axis exposes STEP, DIR, ENABLE and HOME, with optional LIMIT, DIAG and UART.
The first prototype board must accept **pluggable driver modules** so a driver
can be swapped, different models tried, motor current tuned, and maintenance
done before an integrated PCB exists.

Per-axis signals:

```text
STEP        (fast output from ESP32-S3)
DIR         (output)
ENABLE      (shared global ENABLE line, GPIO42 by default)
HOME        (reference sensor input, interrupt-capable)
LIMIT       (optional opposite end-stop)
DIAG        (optional TMC2209 stall/diag)
UART        (optional TMC2209 configuration)
```

### Bow motors — 1–4 × H-bridge (§7.2)

One DC friction-wheel "bow" per string, each driven by an **H-bridge**
(DRV8871, TB6612FNG, L298N, DRV8833, MX1508…). The ESP32-S3 LEDC peripheral
generates the speed PWM. Two topologies are supported: `IN/IN` (two PWM inputs,
`BOWA`+`BOWB`) and `PH/EN` (`BOWA` = PWM speed, `BOWB` = direction level). All
the drivers' `nSLEEP`/`STBY` inputs tie to one **`MOTOR_EN`** line so the safety
layer can cut every wheel at once. The eight LEDC channels are shared with any
direct-GPIO servos (four `IN/IN` motors use all eight) — see
[`../docs/PIN_CONFIGURATION.md`](../docs/PIN_CONFIGURATION.md).

### Servo expander — PCA9685 (§7.3)

A single **PCA9685** provides up to 16 servo channels over I²C — the recommended
home for the servos on a bowed build, since the LEDC channels are taken by the
bow motors. Recommended channel map:

| Channels | Use |
| -------- | --- |
| 0–3 | finger press (one per string) |
| 4–7 | bow press / descent (one per string) |
| 8–15 | auxiliary functions |

The PCA9685 `/OE` (output-enable) pin must be tied to a **safety GPIO**
(`SERVO_OE`, GPIO47 by default) so all servos can be neutralised instantly on
panic or emergency stop (§21).

### Sensors — HOME / LIMIT (§7.2, §13)

Each axis has a HOME reference sensor (mechanical, optical or Hall). LIMIT
opposite end-stops are optional (0–6). HOME/LIMIT inputs must land on
interrupt-capable GPIO with an appropriate pull (internal or external); the
homing state machine normalises the active level via `sensorActiveHigh`.

## Power (summary, §22)

Four rails, servos on a **separate** supply from the ESP32 regulator:

| Rail | Feeds |
| ---- | ----- |
| 24 V | stepper motors (via the drivers) |
| 6–12 V | bow (friction-wheel) DC motors (via the H-bridges) |
| 5–7.4 V | servomotors |
| 5 V | logic |
| 3.3 V | ESP32-S3 |

Fusing, reverse-polarity protection, a TVS on the stepper and bow-motor rails,
driver decoupling and a PCA9685 bulk capacitor are required — see
`wiring/WIRING.md` §Power.

## Capacity (§6)

| Resource | Min | Max |
| -------- | :-: | :-: |
| Strings / steppers / fingers / HOME sensors | 1 | 4 |
| Opposite LIMIT switches | 0 | 4 |
| Finger servos | 1 | 4 |
| Bow-press (descent) servos | 1 | 4 |
| Bow motors (H-bridge) | 1 | 4 |
| Bow-motor PWM (LEDC) channels | 1 | 8 |
| Auxiliary servos | 0 | 4 |
| Total servo outputs (PCA9685) | 1 | 16 |

Invariant: **active strings = active stepper axes = movable fingers**.
