// Pioneer-DDJ-FLX4-script.js
// ****************************************************************************
// * Mixxx mapping script file for the Pioneer DDJ-FLX4.
// * Mostly adapted from the DDJ-400 mapping script
// * Authors: Warker, nschloe, dj3730, jusko, Robert904
// ****************************************************************************
//
//  Implemented (as per manufacturer's manual):
//      * Mixer Section (Faders, EQ, Filter, Gain, Cue)
//      * Browsing and loading + Waveform zoom (shift)
//      * Jogwheels, Scratching, Bending, Loop adjust
//      * Cycle Temporange
//      * Beat Sync
//      * Hot Cue Mode
//      * Beat Loop Mode
//      * Beat Jump Mode
//      * Sampler Mode
//      * Keyshift mode
//
//  Custom (Mixxx specific mappings):
//      * BeatFX (per-deck model): FX unit 1 edits deck A, FX unit 2 edits
//                deck B (fixed routing, seeded at startup). CH SELECT chooses
//                which unit the BEAT FX section addresses:
//                  CH1    -> unit 1 (deck A)
//                  CH2    -> unit 2 (deck B)
//                  MASTER -> both units together
//                v FX_SELECT Load next effect on the addressed unit(s), slot 0.
//                SHIFT + v FX_SELECT Load previous effect.
//                < LEFT Cycle effect focus leftward
//                > RIGHT Cycle effect focus rightward
//                LEVEL/DEPTH raises intensity (super) AND dry/wet (mix)
//                  together on the addressed unit(s); SHIFT trims mix only.
//                ON/OFF toggles the addressed unit(s) effect slot.
//                SHIFT + ON/OFF disables all slots on the addressed unit(s).
//
//      * 32 beat jump forward & back (Shift + </> CUE/LOOP CALL arrows)
//      * Toggle quantize (Shift + channel cue)
//      * Stems selection using PADs (using controller's Keyboard mode)
//
//  Not implemented (after discussion and trial attempts):
//      * Loop Section:
//        * -4BEAT auto loop (hacky---prefer a clean way to set a 4 beat loop
//                            from a previous position on long press)
//
//        * CUE/LOOP CALL - memory & delete (complex and not useful. Hot cues are sufficient)
//
//      * Secondary pad modes (trial attempts complex and too experimental)
//        * Keyboard mode
//        * Pad FX1
//        * Pad FX2
//
//  Not implemented yet (but might be in the future):
//      * Smart CFX
//      * Smart fader

var PioneerDDJFLX4Crate = {};

PioneerDDJFLX4Crate.lights = {
    beatFx: {
        status: 0x94,
        data1: 0x47,
    },
    shiftBeatFx: {
        status: 0x94,
        data1: 0x43,
    },
    deck1: {
        vuMeter: {
            status: 0xB0,
            data1: 0x02,
        },
        playPause: {
            status: 0x90,
            data1: 0x0B,
        },
        shiftPlayPause: {
            status: 0x90,
            data1: 0x47,
        },
        cue: {
            status: 0x90,
            data1: 0x0C,
        },
        shiftCue: {
            status: 0x90,
            data1: 0x48,
        },
        hotcueMode: {
            status: 0x90,
            data1: 0x1B,
        },
        keyboardMode: {
            status: 0x90,
            data1: 0x69,
        },
        padFX1Mode: {
            status: 0x90,
            data1: 0x1E,
        },
        padFX2Mode: {
            status: 0x90,
            data1: 0x6B,
        },
        beatJumpMode: {
            status: 0x90,
            data1: 0x20,
        },
        beatLoopMode: {
            status: 0x90,
            data1: 0x6D,
        },
        samplerMode: {
            status: 0x90,
            data1: 0x22,
        },
        keyShiftMode: {
            status: 0x90,
            data1: 0x6F,
        },
    },
    deck2: {
        vuMeter: {
            status: 0xB0,
            data1: 0x02,
        },
        playPause: {
            status: 0x91,
            data1: 0x0B,
        },
        shiftPlayPause: {
            status: 0x91,
            data1: 0x47,
        },
        cue: {
            status: 0x91,
            data1: 0x0C,
        },
        shiftCue: {
            status: 0x91,
            data1: 0x48,
        },
        hotcueMode: {
            status: 0x91,
            data1: 0x1B,
        },
        keyboardMode: {
            status: 0x91,
            data1: 0x69,
        },
        padFX1Mode: {
            status: 0x91,
            data1: 0x1E,
        },
        padFX2Mode: {
            status: 0x91,
            data1: 0x6B,
        },
        beatJumpMode: {
            status: 0x91,
            data1: 0x20,
        },
        beatLoopMode: {
            status: 0x91,
            data1: 0x6D,
        },
        samplerMode: {
            status: 0x91,
            data1: 0x22,
        },
        keyShiftMode: {
            status: 0x91,
            data1: 0x6F,
        },
    },
};

// Store timer IDs
PioneerDDJFLX4Crate.timers = {};

// Keep alive timer
PioneerDDJFLX4Crate.sendKeepAlive = function() {
    midi.sendSysexMsg([0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xf7], 12); // This was reverse engineered with Wireshark
};

// Jog wheel constants
PioneerDDJFLX4Crate.vinylMode = true;
PioneerDDJFLX4Crate.alpha = 1.0/8;
PioneerDDJFLX4Crate.beta = PioneerDDJFLX4Crate.alpha/32;

// Multiplier for fast seek through track using SHIFT+JOGWHEEL
PioneerDDJFLX4Crate.fastSeekScale = 150;
PioneerDDJFLX4Crate.bendScale = 0.8;

PioneerDDJFLX4Crate.tempoRanges = [0.06, 0.10, 0.16, 0.25];

PioneerDDJFLX4Crate.shiftButtonDown = [false, false];

// Jog wheel loop adjust
PioneerDDJFLX4Crate.loopAdjustIn = [false, false];
PioneerDDJFLX4Crate.loopAdjustOut = [false, false];
PioneerDDJFLX4Crate.loopAdjustMultiply = 50;

