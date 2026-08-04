#include "TestFramework.h"
#include "../src/core/configuration/BowControl.h"
#include "../src/core/configuration/Profile.h"
#include "../src/core/configuration/ProfileValidator.h"
#include "../src/core/motion/StepperAxis.h"

using namespace gmb;

// A four-string bowed instrument (violin GDAE). Its bow motors already use all
// eight LEDC channels (4 x InIn), so there is no room for direct-GPIO servos.
static Profile violin() {
    return Profile::makeDefault("Violin", 4, {55, 62, 69, 76}, 12);
}
// A single-string bowed instrument leaves LEDC and pin headroom, so it is used to
// exercise the generic direct-GPIO / PCA servo validation paths.
static Profile mono() {
    return Profile::makeDefault("Mono", 1, {55}, 12);
}

// Default servos declare a source, a string and a role (finger or bowPress).
TEST(default_servos_have_source_and_string) {
    Profile p = violin();
    CHECK(!p.servos.empty());
    for (const auto& s : p.servos) {
        CHECK(s.source == ServoSource::Pca);
        CHECK(s.stringIndex >= 0);
        CHECK(s.function == "finger" || s.function == "bowPress");
    }
    CHECK(ProfileValidator::isActivatable(p));
}

// Every string ships a bow-press (descent) servo by default.
TEST(default_has_bow_press_per_string) {
    Profile p = violin();
    for (size_t i = 0; i < p.strings.size(); ++i) {
        bool found = false;
        for (const auto& s : p.servos)
            if (s.enabled && s.function == "bowPress" &&
                s.stringIndex == static_cast<int>(i))
                found = true;
        CHECK(found);
    }
}

// A direct-GPIO auxiliary servo on a free pin is valid (works without any PCA).
TEST(direct_gpio_servo_is_valid) {
    Profile p = mono();
    ServoConfig aux;
    aux.enabled = true;
    aux.function = "aux";
    aux.stringIndex = -1;
    aux.source = ServoSource::DirectGpio;
    aux.gpio = 2;  // recommended free pin on the single-string build
    p.servos.push_back(aux);
    CHECK(ProfileValidator::isActivatable(p));
}

