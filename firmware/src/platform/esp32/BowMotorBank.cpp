#include "BowMotorBank.h"

namespace gmb {

void BowMotorBank::begin(const std::vector<BowMotorConfig>& motors,
                         const std::vector<int8_t>& pinsA,
                         const std::vector<int8_t>& pinsB,
                         int8_t enablePin) {
    motors_ = motors;
    pinsA_ = pinsA;
    pinsB_ = pinsB;
    pinsA_.resize(motors_.size(), kNoPin);
    pinsB_.resize(motors_.size(), kNoPin);
    rt_.assign(motors_.size(), Rt{});
    enablePin_ = enablePin;
    attachFault_ = false;

#if defined(ARDUINO)
    if (enablePin_ >= 0) {
        pinMode(enablePin_, OUTPUT);
        digitalWrite(enablePin_, LOW);  // motors disabled at boot (safe)
    }
    for (size_t i = 0; i < motors_.size(); ++i) {
        const BowMotorConfig& m = motors_[i];
        if (!m.enabled) continue;
        const int8_t a = pinsA_[i];
        const int8_t b = pinsB_[i];
        // pinA is always a PWM speed input.
        if (a >= 0) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
            if (!ledcAttach(a, m.pwmFreqHz, m.pwmResolutionBits)) attachFault_ = true;
#else
            ledcSetup(2 * i, m.pwmFreqHz, m.pwmResolutionBits);
            ledcAttachPin(a, 2 * i);
#endif
        } else {
            attachFault_ = true;
        }
        // pinB: a second PWM input in InIn mode, a plain direction level otherwise.
        if (b >= 0) {
            if (m.driveMode == BowDriveMode::InIn) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
                if (!ledcAttach(b, m.pwmFreqHz, m.pwmResolutionBits)) attachFault_ = true;
#else
                ledcSetup(2 * i + 1, m.pwmFreqHz, m.pwmResolutionBits);
                ledcAttachPin(b, 2 * i + 1);
#endif
            } else {
                pinMode(b, OUTPUT);
                digitalWrite(b, LOW);
            }
        } else {
            attachFault_ = true;
        }
    }
#else
    (void)enablePin;
#endif
    for (size_t i = 0; i < motors_.size(); ++i) stop(static_cast<int>(i));
}

void BowMotorBank::setSpeed(int index, double duty, bool forward) {
    if (index < 0 || index >= static_cast<int>(motors_.size())) return;
    if (!motors_[index].enabled) return;
    if (duty < 0.0) duty = 0.0;
    if (duty > 1.0) duty = 1.0;
    Rt& r = rt_[index];
    r.target = duty;
    // Apply the per-motor direction inversion once, here, so writeMotor works in
    // "applied" direction terms only.
    r.targetForward = forward ^ motors_[index].reverse;
    r.started = true;
}

void BowMotorBank::stop(int index) {
    if (index < 0 || index >= static_cast<int>(motors_.size())) return;
    rt_[index].target = 0.0;  // ramp down (spinDownMs); direction kept until stopped
    rt_[index].started = true;
}

void BowMotorBank::update(uint32_t nowMs) {
    for (size_t i = 0; i < motors_.size(); ++i) {
        if (!motors_[i].enabled) continue;
        Rt& r = rt_[i];
        if (!r.started) { r.lastMs = nowMs; continue; }
        const uint32_t dt = nowMs - r.lastMs;
        r.lastMs = nowMs;
        const BowMotorConfig& m = motors_[i];

        // Ramp to zero before reversing so the H-bridge is never slammed.
        double tgt = r.target;
        if (r.targetForward != r.forward && r.current > 0.001) tgt = 0.0;

        if (dt != 0) {
            const uint16_t rampMs = (tgt >= r.current) ? m.spinUpMs : m.spinDownMs;
            if (rampMs == 0) {
                r.current = tgt;
            } else {
                const double step = static_cast<double>(dt) / static_cast<double>(rampMs);
                if (r.current < tgt) { r.current += step; if (r.current > tgt) r.current = tgt; }
                else { r.current -= step; if (r.current < tgt) r.current = tgt; }
            }
            // Adopt the new direction once the wheel has fully stopped.
            if (r.current <= 0.001 && r.targetForward != r.forward)
                r.forward = r.targetForward;
        }
        writeMotor(static_cast<int>(i));
    }
}

