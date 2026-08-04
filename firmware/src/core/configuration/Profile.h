// Instrument configuration profile (spec section 20).
//
// This is the single source of truth for the firmware. The web UI edits a draft
// which is validated and then atomically activated; SysEx capabilities and the
// runtime are rebuilt from the active profile only.
//
// Bowed-string variant: each string is excited by a motorised friction wheel
// (the "bow"), driven through an H-bridge (BowMotorConfig), and pressed onto the
// string by a mandatory descent servo (ServoConfig function == "bowPress").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../board/PinManager.h"
#include "../instrument/NoteAllocator.h"
#include "../midi/StringFretSelector.h"
#include "../motion/HomingController.h"
#include "../motion/StepperAxis.h"

namespace gmb {

enum class NetworkMode : uint8_t { AccessPoint = 0, Station = 1 };

enum class VelocityCurve : uint8_t { Linear, Soft, Hard, Exponential, Custom };

struct InstrumentInfo {
    std::string name = "Instrument";
    std::string description;
    uint8_t stringCount = 4;
    std::string type = "violin";
    uint8_t gmProgram = 40;   // GM 40 = violin
    uint8_t typeId = 0x05;    // GMB instrument type id (bowed string family)
    uint8_t subType = 0;      // 0 violin / 1 viola / 2 cello / 3 contrabass
    bool fretless = true;     // bowed strings are fretless (positions are computed)
    int8_t capo = 0;
    int8_t transpose = 0;
};

struct NetworkConfig {
    NetworkMode mode = NetworkMode::AccessPoint;
    std::string ssid;            // station SSID (never exported with password)
    std::string hostname = "gmb-instrument";
    std::string apSsid = "Stepper-Bowed-Strings-GMB";
    bool staticIp = false;
};

struct MidiConfig {
    uint8_t globalChannel = 0;   // zero-based internal channel
    bool omni = false;
    int8_t transpose = 0;
    uint8_t chordWindowMs = 3;   // grouping window (spec 17.2)
    VelocityCurve velocityCurve = VelocityCurve::Linear;
    bool sustainPedal = true;
    uint8_t sustainCc = 64;
    SaturationStrategy saturationStrategy = SaturationStrategy::PriorityLow;

    // Continuous dynamics — the defining feature of a bowed instrument. Unlike a
    // plucked note (a single impulse), a bowed note is excited continuously, so
    // its loudness can be shaped WHILE it sounds. When enabled, CC7 (volume),
    // CC11 (expression), CC1 (modulation) and channel aftertouch modulate the
    // bow speed and pressure of every sounding string in real time. Disable it to
    // freeze each note's dynamics at its attack velocity.
    bool continuousDynamics = true;

    // Playback timing / latency management.
    //   noteExecutionDelayMs : fixed delay between receiving a Note On and the
    //                          note actually sounding, so the mechanics have a
    //                          predictable, constant window to get in position.
    //   fingerLeadMs         : begin the finger descent up to this long before
    //                          the carriage is estimated to reach the position,
    //                          so the finger arrives on the string around the
    //                          same time (overlaps descent with the approach).
    //   bowLeadMs            : begin lowering the bow-press (descent) servo up to
    //                          this long before the string is ready, so the
    //                          friction wheel is already in contact when the note
    //                          starts (shrinks the attack latency).
    // The two leads shrink the minimum achievable noteExecutionDelayMs; both
    // default to 0 (no anticipation — the safe, strictly-sequential behaviour).
    uint16_t noteExecutionDelayMs = 0;
    uint16_t fingerLeadMs = 0;
    uint16_t bowLeadMs = 0;
};

// Where a servo's PWM signal comes from. The system must work with OR without a
// PCA9685 (a servo can hang directly off a free ESP32 GPIO), and both can be
// mixed on the same instrument. On a bowed build the LEDC channels are shared
// with the bow motors, so driving the servos over the PCA9685 is preferred.
enum class ServoSource : uint8_t { Pca = 0, DirectGpio = 1 };

// Servo roles. Per-string roles carry a stringIndex; shared roles use -1.
//   finger   : presses/stops the string to select the pitch     (per string)
//   bowPress : lowers the friction wheel onto the string and     (per string)
//              sets the bow pressure (mandatory on a bowed build)
//   aux      : any auxiliary actuator                            (shared/global)
// (The plucked-instrument roles — pluck / strum / strumLift / damper — do not
//  exist on a bowed instrument: the wheel excites the string continuously and a
//  note ends by lifting the wheel, not by damping.)
// (Function is kept as a string so the web UI can offer new roles without a
// firmware change.)
struct ServoConfig {
    bool enabled = false;
    std::string function = "finger";
    int8_t stringIndex = -1;      // owning string, or -1 for a shared/global servo