// A direct servo on a reserved pin is rejected.
TEST(direct_servo_on_reserved_pin_rejected) {
    Profile p = mono();
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.stringIndex = -1;
    s.source = ServoSource::DirectGpio;
    s.gpio = 19;  // USB pin
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// A direct servo clashing with a stepper STEP pin is rejected.
TEST(direct_servo_conflicts_with_stepper_pin) {
    Profile p = mono();
    int8_t stepGpio = -1;
    for (const auto& a : p.pins)
        if (a.signal == "STEP1") stepGpio = a.gpio;
    CHECK(stepGpio >= 0);
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.stringIndex = -1;
    s.source = ServoSource::DirectGpio;
    s.gpio = stepGpio;
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// A direct servo clashing with a bow-motor PWM pin is rejected.
TEST(direct_servo_conflicts_with_bow_motor_pin) {
    Profile p = mono();
    int8_t bowGpio = -1;
    for (const auto& a : p.pins)
        if (a.signal == "BOWA1") bowGpio = a.gpio;
    CHECK(bowGpio >= 0);
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.stringIndex = -1;
    s.source = ServoSource::DirectGpio;
    s.gpio = bowGpio;
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// Two servos on the same PCA board+channel conflict.
TEST(duplicate_pca_channel_rejected) {
    Profile p = mono();
    ServoConfig s;
    s.enabled = true;
    s.function = "aux";
    s.stringIndex = -1;
    s.source = ServoSource::Pca;
    s.pcaBoard = 0;
    s.channel = 0;  // already used by finger of string 0
    p.servos.push_back(s);
    CHECK(!ProfileValidator::isActivatable(p));
}

// Up to four PCA boards addressable (0..3); board 4 is rejected.
TEST(pca_board_range) {
    Profile p = mono();
    ServoConfig ok;
    ok.enabled = true; ok.function = "aux"; ok.stringIndex = -1;
    ok.source = ServoSource::Pca; ok.pcaBoard = 3; ok.channel = 5;
    p.servos.push_back(ok);
    CHECK(ProfileValidator::isActivatable(p));

    p.servos.back().pcaBoard = 4;  // out of range
    CHECK(!ProfileValidator::isActivatable(p));
}

// An absurd absolute pulse width (outside the safe servo window) is rejected.
TEST(servo_pulse_absolute_range_rejected) {
    Profile p = violin();
    p.servos[0].pulseMaxUs = 4000;  // > 3000 µs absolute ceiling
    CHECK(!ProfileValidator::isActivatable(p));
}

// A profile with no enabled string can never arm and is rejected.
TEST(zero_enabled_strings_rejected) {
    Profile p = violin();
    for (auto& s : p.strings) s.enabled = false;
    CHECK(!ProfileValidator::isActivatable(p));
}

// When the selector is disabled its CC numbers are unused, so colliding
// string/fret CCs must NOT fail the profile.
TEST(disabled_selector_ignores_cc_collision) {
    Profile p = violin();
    p.selector.enabled = false;
    p.selector.string.ccNumber = 20;
    p.selector.fret.ccNumber = 20;  // identical, but selection is off
    CHECK(ProfileValidator::isActivatable(p));
}

// --- Bow actuator presence -----------------------------------------------

// Every enabled string needs a bow-press (descent) servo.
TEST(string_without_bow_press_rejected) {
    Profile p = violin();
    for (auto& s : p.servos)
        if (s.function == "bowPress" && s.stringIndex == 0) s.enabled = false;
    CHECK(!ProfileValidator::isActivatable(p));
}

// Every enabled string needs an enabled bow motor to be bowed.
TEST(string_without_bow_motor_rejected) {
    Profile p = violin();
    for (auto& m : p.bowMotors)
        if (m.stringIndex == 0) m.enabled = false;
    CHECK(!ProfileValidator::isActivatable(p));
}

// A missing BOWA pin for an enabled string is rejected.
TEST(missing_bow_motor_pin_rejected) {
    Profile p = violin();
    for (auto it = p.pins.begin(); it != p.pins.end(); ++it)
        if (it->signal == "BOWA1") { p.pins.erase(it); break; }
    CHECK(!ProfileValidator::isActivatable(p));
}

// --- Bow motor sanity ----------------------------------------------------

TEST(bow_motor_duty_out_of_order_rejected) {
    Profile p = violin();
    p.bowMotors[0].minDutyPercent = 80;
    p.bowMotors[0].maxDutyPercent = 40;  // min > max
    CHECK(!ProfileValidator::isActivatable(p));
}

TEST(bow_motor_pwm_clock_rejected) {
    Profile p = violin();
    p.bowMotors[0].pwmFreqHz = 40000;      // 40 kHz
    p.bowMotors[0].pwmResolutionBits = 12; // 40k * 4096 = 163 MHz > 80 MHz LEDC clock
    CHECK(!ProfileValidator::isActivatable(p));
}

// The LEDC budget (8 channels) is shared: four InIn motors already use all eight,
// so even one extra direct-GPIO servo overflows it.
TEST(ledc_budget_exceeded_rejected) {
    Profile p = violin();  // 4 x InIn = 8 PWM channels, 0 direct servos
    CHECK(ProfileValidator::isActivatable(p));  // exactly at the budget
    ServoConfig aux;
    aux.enabled = true; aux.function = "aux"; aux.stringIndex = -1;
    aux.source = ServoSource::DirectGpio; aux.gpio = 34;  // a caution output pin
    p.servos.push_back(aux);                              // -> 9 LEDC consumers
    bool ledc = false;
    for (const auto& is : ProfileValidator::validate(p))
        if (is.field == "ledc") ledc = true;
    CHECK(ledc);
}

// PhaseEnable motors free LEDC channels (one PWM each): four of them use only
// four channels, leaving room for direct-GPIO servos.
TEST(phase_enable_frees_ledc_channels) {
    Profile p = violin();
    for (auto& m : p.bowMotors) m.driveMode = BowDriveMode::PhaseEnable;
    // 4 motors x 1 PWM = 4 channels; add four direct servos -> 8, still within budget.
    CHECK(ProfileValidator::isActivatable(p));
}

// --- Bow dynamics maths (BowControl.h) -----------------------------------

static BowMotorConfig motorSpan() {
    BowMotorConfig m;
    m.minDutyPercent = 20;
    m.maxDutyPercent = 90;
    return m;
}

// Bow speed maps intensity into the motor's [min,max] duty window.
TEST(bow_speed_duty_follows_intensity) {
    BowMotorConfig m = motorSpan();
    CHECK_NEAR(bowSpeedDuty(m, 0.0), 0.20, 1e-9);   // floor
    CHECK_NEAR(bowSpeedDuty(m, 1.0), 0.90, 1e-9);   // ceiling
    CHECK_NEAR(bowSpeedDuty(m, 0.5), 0.55, 1e-9);   // midpoint
}

// Intensity is clamped so out-of-range values stay inside the window.
TEST(bow_speed_duty_clamps_intensity) {
    BowMotorConfig m = motorSpan();
    CHECK_NEAR(bowSpeedDuty(m, -1.0), 0.20, 1e-9);
    CHECK_NEAR(bowSpeedDuty(m, 2.0), 0.90, 1e-9);
}

static ServoConfig bowPressServo() {
    ServoConfig s;
    s.function = "bowPress";
    s.pulseMinUs = 500;
    s.pulseMaxUs = 2500;
    s.restUs = 1000;      // lifted
    s.contactUs = 1400;   // lightest contact
    s.activeUs = 1900;    // full pressure
    return s;
}

// Bow pressure maps intensity between contact and active while engaged.
TEST(bow_pressure_follows_intensity) {
    ServoConfig s = bowPressServo();
    CHECK_EQ((int)bowPressureTargetUs(s, 0.0), 1400);  // lightest contact
    CHECK_EQ((int)bowPressureTargetUs(s, 1.0), 1900);  // full pressure
    CHECK_EQ((int)bowPressureTargetUs(s, 0.5), 1650);  // midpoint
}

// The pressure target is clamped to the servo's mechanical pulse window.
TEST(bow_pressure_clamped_to_window) {
    ServoConfig s = bowPressServo();
    s.activeUs = 2600;  // beyond pulseMaxUs
    CHECK_EQ((int)bowPressureTargetUs(s, 1.0), 2500);  // clamped down to pulseMax
}

// --- Fret geometry (unchanged by the bow port) ---------------------------

// Adjustable per-fret positions: the calibrated table overrides theory and is
// what the web fret editor writes.
TEST(adjustable_fret_positions) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 3;
    // User nudges fret 1 to a measured value.
    cfg.calibratedFretMm = {0.0, 19.5, gmb::fretPositionMm(330.0, 2),
                            gmb::fretPositionMm(330.0, 3)};
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(1), 19.5, 1e-9);           // manual override
    CHECK_NEAR(axis.fretPositionMm(2), gmb::fretPositionMm(330.0, 2), 1e-9);
}

// The per-string fret offset (nut position from the FDC) shifts every position.
TEST(fret_offset_shifts_all_frets) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 3;
    cfg.fretOffsetMm = 25.0;
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(0), 25.0, 1e-9);                                 // nut at the offset
    CHECK_NEAR(axis.fretPositionMm(1), 25.0 + gmb::fretPositionMm(330.0, 1), 1e-9); // + spacing
}