// Beatjump pad (beatjump_size values)
PioneerDDJFLX4Crate.beatjumpSizeForPad = {
    0x20: -1, // PAD 1
    0x21: 1,  // PAD 2
    0x22: -2, // PAD 3
    0x23: 2,  // PAD 4
    0x24: -4, // PAD 5
    0x25: 4,  // PAD 6
    0x26: -8, // PAD 7
    0x27: 8   // PAD 8
};

// Stems (KEYBOARD) pads mode status for deck 1 and 2, without or with SHIFT pressed
PioneerDDJFLX4Crate.stemsPadsModesStatus = {
    "[Channel1]": [0x97, 0x98],
    "[Channel2]": [0x99, 0x9a],
};

// Stems (KEYBOARD) pad 1 control (pad control = [this value] + [pad  number] - 1)
PioneerDDJFLX4Crate.stemMutePadsFirstControl = 0x40;

// Stems (KEYBOARD) pad 5 control (pad control = [this value] + [pad  number] - 1)
PioneerDDJFLX4Crate.stemFxPadsFirstControl = 0x44;

// Pitch shift (KEY SHIFT) pads mode status for deck 1 and 2, without or with SHIFT pressed
PioneerDDJFLX4Crate.pitchPadsModesStatus = {
    "[Channel1]": [0x97, 0x98],
    "[Channel2]": [0x99, 0x9a],
};

// Pitch shift (KEY SHIFT) pad 1 control (pad control = [this value] + [pad  number] - 1)
PioneerDDJFLX4Crate.pitchPadsFirstControl = 0x70;

PioneerDDJFLX4Crate.quickJumpSize = 32;

// Used for tempo slider
PioneerDDJFLX4Crate.highResMSB = {
    "[Channel1]": {},
    "[Channel2]": {}
};

PioneerDDJFLX4Crate.trackLoadedLED = function(value, group, _control) {
    midi.sendShortMsg(
        0x9F,
        group.match(script.channelRegEx)[1] - 1,
        value > 0 ? 0x7F : 0x00
    );
};

PioneerDDJFLX4Crate.toggleLight = function(midiIn, active) {
    midi.sendShortMsg(midiIn.status, midiIn.data1, active ? 0x7F : 0);
};

//
// Init
//

PioneerDDJFLX4Crate.init = function() {
    engine.setValue("[EffectRack1_EffectUnit1]", "show_focus", 1);
    engine.setValue("[EffectRack1_EffectUnit2]", "show_focus", 1);

    engine.makeConnection("[Channel1]", "vu_meter", PioneerDDJFLX4Crate.vuMeterUpdate);
    engine.makeConnection("[Channel2]", "vu_meter", PioneerDDJFLX4Crate.vuMeterUpdate);

    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.deck1.vuMeter, false);
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.deck2.vuMeter, false);

    engine.softTakeover("[Channel1]", "rate", true);
    engine.softTakeover("[Channel2]", "rate", true);
    // BEAT FX LEVEL/DEPTH drives the addressed unit's super1 + mix directly
    // (absolute knob, no soft takeover) so the intensity and wet always follow
    // the physical position - the fix for "the knob doesn't affect the mix".

    const samplerCount = 16;
    if (engine.getValue("[App]", "num_samplers") < samplerCount) {
        engine.setValue("[App]", "num_samplers", samplerCount);
    }
    for (let i = 1; i <= samplerCount; ++i) {
        engine.makeConnection("[Sampler" + i + "]", "play", PioneerDDJFLX4Crate.samplerPlayOutputCallbackFunction);
    }

    engine.makeConnection("[Channel1]", "track_loaded", PioneerDDJFLX4Crate.trackLoadedLED);
    engine.makeConnection("[Channel2]", "track_loaded", PioneerDDJFLX4Crate.trackLoadedLED);

    // play the "track loaded" animation on both decks at startup
    midi.sendShortMsg(0x9F, 0x00, 0x7F);
    midi.sendShortMsg(0x9F, 0x01, 0x7F);

    PioneerDDJFLX4Crate.setLoopButtonLights(0x90, 0x7F);
    PioneerDDJFLX4Crate.setLoopButtonLights(0x91, 0x7F);

    engine.makeConnection("[Channel1]", "loop_enabled", PioneerDDJFLX4Crate.loopToggle);
    engine.makeConnection("[Channel2]", "loop_enabled", PioneerDDJFLX4Crate.loopToggle);

    // The BEAT FX light tracks slot 0 (Effect1) of each unit; whichever unit(s)
    // are currently addressed drive the lit state (see updateBeatFxLights).
    engine.makeConnection("[EffectRack1_EffectUnit1_Effect1]", "enabled", PioneerDDJFLX4Crate.toggleFxLight);
    engine.makeConnection("[EffectRack1_EffectUnit2_Effect1]", "enabled", PioneerDDJFLX4Crate.toggleFxLight);

    // Register callbacks for each deck, when a file is loaded and the number of stems is available
    engine.makeConnection("[Channel1]", "stem_count", PioneerDDJFLX4Crate.stemCountChanged);
    engine.makeConnection("[Channel2]", "stem_count", PioneerDDJFLX4Crate.stemCountChanged);

    // Register callbacks for each stems of each decks, to change pad lights when muted/unmuted/FX
    for (let stem=1; stem<=4; stem++) {
        for (let deck=1; deck<=2; deck++) {
            engine.makeConnection(`[Channel${deck}_Stem${stem}]`, "mute", PioneerDDJFLX4Crate.stemMuteChanged);
            engine.makeConnection(`[QuickEffectRack1_[Channel${deck}_Stem${stem}]]`, "enabled", PioneerDDJFLX4Crate.stemFxChanged);
        }
    }

    // Register callbacks for each deck, when a file is loaded to reset pitch shift
    engine.makeConnection("[Channel1]", "track_loaded", PioneerDDJFLX4Crate.pitchAdjusted);
    engine.makeConnection("[Channel2]", "track_loaded", PioneerDDJFLX4Crate.pitchAdjusted);

    // Register callbacks for each deck, when the pitch shift is modified
    engine.makeConnection("[Channel1]", "pitch_adjust", PioneerDDJFLX4Crate.pitchAdjusted);
    engine.makeConnection("[Channel2]", "pitch_adjust", PioneerDDJFLX4Crate.pitchAdjusted);

    PioneerDDJFLX4Crate.keepAliveTimer = engine.beginTimer(200, PioneerDDJFLX4Crate.sendKeepAlive);

    // query the controller for current control positions on startup
    PioneerDDJFLX4Crate.sendKeepAlive(); // the query seems to double as a keep alive message
};

