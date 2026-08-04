// Stepper-Bowed-Strings-GMB — core shared types and constants.
//
// This header is pure C++17 with no Arduino / ESP-IDF dependency so that the
// whole algorithmic core can be unit-tested natively on a host with g++.
#pragma once

#include <cstdint>

namespace gmb {

// Hard capacity limits derived from the spec (section 6).
// A bowed instrument uses one DC "bow" motor (friction wheel) per string driven
// through an H-bridge. Each motor needs 1 or 2 LEDC PWM channels and the
// ESP32-S3 exposes only 8, so the string count is capped at 4.
constexpr uint8_t kMaxStrings = 4;      // 1..4 strings / stepper axes / bow motors
constexpr uint8_t kMaxBowMotors = kMaxStrings; // one friction-wheel motor per string
constexpr uint8_t kMaxServoOutputs = 16; // PCA9685 channels
constexpr uint8_t kMaxAuxPower = 8;
constexpr uint8_t kMinProfiles = 8;

// A MIDI CC number is 7-bit. 120..127 are Channel Mode messages and must not be
// offered as string/fret selectors.
constexpr uint8_t kMaxAssignableCc = 119;

// Sentinel used across the code base for "no GPIO / not assigned".
constexpr int8_t kNoPin = -1;

// Convert a fret index to the theoretical position along the vibrating string.
// position = scaleLengthMm * (1 - 2^(-fret/12))    (spec 14.2)
double fretPositionMm(double scaleLengthMm, int fret);

// Equal-tempered MIDI note produced by an open string at a given fret.
// note = openNote + fret + capo + transpose
inline int frettedNote(int openNote, int fret, int capo = 0, int transpose = 0) {
    return openNote + fret + capo + transpose;
}

// Clamp helper (std::clamp needs <algorithm>; keep this header light).
template <typename T>
inline T clampValue(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace gmb