// The offset applies on top of a nut-relative calibrated table too.
TEST(fret_offset_applies_to_calibrated) {
    AxisConfig cfg;
    cfg.scaleLengthMm = 330.0;
    cfg.maxFret = 2;
    cfg.fretOffsetMm = 10.0;
    cfg.calibratedFretMm = {0.0, 19.5, 37.0};  // nut-relative
    StepperAxis axis(cfg);
    CHECK_NEAR(axis.fretPositionMm(0), 10.0, 1e-9);
    CHECK_NEAR(axis.fretPositionMm(1), 10.0 + 19.5, 1e-9);
}

// The travel-fit validator folds in fretOffsetMm (absolute target).
TEST(fret_offset_beyond_travel_rejected) {
    Profile p = violin();  // scale 330, maxFret 12 -> lastFret 165; maxPositionMm 400
    p.strings[0].fretOffsetMm = 300.0;  // 300 + 165 = 465 > 400
    CHECK(!ProfileValidator::isActivatable(p));
}
TEST(fret_offset_within_travel_valid) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = 20.0;   // 20 + 165 = 185 < 400
    CHECK(ProfileValidator::isActivatable(p));
}
TEST(negative_fret_offset_before_travel_rejected) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = -10.0;  // fret 0 target below minPositionMm (0)
    CHECK(!ProfileValidator::isActivatable(p));
}
// A calibrated value is nut-relative, so the range check must add the offset.
TEST(calibrated_plus_offset_out_of_travel_rejected) {
    Profile p = violin();
    p.strings[0].fretOffsetMm = 350.0;
    p.strings[0].calibratedFretMm = {0.0, 60.0};  // absolute: 350, 410 > 400
    CHECK(!ProfileValidator::isActivatable(p));
}