//
// Waveform zoom
//

PioneerDDJFLX4Crate.waveformZoom = function(midichan, control, value, status, group) {
    if (value === 0x7f) {
        script.triggerControl(group, "waveform_zoom_up", 100);
    } else {
        script.triggerControl(group, "waveform_zoom_down", 100);
    }
};

//
// Channel level lights
//

PioneerDDJFLX4Crate.vuMeterUpdate = function(value, group) {
    const newVal = value * 127;

    switch (group) {
    case "[Channel1]":
        midi.sendShortMsg(0xB0, 0x02, newVal);
        break;

    case "[Channel2]":
        midi.sendShortMsg(0xB1, 0x02, newVal);
        break;
    }
};

//
// Effects
//

// Per-deck BEAT FX model. FX unit 1 is permanently wired to deck A and unit 2
// to deck B (the fixed routing is seeded in EffectsManager). CH SELECT does NOT
// re-route; it chooses which unit(s) the hardware section (SELECT / ON / LEVEL)
// currently EDITS. beatFxAddressedUnits holds the one-based unit number(s):
//   [1]    -> CH1    (edit unit 1 / deck A)
//   [2]    -> CH2    (edit unit 2 / deck B)
//   [1, 2] -> MASTER (edit both units together)
PioneerDDJFLX4Crate.beatFxAddressedUnits = [1];

PioneerDDJFLX4Crate.beatFxUnitGroup = function(unit) {
    return "[EffectRack1_EffectUnit" + unit + "]";
};

// The section always edits slot 0 (Effect1) of the addressed unit.
PioneerDDJFLX4Crate.beatFxSlotGroup = function(unit) {
    return "[EffectRack1_EffectUnit" + unit + "_Effect1]";
};

PioneerDDJFLX4Crate.updateBeatFxLights = function() {
    const enabled = PioneerDDJFLX4Crate.beatFxAddressedUnits.some(function(unit) {
        return engine.getValue(PioneerDDJFLX4Crate.beatFxSlotGroup(unit), "enabled") > 0;
    });

    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.beatFx, enabled);
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.shiftBeatFx, enabled);
};

PioneerDDJFLX4Crate.toggleFxLight = function(_value, _group, _control) {
    PioneerDDJFLX4Crate.updateBeatFxLights();
};

PioneerDDJFLX4Crate.beatFxLevelDepthRotate = function(_channel, _control, value) {
    const param = value / 0x7F;
    const shift = PioneerDDJFLX4Crate.shiftButtonDown[0] || PioneerDDJFLX4Crate.shiftButtonDown[1];

    PioneerDDJFLX4Crate.beatFxAddressedUnits.forEach(function(unit) {
        const group = PioneerDDJFLX4Crate.beatFxUnitGroup(unit);
        if (shift) {
            // SHIFT: fine dry/wet trim only (mix), leaving intensity where it is.
            engine.setParameter(group, "mix", param);
        } else {
            // No SHIFT: intensity (super1, which turns every loaded effect's
            // meta together) AND wet (mix) rise as one - the rekordbox feel.
            engine.setParameter(group, "super1", param);
            engine.setParameter(group, "mix", param);
        }
    });
};

PioneerDDJFLX4Crate.changeFocusedEffectBy = function(numberOfSteps) {
    let focusedEffect = engine.getValue("[EffectRack1_EffectUnit1]", "focused_effect");

    // Convert to zero-based index
    focusedEffect -= 1;

    // Standard Euclidean modulo by use of two plain modulos
    const numberOfEffectsPerEffectUnit = 3;
    focusedEffect = (((focusedEffect + numberOfSteps) % numberOfEffectsPerEffectUnit) + numberOfEffectsPerEffectUnit) % numberOfEffectsPerEffectUnit;

    // Convert back to one-based index
    focusedEffect += 1;

    engine.setValue("[EffectRack1_EffectUnit1]", "focused_effect", focusedEffect);
};

PioneerDDJFLX4Crate.beatFxSelectPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    PioneerDDJFLX4Crate.beatFxAddressedUnits.forEach(function(unit) {
        engine.setValue(PioneerDDJFLX4Crate.beatFxSlotGroup(unit), "next_effect", value);
    });
};

PioneerDDJFLX4Crate.beatFxSelectShiftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    PioneerDDJFLX4Crate.beatFxAddressedUnits.forEach(function(unit) {
        engine.setValue(PioneerDDJFLX4Crate.beatFxSlotGroup(unit), "prev_effect", value);
    });
};

PioneerDDJFLX4Crate.beatFxLeftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    PioneerDDJFLX4Crate.changeFocusedEffectBy(-1);
};

PioneerDDJFLX4Crate.beatFxRightPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    PioneerDDJFLX4Crate.changeFocusedEffectBy(1);
};

PioneerDDJFLX4Crate.beatFxOnOffPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    // Toggle both addressed units together off the first unit's current state,
    // so MASTER moves them as one instead of drifting out of phase.
    const units = PioneerDDJFLX4Crate.beatFxAddressedUnits;
    const target = engine.getValue(PioneerDDJFLX4Crate.beatFxSlotGroup(units[0]), "enabled") ? 0 : 1;

    units.forEach(function(unit) {
        engine.setValue(PioneerDDJFLX4Crate.beatFxSlotGroup(unit), "enabled", target);
    });
};

PioneerDDJFLX4Crate.beatFxOnOffShiftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    PioneerDDJFLX4Crate.beatFxAddressedUnits.forEach(function(unit) {
        const group = PioneerDDJFLX4Crate.beatFxUnitGroup(unit);
        engine.setParameter(group, "mix", 0);

        for (let i = 1; i <= 3; i++) {
            engine.setValue("[EffectRack1_EffectUnit" + unit + "_Effect" + i + "]", "enabled", 0);
        }
    });

    PioneerDDJFLX4Crate.updateBeatFxLights();
};

