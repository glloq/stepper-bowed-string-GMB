#include "Profile.h"

namespace gmb {

InstrumentView Profile::instrumentView() const {
    InstrumentView v;
    v.stringCount = static_cast<uint8_t>(strings.size());
    v.capo = instrument.capo;
    v.transpose = instrument.transpose + midi.transpose;
    for (const auto& s : strings) {
        v.openNotes.push_back(s.openNote);
        v.maxFretPerString.push_back(s.maxFret);
    }
    return v;
}

Profile Profile::makeDefault(const std::string& name, uint8_t stringCount,
                             const std::vector<uint8_t>& tuning, uint8_t maxFret) {
    Profile p;
    p.instrument.name = name;
    p.instrument.stringCount = stringCount;

    for (uint8_t i = 0; i < stringCount; ++i) {
        AxisConfig axis;
        axis.openNote = i < tuning.size() ? tuning[i] : 40;
        axis.maxFret = maxFret;
        p.strings.push_back(axis);

        HomingConfig h;
        p.homing.push_back(h);

        // Finger servo (pitch stop) on PCA channels 0..3.
        ServoConfig finger;
        finger.enabled = true;
        finger.function = "finger";
        finger.stringIndex = static_cast<int8_t>(i);
        finger.source = ServoSource::Pca;
        finger.channel = i;                            // channels 0..3 : finger press
        p.servos.push_back(finger);

        // Bow-press (descent) servo on PCA channels 4..7. Mandatory on a bowed
        // build: it lowers the friction wheel onto the string and sets the bow
        // pressure (rest = lifted, contact = lightest touch, active = full weight).
        ServoConfig bow;
        bow.enabled = true;
        bow.function = "bowPress";
        bow.stringIndex = static_cast<int8_t>(i);
        bow.source = ServoSource::Pca;
        bow.channel = static_cast<uint8_t>(4 + i);     // channels 4..7 : bow press
        bow.restUs = 1000;                             // wheel fully lifted
        bow.contactUs = 1400;                          // lightest audible contact
        bow.activeUs = 1900;                           // maximum bow pressure
        bow.travelMs = 80;
        bow.settleMs = 20;
        p.servos.push_back(bow);

        // Friction-wheel bow motor (DC via H-bridge), one per string. Its GPIOs
        // are auto-assigned into the pin table below (BOWA{n}/BOWB{n}).
        BowMotorConfig motor;
        motor.enabled = true;
        motor.stringIndex = static_cast<int8_t>(i);
        motor.driveMode = BowDriveMode::InIn;
        p.bowMotors.push_back(motor);
    }

    // Default explicit selection follows the General-Midi-Boop preset.
    p.selector.string.maximum = stringCount;
    p.selector.fret.maximum = maxFret;

    // Automatic pin assignment for the reference board.
    if (const BoardProfile* board = builtinBoardProfile(p.boardIdentifier)) {
        PinManager pm(*board);
        PinRequest req;
        req.stringCount = stringCount;
        req.useBowMotors = true;
        pm.autoAssign(req);
        p.pins = pm.assignments();
    }
    return p;
}

}  // namespace gmb
