// Bow-motor bank: one DC friction-wheel "bow" per string driven through an
// H-bridge, with the ESP32 LEDC peripheral generating the speed PWM. Supports two
// driver topologies (BowDriveMode): InIn (two PWM inputs) and PhaseEnable (one PWM
// speed input + one direction level). A shared MOTOR_EN line (nSLEEP/STBY) powers
// every stage on or off for the safety layer.
//
// The bank owns a per-motor speed slew (spinUp/spinDownMs) so the wheel ramps in
// and out smoothly, and it ramps through zero before reversing direction so the
// H-bridge is never slammed. The LEDC channels come from the same 8-channel pool
// as ServoBank's direct-GPIO servos (ProfileValidator enforces the budget).
#pragma once

#include <cstdint>
#include <vector>

#include "../../core/Types.h"
#include "../../core/configuration/Profile.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace gmb {

class BowMotorBank {
public:
    // motors[i] pairs with pinsA[i]/pinsB[i] (the resolved BOWA/BOWB GPIOs).
    // enablePin is the shared MOTOR_EN (nSLEEP/STBY), -1 if none. Motors are left
    // stopped and (if an enable pin is present) disabled after begin().
    void begin(const std::vector<BowMotorConfig>& motors,
               const std::vector<int8_t>& pinsA,
               const std::vector<int8_t>& pinsB,
               int8_t enablePin);

    // Command the bow speed of a motor: duty 0..1 (already mapped through the
    // motor's min/max window by bowSpeedDuty), forward selects the rotation
    // direction before the per-motor `reverse` inversion. duty <= 0 stops it.
    void setSpeed(int index, double duty, bool forward = true);
    void stop(int index);                 // ramp down, then coast or brake per config

    // Ramp the actual PWM toward each motor's target (spinUp/spinDownMs). Call
    // from loop().
    void update(uint32_t nowMs);

    // Shared H-bridge enable (nSLEEP/STBY): powers every motor stage on/off.
    void enableAll(bool on);
    void neutraliseAll();                 // stop all immediately + disable the line

    size_t count() const { return motors_.size(); }
    int indexForString(int stringIndex) const;
    bool commandable(int index) const {
        return index >= 0 && index < static_cast<int>(motors_.size()) &&
               motors_[index].enabled;
    }
    // The current (ramped) duty of a motor, 0..1 — for the dashboard / diagnostics.
    double dutyOf(int index) const {
        return (index >= 0 && index < static_cast<int>(rt_.size())) ? rt_[index].current : 0.0;
    }
    bool forwardOf(int index) const {
        return (index >= 0 && index < static_cast<int>(rt_.size())) ? rt_[index].forward : true;
    }
    // True if a motor pin could not attach an LEDC channel (out of channels, etc.).
    bool attachFault() const { return attachFault_; }

private:
    struct Rt {
        double current = 0.0;      // ramped duty actually applied (0..1)
        double target = 0.0;       // requested duty
        bool forward = true;       // applied direction (post-`reverse`)
        bool targetForward = true; // requested applied direction
        uint32_t lastMs = 0;
        bool started = false;
    };
    std::vector<BowMotorConfig> motors_;
    std::vector<int8_t> pinsA_, pinsB_;
    std::vector<Rt> rt_;
    int8_t enablePin_ = kNoPin;
    bool attachFault_ = false;

    uint32_t dutyCount(int index, double duty) const;  // duty(0..1) -> LEDC count
    void writeMotor(int index);           // apply rt_[index].current/forward
    void pwmWrite(int8_t pin, int ledcCh2x, uint32_t count) const;
};

}  // namespace gmb
