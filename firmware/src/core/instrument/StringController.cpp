#include "StringController.h"

namespace gmb {

uint32_t StringController::noteOn(int fret) {
    if (state_ == StringState::Disabled || state_ == StringState::Fault) {
        return 0;
    }
    commandId_ = nextId_++;
    targetFret_ = fret;
    openString_ = (fret == 0);
    bowArmed_ = false;
    armOnSettle_ = true;  // direct play: arm the bow as soon as the string is ready
    // The finger always lifts before moving (also the resting state for an open
    // string).
    state_ = StringState::ReleasingFinger;
    return commandId_;
}

uint32_t StringController::prepareNote(int fret) {
    if (state_ == StringState::Disabled || state_ == StringState::Fault) {
        return 0;
    }
    commandId_ = nextId_++;
    targetFret_ = fret;
    openString_ = (fret == 0);
    bowArmed_ = false;
    armOnSettle_ = false;  // anticipation: move + press, but hold the bow disarmed
    state_ = StringState::ReleasingFinger;
    return commandId_;
}

bool StringController::trigger(uint32_t id) {
    // Only the matching (still-current) prepared command may be triggered.
    if (id != commandId_) return false;
    if (state_ == StringState::Disabled || state_ == StringState::Fault) return false;
    armOnSettle_ = true;
    triggerEdge_ = true;  // Note-On instant, for the scheduler's fixed-delay anchor
    // Already settled while prepared: arm the deferred bow right away.
    if (state_ == StringState::ReadyToBow) bowArmed_ = true;
    return true;
}

void StringController::motionReached() {
    // Finger is released -> move -> arrive.
    if (state_ == StringState::ReleasingFinger) {
        state_ = StringState::Moving;
    }
    if (state_ == StringState::Moving) {
        if (openString_) {
            // Open string: no finger press, ready immediately (spec 15.3).
            state_ = StringState::ReadyToBow;
            bowArmed_ = armOnSettle_;  // stay disarmed if merely prepared
        } else {
            state_ = StringState::PressingFinger;
        }
    }
}

void StringController::fingerPressed() {
    if (state_ == StringState::PressingFinger) {
        state_ = StringState::Settling;
    }
}

void StringController::settled() {
    if (state_ == StringState::Settling) {
        state_ = StringState::ReadyToBow;
        // Deferred bow armed (tagged by commandId_) — unless this is a merely
        // prepared note, which stays disarmed until trigger().
        bowArmed_ = armOnSettle_;
    }
}

bool StringController::startBow(uint32_t id) {
    // Guard: stale / cancelled commands are ignored (spec section 16).
    if (id != commandId_) return false;
    if (state_ != StringState::ReadyToBow || !bowArmed_) return false;
    bowArmed_ = false;
    // Engage the wheel and start turning it: the note now sounds continuously
    // until Note Off lifts the wheel.
    state_ = StringState::Bowing;
    return true;
}

void StringController::noteOff() {
    if (state_ == StringState::Disabled || state_ == StringState::Fault) return;
    // Any attack still in preparation is cancelled; a sounding note lifts the
    // wheel and spins the motor down.
    bool wasSounding = state_ == StringState::Bowing;
    invalidate();  // kills any armed bow (spec: no delayed bow-start after Note Off)
    state_ = wasSounding ? StringState::ReleasingBow : StringState::Idle;
}

void StringController::bowReleased() {
    if (state_ == StringState::ReleasingBow) state_ = StringState::Idle;
}

void StringController::panic() {
    // A disabled or faulted axis stays out of service — a panic (CC120/123,
    // Wi-Fi loss, software panic) must never resurrect it to Idle.
    if (state_ == StringState::Disabled || state_ == StringState::Fault) {
        invalidate();
        return;
    }
    invalidate();
    state_ = StringState::Idle;
}

}  // namespace gmb