// CH SELECT is a 3-position switch. Each position sends its own Note-On with
// value 0x7F (and the position being left sends value 0x00). It picks which
// effect unit(s) the BEAT FX section edits - it does NOT change the fixed
// unit->deck routing. Positions (per the DDJ-FLX4/DDJ-400 MIDI spec):
//   CH1    -> 0x94 / 0x10  -> edit unit 1 (deck A)
//   CH2    -> 0x95 / 0x11  -> edit unit 2 (deck B)
//   MASTER -> 0x94 / 0x14  -> edit both units together
// The position being left sends value 0x00, which is ignored so it never
// clobbers the freshly selected addressing.
PioneerDDJFLX4Crate.beatFxChannelSelect = function(_channel, control, value) {
    if (value === 0) { return; }

    if (control === 0x10) {
        PioneerDDJFLX4Crate.beatFxAddressedUnits = [1];
    } else if (control === 0x11) {
        PioneerDDJFLX4Crate.beatFxAddressedUnits = [2];
    } else if (control === 0x14) {
        PioneerDDJFLX4Crate.beatFxAddressedUnits = [1, 2];
    }

    PioneerDDJFLX4Crate.updateBeatFxLights();
};

//
// Loop IN/OUT ADJUST
//

PioneerDDJFLX4Crate.toggleLoopAdjustIn = function(channel, _control, value, _status, group) {
    if (value === 0 || engine.getValue(group, "loop_enabled" === 0)) {
        return;
    }
    PioneerDDJFLX4Crate.loopAdjustIn[channel] = !PioneerDDJFLX4Crate.loopAdjustIn[channel];
    PioneerDDJFLX4Crate.loopAdjustOut[channel] = false;
};

PioneerDDJFLX4Crate.toggleLoopAdjustOut = function(channel, _control, value, _status, group) {
    if (value === 0 || engine.getValue(group, "loop_enabled" === 0)) {
        return;
    }
    PioneerDDJFLX4Crate.loopAdjustOut[channel] = !PioneerDDJFLX4Crate.loopAdjustOut[channel];
    PioneerDDJFLX4Crate.loopAdjustIn[channel] = false;
};

// Two signals are sent here so that the light stays lit/unlit in its shift state too
PioneerDDJFLX4Crate.setReloopLight = function(status, value) {
    midi.sendShortMsg(status, 0x4D, value);
    midi.sendShortMsg(status, 0x50, value);
};


PioneerDDJFLX4Crate.setLoopButtonLights = function(status, value) {
    [0x10, 0x11, 0x4E, 0x4C].forEach(function(control) {
        midi.sendShortMsg(status, control, value);
    });
};

PioneerDDJFLX4Crate.startLoopLightsBlink = function(channel, control, status, group) {
    let blink = 0x7F;

    PioneerDDJFLX4Crate.stopLoopLightsBlink(group, control, status);

    PioneerDDJFLX4Crate.timers[group][control] = engine.beginTimer(500, () => {
        blink = 0x7F - blink;

        // When adjusting the loop out position, turn the loop in light off
        if (PioneerDDJFLX4Crate.loopAdjustOut[channel]) {
            midi.sendShortMsg(status, 0x10, 0x00);
            midi.sendShortMsg(status, 0x4C, 0x00);
        } else {
            midi.sendShortMsg(status, 0x10, blink);
            midi.sendShortMsg(status, 0x4C, blink);
        }

        // When adjusting the loop in position, turn the loop out light off
        if (PioneerDDJFLX4Crate.loopAdjustIn[channel]) {
            midi.sendShortMsg(status, 0x11, 0x00);
            midi.sendShortMsg(status, 0x4E, 0x00);
        } else {
            midi.sendShortMsg(status, 0x11, blink);
            midi.sendShortMsg(status, 0x4E, blink);
        }
    });

};

PioneerDDJFLX4Crate.stopLoopLightsBlink = function(group, control, status) {
    PioneerDDJFLX4Crate.timers[group] = PioneerDDJFLX4Crate.timers[group] || {};

    if (PioneerDDJFLX4Crate.timers[group][control] !== undefined) {
        engine.stopTimer(PioneerDDJFLX4Crate.timers[group][control]);
    }
    PioneerDDJFLX4Crate.timers[group][control] = undefined;
    PioneerDDJFLX4Crate.setLoopButtonLights(status, 0x7F);
};

PioneerDDJFLX4Crate.loopToggle = function(value, group, control) {
    const status = group === "[Channel1]" ? 0x90 : 0x91,
        channel = group === "[Channel1]" ? 0 : 1;

    PioneerDDJFLX4Crate.setReloopLight(status, value ? 0x7F : 0x00);

    if (value) {
        PioneerDDJFLX4Crate.startLoopLightsBlink(channel, control, status, group);
    } else {
        PioneerDDJFLX4Crate.stopLoopLightsBlink(group, control, status);
        PioneerDDJFLX4Crate.loopAdjustIn[channel] = false;
        PioneerDDJFLX4Crate.loopAdjustOut[channel] = false;
    }
};

//
// CUE/LOOP CALL
//

PioneerDDJFLX4Crate.cueLoopCallLeft = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "loop_scale", 0.5);
    }
};

PioneerDDJFLX4Crate.cueLoopCallRight = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "loop_scale", 2.0);
    }
};

//
// LOOP IN / OUT / 4 BEAT (rekordbox model)
//
// LOOP IN press with no active loop sets the loop-in point (manual loop build).
// While a loop is active, holding IN and turning the jog moves the loop-in
// point (no SHIFT); releasing IN returns the jog to normal. OUT mirrors this
// for the loop-out point. The jog handler (jogTurn) reads loopAdjustIn/Out and
// suppresses scratch/nudge while a point is being adjusted.