uint32_t BowMotorBank::dutyCount(int index, double duty) const {
    if (duty < 0.0) duty = 0.0;
    if (duty > 1.0) duty = 1.0;
    uint8_t res = motors_[index].pwmResolutionBits;
    if (res < 1) res = 1;
    if (res > 14) res = 14;
    const uint32_t maxC = (1u << res) - 1u;
    return static_cast<uint32_t>(duty * static_cast<double>(maxC) + 0.5);
}

void BowMotorBank::pwmWrite(int8_t pin, int ledcCh2x, uint32_t count) const {
#if defined(ARDUINO)
    if (pin < 0) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    (void)ledcCh2x;
    ledcWrite(pin, count);
#else
    (void)pin;
    ledcWrite(ledcCh2x, count);
#endif
#else
    (void)pin;
    (void)ledcCh2x;
    (void)count;
#endif
}

void BowMotorBank::writeMotor(int index) {
    Rt& r = rt_[index];
    const BowMotorConfig& m = motors_[index];
    const int8_t a = pinsA_[index];
    const int8_t b = pinsB_[index];
    const bool stopped = r.current <= 0.0005;
    const uint32_t count = dutyCount(index, r.current);

    if (m.driveMode == BowDriveMode::InIn) {
        if (stopped) {
            // Coast (both inputs low) or brake (both high) per config.
            uint8_t res = m.pwmResolutionBits;
            if (res < 1) res = 1; if (res > 14) res = 14;
            const uint32_t hold = m.brakeOnStop ? ((1u << res) - 1u) : 0u;
            pwmWrite(a, 2 * index, hold);
            pwmWrite(b, 2 * index + 1, hold);
        } else if (r.forward) {
            pwmWrite(a, 2 * index, count);
            pwmWrite(b, 2 * index + 1, 0);
        } else {
            pwmWrite(a, 2 * index, 0);
            pwmWrite(b, 2 * index + 1, count);
        }
    } else {  // PhaseEnable: direction level on pinB, speed PWM on pinA.
#if defined(ARDUINO)
        if (b >= 0) digitalWrite(b, r.forward ? HIGH : LOW);
#endif
        pwmWrite(a, 2 * index, stopped ? 0u : count);
    }
}

void BowMotorBank::enableAll(bool on) {
#if defined(ARDUINO)
    if (enablePin_ >= 0) digitalWrite(enablePin_, on ? HIGH : LOW);
#else
    (void)on;
#endif
}

void BowMotorBank::neutraliseAll() {
    for (size_t i = 0; i < motors_.size(); ++i) {
        Rt& r = rt_[i];
        r.current = 0.0;
        r.target = 0.0;
        r.started = false;
#if defined(ARDUINO)
        const int8_t a = pinsA_[i];
        const int8_t b = pinsB_[i];
        pwmWrite(a, 2 * static_cast<int>(i), 0);  // immediate coast
        if (motors_[i].driveMode == BowDriveMode::InIn)
            pwmWrite(b, 2 * static_cast<int>(i) + 1, 0);
        else if (b >= 0)
            digitalWrite(b, LOW);
#endif
    }
    enableAll(false);
}

int BowMotorBank::indexForString(int stringIndex) const {
    for (size_t i = 0; i < motors_.size(); ++i)
        if (motors_[i].enabled && motors_[i].stringIndex == stringIndex)
            return static_cast<int>(i);
    return -1;
}

}  // namespace gmb
