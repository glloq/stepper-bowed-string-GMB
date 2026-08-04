// Per-string state machine (spec section 16).
//
// Each string runs an independent, non-blocking state machine. Every command
// carries an id; when a command is cancelled or replaced, any deferred action
// tagged with the old id is ignored. This prevents a bow-start after a Note Off,
// a late finger press, execution of a stale position, or an attack after a panic.
//
// Bowing is a CONTINUOUS excitation, not an impulse: a note starts when the
// friction wheel engages (Bowing) and sounds until the wheel lifts on Note Off
// (ReleasingBow). There is no separate "damp" — lifting the wheel stops the note.
#pragma once

#include <cstdint>

namespace gmb {

enum class StringState : uint8_t {
    Disabled,
    Homing,
    Idle,
    ReleasingFinger,
    Moving,
    PressingFinger,
    Settling,
    ReadyToBow,
    Bowing,        // sounding — the friction wheel is engaged and turning
    ReleasingBow,  // lifting the wheel + spinning the motor down after Note Off
    Cancelling,
    Fault,
};

class StringController {
public:
    StringState state() const { return state_; }
    uint32_t commandId() const { return commandId_; }
    int targetFret() const { return targetFret_; }
    bool openString() const { return openString_; }

    void enable() { if (state_ == StringState::Disabled) state_ = StringState::Idle; }
    void disable() { state_ = StringState::Disabled; invalidate(); }
    // A disabled or faulted axis must never be dragged into homing.
    void setHoming() {
        if (state_ != StringState::Disabled && state_ != StringState::Fault)
            state_ = StringState::Homing;
    }
    void homingDone() { if (state_ == StringState::Homing) state_ = StringState::Idle; }
    void fault() { state_ = StringState::Fault; invalidate(); }
    // Clear a runtime fault so the axis can be re-homed on an explicit reset. A
    // Disabled axis stays disabled (audit P0-3).
    void clearFault() { if (state_ == StringState::Fault) { state_ = StringState::Idle; invalidate(); } }

    // Begin a new note. Returns the fresh command id. Cancels any prior pending
    // action by advancing the command id. The bow is armed automatically once the
    // string is ready.
    uint32_t noteOn(int fret);

    // Anticipated preparation (prepareOnCompleteSelection): move + press exactly
    // like noteOn, but DO NOT arm the bow when ready — it waits for trigger().
    uint32_t prepareNote(int fret);

    // Arm the (previously prepared) bow. Runs only for the matching command; if
    // the string is not yet ready it arms as soon as it settles.
    bool trigger(uint32_t id);

    // Progress hooks driven by motion/servo/timers (non-blocking).
    void motionReached();
    void fingerPressed();
    void settled();

    // Engage the bow: lower the wheel and start turning it. Runs only when `id`
    // still matches the current command and the string is ready — otherwise it is
    // silently ignored. Enters the continuous Bowing (sounding) state.
    bool startBow(uint32_t id);

    // Whether a deferred bow-start is currently armed (for the scheduler).
    bool bowArmed() const { return bowArmed_; }
    uint32_t bowCommandId() const { return commandId_; }

    // True for a normal note that arms its bow as soon as it is ready; false while
    // a note is only PREPARED (anticipated) and still waiting for trigger(). Lets
    // the scheduler skip bow-descent anticipation for a merely-prepared note and
    // re-anchor the fixed execution delay to the trigger instant.
    bool willArmOnSettle() const { return armOnSettle_; }

    // One-shot edge: true exactly once after trigger() fires for a prepared note,
    // so the scheduler can anchor the fixed execution delay to the Note-On instant
    // even when the note is triggered before it is mechanically ready.
    bool consumeTriggerEdge() { bool e = triggerEdge_; triggerEdge_ = false; return e; }

    // Release the note. Cancels any armed/prepared attack; a sounding note lifts
    // the wheel (ReleasingBow) so the string stops.
    void noteOff();

    // Wheel lift + spin-down finished -> back to idle.
    void bowReleased();

    // Emergency stop (spec 21.3).
    void panic();

private:
    StringState state_ = StringState::Disabled;
    uint32_t commandId_ = 0;
    uint32_t nextId_ = 1;
    int targetFret_ = 0;
    bool openString_ = true;
    bool bowArmed_ = false;
    bool armOnSettle_ = true;  // false while a note is only prepared (not triggered)
    bool triggerEdge_ = false; // set by trigger(), consumed once by the scheduler

    void invalidate() {
        commandId_ = nextId_++;  // any deferred action tagged with the old id dies
        bowArmed_ = false;
        armOnSettle_ = true;     // a fresh command arms normally by default
    }
};

}  // namespace gmb