PioneerDDJFLX4Crate.loopInPress = function(channel, _control, value, _status, group) {
    if (value === 0x7F) {
        if (engine.getValue(group, "loop_enabled") > 0) {
            // Active loop: hold IN + jog moves the loop-in point.
            PioneerDDJFLX4Crate.loopAdjustIn[channel] = true;
            PioneerDDJFLX4Crate.loopAdjustOut[channel] = false;
        } else {
            // No loop: set the loop-in point.
            engine.setValue(group, "loop_in", 1);
        }
    } else {
        // Release returns the jog to normal.
        PioneerDDJFLX4Crate.loopAdjustIn[channel] = false;
    }
};

PioneerDDJFLX4Crate.loopOutPress = function(channel, _control, value, _status, group) {
    if (value === 0x7F) {
        if (engine.getValue(group, "loop_enabled") > 0) {
            // Active loop: hold OUT + jog moves the loop-out point.
            PioneerDDJFLX4Crate.loopAdjustOut[channel] = true;
            PioneerDDJFLX4Crate.loopAdjustIn[channel] = false;
        } else {
            // No loop: set the loop-out point (closes a manual loop).
            engine.setValue(group, "loop_out", 1);
        }
    } else {
        // Release returns the jog to normal.
        PioneerDDJFLX4Crate.loopAdjustOut[channel] = false;
    }
};

// 4 BEAT / EXIT (the RELOOP/EXIT button): a cold press with no active loop
// starts an instant quantized 4-beat loop; a press during an active loop exits
// it (playback continues past the out point). SHIFT + press (a separate note)
// re-enters the last loop via reloop_toggle.
PioneerDDJFLX4Crate.fourBeatExit = function(_channel, _control, value, _status, group) {
    if (value === 0) {
        return;
    }
    if (engine.getValue(group, "loop_enabled") > 0) {
        // Active loop: exit but keep playing.
        engine.setValue(group, "reloop_toggle", 1);
    } else {
        // No loop: instant quantized 4-beat loop at the current position.
        engine.setValue(group, "beatloop_4_activate", 1);
    }
};

//
// BEAT SYNC
//
// Note that the controller sends different signals for a short press and a long
// press of the same button.
//

PioneerDDJFLX4Crate.syncPressed = function(channel, control, value, status, group) {
    if (engine.getValue(group, "sync_enabled") && value > 0) {
        engine.setValue(group, "sync_enabled", 0);
    } else {
        engine.setValue(group, "beatsync", value);
    }
};

PioneerDDJFLX4Crate.syncLongPressed = function(channel, control, value, status, group) {
    if (value) {
        engine.setValue(group, "sync_enabled", 1);
    }
};

PioneerDDJFLX4Crate.cycleTempoRange = function(_channel, _control, value, _status, group) {
    if (value === 0) { return; } // ignore release

    const currRange = engine.getValue(group, "rateRange");
    let idx = 0;

    for (let i = 0; i < this.tempoRanges.length; i++) {
        if (currRange === this.tempoRanges[i]) {
            // idx get the index of the value in tempoRanges following the currently configured one
            // or cycle back to 0 if the current is the last value of the list.
            idx = (i + 1) % this.tempoRanges.length;
            break;
        }
    }
    engine.setValue(group, "rateRange", this.tempoRanges[idx]);
};

//
// Jog wheels
//

PioneerDDJFLX4Crate.jogTurn = function(channel, _control, value, _status, group) {
    const deckNum = channel + 1;
    // wheel center at 64; <64 rew >64 fwd
    let newVal = value - 64;

    // loop_in / out adjust
    const loopEnabled = engine.getValue(group, "loop_enabled");
    if (loopEnabled > 0) {
        if (PioneerDDJFLX4Crate.loopAdjustIn[channel]) {
            newVal = newVal * PioneerDDJFLX4Crate.loopAdjustMultiply + engine.getValue(group, "loop_start_position");
            engine.setValue(group, "loop_start_position", newVal);
            return;
        }
        if (PioneerDDJFLX4Crate.loopAdjustOut[channel]) {
            newVal = newVal * PioneerDDJFLX4Crate.loopAdjustMultiply + engine.getValue(group, "loop_end_position");
            engine.setValue(group, "loop_end_position", newVal);
            return;
        }
    }

    if (engine.isScratching(deckNum)) {
        engine.scratchTick(deckNum, newVal);
    } else { // fallback
        engine.setValue(group, "jog", newVal * this.bendScale);
    }
};


PioneerDDJFLX4Crate.jogSearch = function(_channel, _control, value, _status, group) {
    const newVal = (value - 64) * PioneerDDJFLX4Crate.fastSeekScale;
    engine.setValue(group, "jog", newVal);
};

PioneerDDJFLX4Crate.jogTouch = function(channel, _control, value) {
    const deckNum = channel + 1;

    // skip while adjusting the loop points
    if (PioneerDDJFLX4Crate.loopAdjustIn[channel] || PioneerDDJFLX4Crate.loopAdjustOut[channel]) {
        return;
    }

    if (value !== 0 && this.vinylMode) {
        engine.scratchEnable(deckNum, 720, 33+1/3, this.alpha, this.beta);
    } else {
        engine.scratchDisable(deckNum);
    }
};

//
// Shift button
//

PioneerDDJFLX4Crate.shiftPressed = function(channel, _control, value, _status, _group) {
    PioneerDDJFLX4Crate.shiftButtonDown[channel] = value === 0x7F;
};

// Crate additions preserve the stock actions and layer map controls on top.
PioneerDDJFLX4Crate.crateBrowseShiftPressed = function(
        _channel, _control, value, _status, _group) {
    engine.setValue("[Library]", "MoveFocusBackward", value === 0 ? 0 : 1);
    if (value === 0x7F) {
        engine.setValue("[Crate]", "knob_focus",
                engine.getValue("[Crate]", "knob_focus") === 0 ? 1 : 0);
    }
};

