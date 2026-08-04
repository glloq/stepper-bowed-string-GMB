// Native tests for the BowMotorBank slew / direction state machine. The hardware
// writes are ARDUINO-guarded no-ops here, but the ramp and direction logic runs
// unchanged, so this validates the part that has no other coverage.
#include "TestFramework.h"
#include "../src/platform/esp32/BowMotorBank.h"

using namespace gmb;

static BowMotorBank makeBank(bool reverse = false) {
    BowMotorBank b;
    std::vector<BowMotorConfig> motors;
    BowMotorConfig m;
    m.enabled = true;
    m.stringIndex = 0;
    m.spinUpMs = 100;
    m.spinDownMs = 100;
    m.minDutyPercent = 0;
    m.maxDutyPercent = 100;
    m.reverse = reverse;
    motors.push_back(m);
    b.begin(motors, {10}, {11}, 33);
    return b;
}

// Ramps up toward the target over spinUpMs.
TEST(bowmotor_spins_up_linearly) {
    BowMotorBank b = makeBank();
    b.setSpeed(0, 1.0, true);
    b.update(0);                       // anchors lastMs, no ramp on dt=0
    CHECK(b.dutyOf(0) <= 0.001);
    b.update(50);                      // 50 of 100 ms
    CHECK_NEAR(b.dutyOf(0), 0.5, 0.02);
    b.update(100);                     // full
    CHECK_NEAR(b.dutyOf(0), 1.0, 0.001);
    CHECK(b.forwardOf(0) == true);
}

// stop() ramps down to zero over spinDownMs (does not slam to 0).
TEST(bowmotor_stop_ramps_down) {
    BowMotorBank b = makeBank();
    b.setSpeed(0, 1.0, true);
    for (uint32_t t = 0; t <= 100; t += 10) b.update(t);
    CHECK_NEAR(b.dutyOf(0), 1.0, 0.001);
    b.stop(0);
    b.update(150);                     // half-way down
    CHECK(b.dutyOf(0) < 0.6 && b.dutyOf(0) > 0.4);
    b.update(200);                     // stopped
    CHECK_NEAR(b.dutyOf(0), 0.0, 0.001);
}

// A direction change ramps through zero BEFORE the applied direction flips, so
// the H-bridge is never slammed into reverse at speed.
TEST(bowmotor_reverses_through_zero) {
    BowMotorBank b = makeBank();
    b.setSpeed(0, 1.0, true);
    for (uint32_t t = 0; t <= 100; t += 10) b.update(t);
    CHECK(b.forwardOf(0) == true);
    CHECK_NEAR(b.dutyOf(0), 1.0, 0.001);

    b.setSpeed(0, 1.0, false);         // ask for reverse at full speed
    b.update(110);
    CHECK(b.dutyOf(0) < 1.0);          // ramping down first
    CHECK(b.forwardOf(0) == true);     // NOT flipped while still turning
    // Let it reach zero and then spin back up in the new direction.
    for (uint32_t t = 120; t <= 350; t += 10) b.update(t);
    CHECK(b.forwardOf(0) == false);    // now reversed
    CHECK_NEAR(b.dutyOf(0), 1.0, 0.05);
}

// The per-motor `reverse` flag inverts the requested direction once, up front.
TEST(bowmotor_reverse_flag_inverts) {
    BowMotorBank b = makeBank(/*reverse=*/true);
    b.setSpeed(0, 0.5, true);          // forward requested, but reverse flag set
    for (uint32_t t = 0; t <= 120; t += 10) b.update(t);
    CHECK(b.forwardOf(0) == false);    // applied direction is reversed
}

// The duty is mapped into the motor's [min,max] window.
TEST(bowmotor_duty_window) {
    BowMotorBank b;
    std::vector<BowMotorConfig> motors;
    BowMotorConfig m;
    m.enabled = true; m.stringIndex = 0;
    m.spinUpMs = 0; m.spinDownMs = 0;  // instantaneous, so the value is exact
    motors.push_back(m);
    b.begin(motors, {10}, {11}, -1);
    // Callers pass bowSpeedDuty()'s result (already within the window); the bank
    // applies it verbatim, so 0.5 stays 0.5.
    b.setSpeed(0, 0.5, true);
    b.update(0);
    b.update(10);
    CHECK_NEAR(b.dutyOf(0), 0.5, 0.001);
}

// neutraliseAll() cuts every wheel immediately (no ramp).
TEST(bowmotor_neutralise_is_immediate) {
    BowMotorBank b = makeBank();
    b.setSpeed(0, 1.0, true);
    for (uint32_t t = 0; t <= 100; t += 10) b.update(t);
    CHECK_NEAR(b.dutyOf(0), 1.0, 0.001);
    b.neutraliseAll();
    CHECK_NEAR(b.dutyOf(0), 0.0, 0.001);  // immediate, not ramped
}

// indexForString maps an owning string to its motor slot.
TEST(bowmotor_index_for_string) {
    BowMotorBank b = makeBank();
    CHECK_EQ(b.indexForString(0), 0);
    CHECK_EQ(b.indexForString(1), -1);   // no motor for string 1
    CHECK_EQ((int)b.count(), 1);
}

// A disabled motor is never commandable and never ramps.
TEST(bowmotor_disabled_motor_inert) {
    BowMotorBank b;
    std::vector<BowMotorConfig> motors;
    BowMotorConfig m; m.enabled = false; m.stringIndex = 0;
    motors.push_back(m);
    b.begin(motors, {10}, {11}, -1);
    CHECK(!b.commandable(0));
    b.setSpeed(0, 1.0, true);
    b.update(0); b.update(100);
    CHECK_NEAR(b.dutyOf(0), 0.0, 0.001);  // never moved
}
