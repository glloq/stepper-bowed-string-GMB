#include "TestFramework.h"
#include "../src/core/configuration/Profile.h"
#include "../src/core/configuration/ProfileValidator.h"

using namespace gmb;

// Cello tuning C-G-D-A (four strings, within the bowed 4-string cap).
static Profile celloProfile() {
    return Profile::makeDefault("Cello", 4, {36, 43, 50, 57}, 12);
}

// Acceptance criterion 15: profiles can be built and are valid by default.
TEST(default_profile_is_activatable) {
    Profile p = celloProfile();
    auto issues = ProfileValidator::validate(p);
    for (auto& i : issues)
        if (i.severity == ValidationIssue::Severity::Error)
            std::printf("  unexpected error: %s: %s\n", i.field.c_str(),
                        i.message.c_str());
    CHECK(ProfileValidator::isActivatable(p));
}

TEST(default_profile_auto_assigns_pins) {
    Profile p = celloProfile();
    CHECK(!p.pins.empty());
    // Auto-assignment must be conflict-free.
    const BoardProfile* b = builtinBoardProfile(p.boardIdentifier);
    CHECK(b != nullptr);
    PinManager pm(*b);
    pm.set(p.pins);
    CHECK(pm.validate(true).empty());
}

// The default 4-string build auto-assigns a bow motor pin pair per string.
TEST(default_profile_auto_assigns_bow_pins) {
    Profile p = celloProfile();
    auto has = [&](const std::string& sig) {
        for (const auto& a : p.pins)
            if (a.signal == sig && a.gpio >= 0) return true;
        return false;
    };
    for (int i = 1; i <= 4; ++i) {
        CHECK(has("BOWA" + std::to_string(i)));
        CHECK(has("BOWB" + std::to_string(i)));
    }
    CHECK(has("MOTOR_EN"));
}

TEST(string_count_mismatch_is_error) {
    Profile p = celloProfile();
    p.instrument.stringCount = 3;  // but 4 axes configured
    CHECK(!ProfileValidator::isActivatable(p));
}

// More than four strings is out of range for a bowed build.
TEST(too_many_strings_is_error) {
    Profile p = celloProfile();
    p.instrument.stringCount = 5;
    CHECK(!ProfileValidator::isActivatable(p));
}

TEST(same_cc_for_string_and_fret_is_error) {
    Profile p = celloProfile();
    p.selector.fret.ccNumber = p.selector.string.ccNumber;  // 20 == 20
    CHECK(!ProfileValidator::isActivatable(p));
}

TEST(channel_mode_cc_is_rejected) {
    Profile p = celloProfile();
    p.selector.string.ccNumber = 120;  // channel-mode message
    CHECK(!ProfileValidator::isActivatable(p));
}

TEST(zero_timeout_is_error) {
    Profile p = celloProfile();
    p.selector.selectionTimeoutMs = 0;
    CHECK(!ProfileValidator::isActivatable(p));
}

TEST(instrument_view_from_profile) {
    Profile p = celloProfile();
    InstrumentView v = p.instrumentView();
    CHECK_EQ((int)v.stringCount, 4);
    CHECK_EQ((int)v.openNotes[0], 36);
    CHECK_EQ((int)v.openNotes[3], 57);
}