PioneerDDJFLX4Crate.crateLoadPressed = function(
        _channel, _control, value, _status, group) {
    engine.setValue(group, "LoadSelectedTrack", value === 0 ? 0 : 1);

    const deck = group === "[Channel1]" ? 0 : 1;
    if (!PioneerDDJFLX4Crate.shiftButtonDown[deck]) {
        return;
    }
    const crateKey = deck === 0 ? "galaxy_load" : "galaxy_reload";
    engine.setValue("[Crate]", crateKey, value === 0 ? 0 : 1);
};


//
// Tempo sliders
//
// The tempo option in Mixxx's deck preferences determine whether down/up
// increases/decreases the rate. Therefore it must be inverted here so that the
// UI and the control sliders always move in the same direction.
//

PioneerDDJFLX4Crate.tempoSliderMSB = function(channel, control, value, status, group) {
    PioneerDDJFLX4Crate.highResMSB[group].tempoSlider = value;
};

PioneerDDJFLX4Crate.tempoSliderLSB = function(channel, control, value, status, group) {
    const fullValue = (PioneerDDJFLX4Crate.highResMSB[group].tempoSlider << 7) + value;

    engine.setValue(
        group,
        "rate",
        1 - (fullValue / 0x2000)
    );
};

//
// Beat Jump mode
//
// Note that when we increase/decrease the sizes on the pad buttons, we use the
// value of the first pad (0x21) as an upper/lower limit beyond which we don't
// allow further increasing/decreasing of all the values.
//

PioneerDDJFLX4Crate.beatjumpPadPressed = function(_channel, control, value, _status, group) {
    if (value === 0) {
        return;
    }
    engine.setValue(group, "beatjump_size", Math.abs(PioneerDDJFLX4Crate.beatjumpSizeForPad[control]));
    engine.setValue(group, "beatjump", PioneerDDJFLX4Crate.beatjumpSizeForPad[control]);
};

PioneerDDJFLX4Crate.increaseBeatjumpSizes = function(_channel, control, value, _status, group) {
    if (value === 0 || PioneerDDJFLX4Crate.beatjumpSizeForPad[0x21] * 16 > 16) {
        return;
    }
    Object.keys(PioneerDDJFLX4Crate.beatjumpSizeForPad).forEach(function(pad) {
        PioneerDDJFLX4Crate.beatjumpSizeForPad[pad] = PioneerDDJFLX4Crate.beatjumpSizeForPad[pad] * 16;
    });
    engine.setValue(group, "beatjump_size", PioneerDDJFLX4Crate.beatjumpSizeForPad[0x21]);
};

PioneerDDJFLX4Crate.decreaseBeatjumpSizes = function(_channel, control, value, _status, group) {
    if (value === 0 || PioneerDDJFLX4Crate.beatjumpSizeForPad[0x21] / 16 < 1/16) {
        return;
    }
    Object.keys(PioneerDDJFLX4Crate.beatjumpSizeForPad).forEach(function(pad) {
        PioneerDDJFLX4Crate.beatjumpSizeForPad[pad] = PioneerDDJFLX4Crate.beatjumpSizeForPad[pad] / 16;
    });
    engine.setValue(group, "beatjump_size", PioneerDDJFLX4Crate.beatjumpSizeForPad[0x21]);
};

//
// Sampler mode
//

PioneerDDJFLX4Crate.samplerPlayOutputCallbackFunction = function(value, group, _control) {
    if (value === 1) {
        const curPad = group.match(script.samplerRegEx)[1];
        let deckIndex = 0;
        let padIndex = 0;

        if (curPad >=1 && curPad <= 4) {
            deckIndex = 0;
            padIndex = curPad - 1;
        } else if (curPad >=5 && curPad <= 8) {
            deckIndex = 2;
            padIndex = curPad - 5;
        } else if (curPad >=9 && curPad <= 12) {
            deckIndex = 0;
            padIndex = curPad - 5;
        } else if (curPad >=13 && curPad <= 16) {
            deckIndex = 2;
            padIndex = curPad - 9;
        }

        PioneerDDJFLX4Crate.startSamplerBlink(
            0x97 + deckIndex,
            0x30 + padIndex,
            group);
    }
};

PioneerDDJFLX4Crate.padModeKeyPressed = function(_channel, _control, value, _status, _group) {
    const deck = (_status === 0x90 ? PioneerDDJFLX4Crate.lights.deck1 : PioneerDDJFLX4Crate.lights.deck2);

    if (_control === 0x1B) {
        PioneerDDJFLX4Crate.toggleLight(deck.hotcueMode, true);
    } else if (_control === 0x69) {
        PioneerDDJFLX4Crate.toggleLight(deck.keyboardMode, true);
    } else if (_control === 0x1E) {
        PioneerDDJFLX4Crate.toggleLight(deck.padFX1Mode, true);
    } else if (_control === 0x6B) {
        PioneerDDJFLX4Crate.toggleLight(deck.padFX2Mode, true);
    } else if (_control === 0x20) {
        PioneerDDJFLX4Crate.toggleLight(deck.beatJumpMode, true);
    } else if (_control === 0x6D) {
        PioneerDDJFLX4Crate.toggleLight(deck.beatLoopMode, true);
    } else if (_control === 0x22) {
        PioneerDDJFLX4Crate.toggleLight(deck.samplerMode, true);
    } else if (_control === 0x6F) {
        PioneerDDJFLX4Crate.toggleLight(deck.keyShiftMode, true);
    }
};

PioneerDDJFLX4Crate.samplerPadPressed = function(_channel, _control, value, _status, group) {
    if (engine.getValue(group, "track_loaded")) {
        engine.setValue(group, "cue_gotoandplay", value);
    } else {
        engine.setValue(group, "LoadSelectedTrack", value);
    }
};

PioneerDDJFLX4Crate.samplerPadShiftPressed = function(_channel, _control, value, _status, group) {
    if (engine.getValue(group, "play")) {
        engine.setValue(group, "cue_gotoandstop", value);
    } else if (engine.getValue(group, "track_loaded")) {
        engine.setValue(group, "eject", value);
    }
};

