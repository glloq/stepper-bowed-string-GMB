#include "TestFramework.h"
#include "../src/core/instrument/StringController.h"

using namespace gmb;

static StringController armed() {
    StringController c;
    c.enable();
    return c;
}

// Full happy path to a sounding (bowed) note.
TEST(fsm_full_bow_sequence) {
    StringController c = armed();
    uint32_t id = c.noteOn(5);  // positioned note
    CHECK(id != 0);
    CHECK(c.state() == StringState::ReleasingFinger);
    c.motionReached();
    CHECK(c.state() == StringState::PressingFinger);
    c.fingerPressed();
    CHECK(c.state() == StringState::Settling);
    c.settled();
    CHECK(c.state() == StringState::ReadyToBow);
    CHECK(c.bowArmed());
    CHECK(c.startBow(id));
    CHECK(c.state() == StringState::Bowing);
}

// Open string skips the finger press (spec 15.3).
TEST(fsm_open_string_skips_finger) {
    StringController c = armed();
    uint32_t id = c.noteOn(0);  // open
    CHECK(c.openString());
    c.motionReached();
    CHECK(c.state() == StringState::ReadyToBow);
    CHECK(c.startBow(id));
    CHECK(c.state() == StringState::Bowing);
}

// A held note is stopped by lifting the wheel: Note Off -> ReleasingBow -> Idle.
TEST(fsm_note_off_while_bowing_releases) {
    StringController c = armed();
    uint32_t id = c.noteOn(0);
    c.motionReached();
    CHECK(c.startBow(id));
    CHECK(c.state() == StringState::Bowing);
    c.noteOff();
    CHECK(c.state() == StringState::ReleasingBow);  // wheel lifting + motor spin-down
    c.bowReleased();
    CHECK(c.state() == StringState::Idle);
}

// Acceptance criteria 12 & 13: Note Off cancels a preparing attack; no delayed
// bow-start runs after cancellation.
TEST(fsm_note_off_cancels_prepared_bow) {
    StringController c = armed();
    uint32_t id = c.noteOn(5);
    c.motionReached();
    c.fingerPressed();
    c.settled();
    CHECK(c.bowArmed());
    // Note Off arrives before the deferred bow-start executes.
    c.noteOff();
    CHECK(!c.bowArmed());
    // The old deferred bow-start must be ignored (stale command id).
    CHECK(!c.startBow(id));
    CHECK(c.state() != StringState::Bowing);
}

// A stale command id from a replaced note must never start the bow.
TEST(fsm_replaced_command_ignores_old_bow) {
    StringController c = armed();
    uint32_t oldId = c.noteOn(5);
    c.motionReached();
    c.fingerPressed();
    c.settled();
    uint32_t newId = c.noteOn(7);  // replace with a new note
    CHECK(newId != oldId);
    CHECK(!c.startBow(oldId));  // old id rejected
    CHECK(c.state() == StringState::ReleasingFinger);
}

// prepareNote reaches ReadyToBow without arming; trigger() then arms the bow so
// the deferred attack fires — anticipated pre-positioning.
TEST(fsm_prepare_holds_bow_until_trigger) {
    StringController c = armed();
    uint32_t id = c.prepareNote(5);
    CHECK(id != 0);
    c.motionReached();
    c.fingerPressed();
    c.settled();
    CHECK(c.state() == StringState::ReadyToBow);
    CHECK(!c.bowArmed());            // prepared, but held disarmed
    CHECK(!c.startBow(id));          // must not fire before the Note On
    CHECK(c.trigger(id));            // Note On arrives -> arm now (already settled)
    CHECK(c.bowArmed());
    CHECK(c.startBow(id));
    CHECK(c.state() == StringState::Bowing);
}

// trigger() before the string settles arms on-settle: the bow engages the moment
// the mechanical sequence completes.
TEST(fsm_trigger_before_settle_arms_on_settle) {
    StringController c = armed();
    uint32_t id = c.prepareNote(5);
    c.motionReached();               // still PressingFinger
    CHECK(c.trigger(id));            // triggered mid-preparation
    CHECK(!c.bowArmed());            // not ready yet
    c.fingerPressed();
    c.settled();
    CHECK(c.bowArmed());             // armed as soon as it settled
    CHECK(c.startBow(id));
}

// A stale trigger id (superseded prepare) is ignored.
TEST(fsm_trigger_rejects_stale_id) {
    StringController c = armed();
    uint32_t oldId = c.prepareNote(5);
    uint32_t newId = c.prepareNote(7);  // supersedes the first prepare
    CHECK(newId != oldId);
    CHECK(!c.trigger(oldId));
    CHECK(c.trigger(newId));
}

// Panic drops any armed attack (spec 21.3).
TEST(fsm_panic_cancels_everything) {
    StringController c = armed();
    uint32_t id = c.noteOn(5);
    c.motionReached();
    c.fingerPressed();
    c.settled();
    c.panic();
    CHECK(!c.startBow(id));
    CHECK(c.state() == StringState::Idle);
}
