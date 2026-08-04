// Pure bow-dynamics maths (unit-tested on the host).
//
// A bowed note has two dynamic dimensions, both driven by the note's intensity
// (0..1, derived from MIDI velocity and — while it sounds — the live expression
// controllers):
//
//   * bow SPEED    : how fast the friction wheel turns (H-bridge PWM duty).
//   * bow PRESSURE : how hard the wheel presses on the string (descent servo).
//
// Louder/brighter = faster wheel AND more pressure, so a single intensity maps to
// both. Kept out of the platform banks so the maths is unit-tested natively
// without the Arduino runtime. `inverted` on the servo is NOT applied here — the
// ServoBank mirrors within the pulse window at write time, exactly as before.
#pragma once

#include <cstdint>

#include "Profile.h"

namespace gmb {

// Bow-wheel PWM duty (0..1) for a given intensity. The intensity is mapped into
// the motor's [minDutyPercent, maxDutyPercent] window: the floor keeps the wheel
// turning past its dead-band even at pianissimo, the ceiling caps the top speed.
inline double bowSpeedDuty(const BowMotorConfig& m, double intensity) {
    if (intensity < 0.0) intensity = 0.0;
    if (intensity > 1.0) intensity = 1.0;
    double lo = static_cast<double>(m.minDutyPercent) / 100.0;
    double hi = static_cast<double>(m.maxDutyPercent) / 100.0;
    if (lo < 0.0) lo = 0.0;
    if (hi > 1.0) hi = 1.0;
    if (hi < lo) hi = lo;  // a misconfigured window collapses to the floor
    double duty = lo + intensity * (hi - lo);
    if (duty < 0.0) duty = 0.0;
    if (duty > 1.0) duty = 1.0;
    return duty;
}

// Descent-servo target pulse (µs) for a given bow pressure intensity. While the
// bow is engaged the pulse runs between contactUs (lightest audible contact,
// intensity 0) and activeUs (maximum pressure, intensity 1); the wheel is only
// fully lifted at restUs, which is commanded separately on note-off. The result
// is clamped to the servo's mechanical pulse window.
inline uint16_t bowPressureTargetUs(const ServoConfig& s, double intensity) {
    if (intensity < 0.0) intensity = 0.0;
    if (intensity > 1.0) intensity = 1.0;

    int lo = static_cast<int>(s.contactUs);
    int hi = static_cast<int>(s.activeUs);
    double target = static_cast<double>(lo) + intensity * static_cast<double>(hi - lo);

    // Clamp to the servo's mechanical pulse window.
    if (target < s.pulseMinUs) target = s.pulseMinUs;
    if (target > s.pulseMaxUs) target = s.pulseMaxUs;
    return static_cast<uint16_t>(target + 0.5);
}

}  // namespace gmb