PioneerDDJFLX4Crate.startSamplerBlink = function(channel, control, group) {
    let val = 0x7f;

    PioneerDDJFLX4Crate.stopSamplerBlink(channel, control);
    PioneerDDJFLX4Crate.timers[channel][control] = engine.beginTimer(250, () => {
        val = 0x7f - val;

        // blink the appropriate pad
        midi.sendShortMsg(channel, control, val);
        // also blink the pad while SHIFT is pressed
        midi.sendShortMsg((channel+1), control, val);

        const isPlaying = engine.getValue(group, "play") === 1;

        if (!isPlaying) {
            // kill timer
            PioneerDDJFLX4Crate.stopSamplerBlink(channel, control);
            // set the pad LED to ON
            midi.sendShortMsg(channel, control, 0x7f);
            // set the pad LED to ON while SHIFT is pressed
            midi.sendShortMsg((channel+1), control, 0x7f);
        }
    });
};

PioneerDDJFLX4Crate.stopSamplerBlink = function(channel, control) {
    PioneerDDJFLX4Crate.timers[channel] = PioneerDDJFLX4Crate.timers[channel] || {};

    if (PioneerDDJFLX4Crate.timers[channel][control] !== undefined) {
        engine.stopTimer(PioneerDDJFLX4Crate.timers[channel][control]);
        PioneerDDJFLX4Crate.timers[channel][control] = undefined;
    }
};


PioneerDDJFLX4Crate.toggleQuantize = function(_channel, _control, value, _status, group) {
    if (value) {
        script.toggleControl(group, "quantize");
    }
};

PioneerDDJFLX4Crate.quickJumpForward = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "beatjump", PioneerDDJFLX4Crate.quickJumpSize);
    }
};

PioneerDDJFLX4Crate.quickJumpBack = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "beatjump", -PioneerDDJFLX4Crate.quickJumpSize);
    }
};

//
// Stems mode
//

PioneerDDJFLX4Crate.stemMutePadPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    const stemCount = Math.min(engine.getValue(group, "stem_count"), 4);

    if (control - PioneerDDJFLX4Crate.stemMutePadsFirstControl + 1 > stemCount) {
        return;
    }

    const stemGroup = `[${group.substring(1, group.length-1)}_Stem${control - PioneerDDJFLX4Crate.stemMutePadsFirstControl + 1}]`;

    if (engine.getValue(stemGroup, "mute")) {
        engine.setValue(stemGroup, "mute", 0);
    } else {
        engine.setValue(stemGroup, "mute", 1);
    }
};

PioneerDDJFLX4Crate.stemMutePadShiftPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    const stemCount = Math.min(engine.getValue(group, "stem_count"), 4);

    if (control - PioneerDDJFLX4Crate.stemMutePadsFirstControl + 1 > stemCount) {
        return;
    }

    for (let stemIdx=1; stemIdx<=stemCount; stemIdx++) {
        const stemGroup = `[${group.substring(1, group.length-1)}_Stem${stemIdx}]`;

        if (stemIdx + PioneerDDJFLX4Crate.stemMutePadsFirstControl - 1 === control) {
            engine.setValue(stemGroup, "mute", 0);
        } else {
            engine.setValue(stemGroup, "mute", 1);
        }
    }
};

PioneerDDJFLX4Crate.stemFxPadPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    if (control - PioneerDDJFLX4Crate.stemFxPadsFirstControl + 1 > 4) {
        return;
    }

    const stemGroup = `[QuickEffectRack1_[${group.substring(1, group.length-1)}_Stem${control - PioneerDDJFLX4Crate.stemFxPadsFirstControl + 1}]]`;

    if (engine.getValue(stemGroup, "enabled")) {
        engine.setValue(stemGroup, "enabled", 0);
    } else {
        engine.setValue(stemGroup, "enabled", 1);
    }
};

PioneerDDJFLX4Crate.stemFxPadShiftPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    if (control - PioneerDDJFLX4Crate.stemFxPadsFirstControl + 1 > 4) {
        return;
    }

    const stemGroup = `[QuickEffectRack1_[${group.substring(1, group.length-1)}_Stem${control - PioneerDDJFLX4Crate.stemFxPadsFirstControl + 1}]]`;

    engine.setValue(stemGroup, "next_chain_preset", 1);
};

PioneerDDJFLX4Crate.stemCountChanged = function(_value, group, _control) {

    for (let stem=1; stem<=4; stem++) {
        // Stem mute pads
        PioneerDDJFLX4Crate.stemMuteChanged(
            engine.getValue(`[${group.substring(1, group.length-1)}_Stem${stem}]`, "mute"),
            `[${group.substring(1, group.length-1)}_Stem${stem}]`,
            _control,
        );

        // Stem FX pads
        PioneerDDJFLX4Crate.stemFxChanged(
            engine.getValue(`[QuickEffectRack1_[${group.substring(1, group.length-1)}_Stem${stem}]]`, "enabled"),
            `[QuickEffectRack1_[${group.substring(1, group.length-1)}_Stem${stem}]]`,
            _control,
        );
    }
};

PioneerDDJFLX4Crate.stemMuteChanged = function(value, group, _control) {
    const channelStem = group.match(/\[Channel(\d+)_Stem(\d+)\]/);
    const deck = Number(channelStem[1]);
    const stem = Number(channelStem[2]);
    const channel = `[Channel${deck}]`;

    if (stem > 4) {
        return;
    }

    const stemCount = engine.getValue(channel, "stem_count");

    let code = 0x00;
    if (stem <= stemCount && value <= 0.5) {
        code = 0x7f;
    }

    for (let i=0; i<PioneerDDJFLX4Crate.stemsPadsModesStatus[channel].length; i++) {
        midi.sendShortMsg(
            PioneerDDJFLX4Crate.stemsPadsModesStatus[channel][i],
            PioneerDDJFLX4Crate.stemMutePadsFirstControl + stem -1,
            code,
        );
    }
};