    // Signal source.
    ServoSource source = ServoSource::Pca;
    uint8_t pcaBoard = 0;         // 0..3 : up to four PCA9685 (0x40..0x43)
    uint8_t channel = 0;          // PCA9685 channel 0..15 (source == Pca)
    int8_t gpio = -1;             // ESP32 GPIO           (source == DirectGpio)

    // Motion calibration (microseconds).
    uint16_t pulseMinUs = 500;
    uint16_t pulseMaxUs = 2500;
    uint16_t restUs = 1000;       // finger: raised   | bowPress: wheel fully lifted
    uint16_t activeUs = 1800;     // finger: pressed  | bowPress: maximum bow pressure
    // bowPress only: the pulse for the lightest audible contact (bow intensity 0
    // while engaged). Bow pressure is mapped between contactUs and activeUs by the
    // note's intensity, so even a pianissimo note keeps the wheel on the string.
    // Ignored for the finger role. contactUs should sit between restUs and activeUs.
    uint16_t contactUs = 1400;
    bool inverted = false;
    uint16_t travelMs = 120;
    uint16_t settleMs = 30;
    bool disableAtRest = true;

    //   engageDelayMs : bowPress only — extra pause after the wheel is down before
    //                   the bow motor spins up, so the wheel is settled on the
    //                   string when it starts to move (0 = none).
    uint16_t engageDelayMs = 0;
};

// H-bridge drive topology for a DC bow motor.
//   InIn        : two PWM-capable inputs (IN1/IN2). Forward = PWM on IN1, IN2 low;
//                 reverse = PWM on IN2, IN1 low. Covers DRV8871, DRV8833, TB6612
//                 (in/in), L298N, L9110, MX1508, … — the common hobby drivers.
//   PhaseEnable : one PWM speed input (EN) + one direction level (PH/DIR). Covers
//                 TB6612 (phase/enable style) and A4950/DRV8256 PH/EN parts.
enum class BowDriveMode : uint8_t { InIn = 0, PhaseEnable = 1 };

// One motorised friction wheel ("bow") per string. Its rotation speed is the bow
// speed (louder/brighter when faster); its direction is the bow direction. Speed
// is generated with the ESP32 LEDC peripheral (shared 8-channel budget with any
// direct-GPIO servos — see ProfileValidator and Types.h::kMaxStrings).
//
// Like the stepper axes, a motor's GPIOs live in the central pin table (signals
// BOWA{n}/BOWB{n} per string, plus one shared MOTOR_EN), NOT inline here, so the
// pin manager can auto-assign and conflict-check them. Their meaning depends on
// driveMode: InIn -> BOWA = IN1 (PWM forward), BOWB = IN2 (PWM reverse);
// PhaseEnable -> BOWA = EN (PWM speed), BOWB = PH (direction level).
struct BowMotorConfig {
    bool enabled = false;
    int8_t stringIndex = -1;              // owning string (0..kMaxStrings-1)
    BowDriveMode driveMode = BowDriveMode::InIn;
    uint32_t pwmFreqHz = 20000;           // 20 kHz — above hearing, quiet drive
    uint8_t pwmResolutionBits = 10;       // LEDC duty resolution (bits)
    // A friction wheel must keep turning to excite the string, so the intensity
    // maps into [minDutyPercent, maxDutyPercent]: the floor keeps it spinning past
    // its dead-band even at pianissimo.
    uint8_t minDutyPercent = 25;
    uint8_t maxDutyPercent = 100;         // bow-speed ceiling
    bool reverse = false;                 // invert the default rotation direction
    bool brakeOnStop = false;             // brake (both inputs high) vs coast on note-off
    uint16_t spinUpMs = 40;               // ramp to target speed when the bow engages
    uint16_t spinDownMs = 60;             // ramp back to zero when the bow releases
};

struct Profile {
    std::string project = "Stepper-Bowed-Strings-GMB";
    uint16_t profileVersion = 1;
    uint32_t capabilitiesRevision = 1;

    InstrumentInfo instrument;
    std::string boardIdentifier = "esp32-s3-devkitc-1";
    bool reserveUsb = true;
    bool automaticPinAssignment = true;
    std::vector<PinAssignment> pins;

    NetworkConfig network;
    MidiConfig midi;
    SelectorConfig selector;

    std::vector<AxisConfig> strings;
    std::vector<HomingConfig> homing;
    std::vector<ServoConfig> servos;
    std::vector<BowMotorConfig> bowMotors;   // one friction-wheel motor per string

    // Build an InstrumentView (used by the selector and capabilities) from the
    // string list.
    InstrumentView instrumentView() const;

    // Convenience: create a sensible default profile for a given instrument.
    static Profile makeDefault(const std::string& name, uint8_t stringCount,
                               const std::vector<uint8_t>& tuning, uint8_t maxFret);
};

}  // namespace gmb