PioneerDDJFLX4Crate.stemFxChanged = function(value, group, _control) {
    const channelStem = group.match(/\[QuickEffectRack1_\[Channel(\d+)_Stem(\d+)\]\]/);
    const deck = Number(channelStem[1]);
    const stem = Number(channelStem[2]);
    const channel = `[Channel${deck}]`;

    if (stem > 4) {
        return;
    }

    for (let i=0; i<PioneerDDJFLX4Crate.stemsPadsModesStatus[channel].length; i++) {
        midi.sendShortMsg(
            PioneerDDJFLX4Crate.stemsPadsModesStatus[channel][i],
            PioneerDDJFLX4Crate.stemFxPadsFirstControl + stem -1,
            value <= 0.5 ? 0x00 : 0x7f,
        );
    }
};

//
// Pitch Shift mode
//

PioneerDDJFLX4Crate.pitchAdjusted = function(_value, group, _control) {
    const pitchAdjust = Math.round(engine.getValue(group, "pitch_adjust"));
    let lights = 0b00000000;

    if (pitchAdjust === 0) {
        lights = 0b10000001;
    } else if (pitchAdjust === 1) {
        lights = 0b01000000;
    } else if (pitchAdjust === 2) {
        lights = 0b00100000;
    } else if (pitchAdjust === 3) {
        lights = 0b00010000;
    } else if (pitchAdjust === 4) {
        lights = 0b10010000;
    } else if (pitchAdjust === 5) {
        lights = 0b01010000;
    } else if (pitchAdjust === 6) {
        lights = 0b00110000;
    } else if (pitchAdjust === 7) {
        lights = 0b10110000;
    } else if (pitchAdjust === 8) {
        lights = 0b01110000;
    } else if (pitchAdjust > 8) {
        lights = 0b11110000;
    } else if (pitchAdjust === -1) {
        lights = 0b00000010;
    } else if (pitchAdjust === -2) {
        lights = 0b00000100;
    } else if (pitchAdjust === -3) {
        lights = 0b00001000;
    } else if (pitchAdjust === -4) {
        lights = 0b00001001;
    } else if (pitchAdjust === -5) {
        lights = 0b00001010;
    } else if (pitchAdjust === -6) {
        lights = 0b00001100;
    } else if (pitchAdjust === -7) {
        lights = 0b00001101;
    } else if (pitchAdjust === -8) {
        lights = 0b00001110;
    } else if (pitchAdjust < -8) {
        lights = 0b00001111;
    } else {
        lights = 0b11111111;
    }

    for (let i=0; i<8; i++) {
        let code = 0x00;
        const pad = 0b10000000 >>> i;

        if (lights & pad) {
            code = 0x7f;
        } else {
            code = 0x00;
        }

        PioneerDDJFLX4Crate.pitchPadsModesStatus[group].forEach(
            (padMode) => midi.sendShortMsg(
                padMode,
                PioneerDDJFLX4Crate.pitchPadsFirstControl + i,
                code,
            )
        );
    }
};

PioneerDDJFLX4Crate.pitchPadPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    const pad = control - this.pitchPadsFirstControl;
    let pitch = 0;

    if (pad === 0) {
        pitch = 0;
    } else if (pad === 1) {
        pitch = 1;
    } else if (pad === 2) {
        pitch = 2;
    } else if (pad === 3) {
        pitch = 3;
    } else if (pad === 4) {
        pitch = -3;
    } else if (pad === 5) {
        pitch = -2;
    } else if (pad === 6) {
        pitch = -1;
    } else if (pad === 7) {
        pitch = 0;
    }

    engine.setValue(group, "pitch_adjust", pitch);
};

PioneerDDJFLX4Crate.pitchPadShiftPressed = function(_channel, control, value, _status, group) {
    if (value !== 0x7f) {
        return;
    }

    const pad = control - this.pitchPadsFirstControl;

    let currentPitch = engine.getValue(group, "pitch_adjust");

    if (pad === 0) {
        currentPitch += 1;
    } else if (pad === 1) {
        currentPitch += 2;
    } else if (pad === 2) {
        currentPitch += 3;
    } else if (pad === 3) {
        currentPitch += 4;
    } else if (pad === 4) {
        currentPitch += -4;
    } else if (pad === 5) {
        currentPitch += -3;
    } else if (pad === 6) {
        currentPitch += -2;
    } else if (pad === 7) {
        currentPitch += -1;
    }

    engine.setValue(group, "pitch_adjust", currentPitch);
};


//
// Shutdown
//

PioneerDDJFLX4Crate.shutdown = function() {
    // reset vumeter
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.deck1.vuMeter, false);
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.deck2.vuMeter, false);

    // housekeeping
    // turn off all Sampler LEDs
    for (var i = 0; i <= 7; ++i) {
        midi.sendShortMsg(0x97, 0x30 + i, 0x00);    // Deck 1 pads
        midi.sendShortMsg(0x98, 0x30 + i, 0x00);    // Deck 1 pads with SHIFT
        midi.sendShortMsg(0x99, 0x30 + i, 0x00);    // Deck 2 pads
        midi.sendShortMsg(0x9A, 0x30 + i, 0x00);    // Deck 2 pads with SHIFT
    }
    // turn off all Hotcue LEDs
    for (i = 0; i <= 7; ++i) {
        midi.sendShortMsg(0x97, 0x00 + i, 0x00);    // Deck 1 pads
        midi.sendShortMsg(0x98, 0x00 + i, 0x00);    // Deck 1 pads with SHIFT
        midi.sendShortMsg(0x99, 0x00 + i, 0x00);    // Deck 2 pads
        midi.sendShortMsg(0x9A, 0x00 + i, 0x00);    // Deck 2 pads with SHIFT
    }

    // turn off loop in and out lights
    PioneerDDJFLX4Crate.setLoopButtonLights(0x90, 0x00);
    PioneerDDJFLX4Crate.setLoopButtonLights(0x91, 0x00);

    // turn off reloop lights
    PioneerDDJFLX4Crate.setReloopLight(0x90, 0x00);
    PioneerDDJFLX4Crate.setReloopLight(0x91, 0x00);

    // stop any flashing lights
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.beatFx, false);
    PioneerDDJFLX4Crate.toggleLight(PioneerDDJFLX4Crate.lights.shiftBeatFx, false);

    // stop the keepalive timer
    engine.stopTimer(PioneerDDJFLX4Crate.keepAliveTimer);
};
