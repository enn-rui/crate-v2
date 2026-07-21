#include <gmock/gmock.h>

#include <QFile>
#include <QScopedPointer>

#include "control/controlobject.h"
#include "control/controlpotmeter.h"
#include "control/controlpushbutton.h"
#include "controllers/legacycontrollermappingfilehandler.h"
#include "controllers/midi/legacymidicontrollermapping.h"
#include "controllers/midi/midicontroller.h"
#include "controllers/midi/midimessage.h"
#include "controllers/midi/midiutils.h"
#include "controllers/scripting/legacy/controllerscriptenginelegacy.h"
#include "test/mixxxtest.h"
#include "util/time.h"

class MockMidiController : public MidiController {
  public:
    explicit MockMidiController()
            : MidiController("test") {
    }
    ~MockMidiController() override {
    }

    MOCK_METHOD1(open, int(const QString& resourcePath));
    MOCK_METHOD0(close, int());
    MOCK_METHOD3(sendShortMsg,
            void(unsigned char status,
                    unsigned char byte1,
                    unsigned char byte2));
    MOCK_METHOD1(sendBytes, bool(const QByteArray& data));
    MOCK_CONST_METHOD0(isPolling, bool());

    PhysicalTransportProtocol getPhysicalTransportProtocol() const override {
        return PhysicalTransportProtocol::UNKNOWN;
    }
    DataRepresentationProtocol getDataRepresentationProtocol() const override {
        return DataRepresentationProtocol::MIDI;
    }

    QString getVendorString() const override {
        static const QString manufacturer = "Test Manufacturer";
        return manufacturer;
    }
    std::optional<uint16_t> getVendorId() const override {
        return std::nullopt;
    }

    QString getProductString() const override {
        static const QString product = "Test Product";
        return product;
    }
    std::optional<uint16_t> getProductId() const override {
        return std::nullopt;
    }

    QString getSerialNumber() const override {
        static const QString serialNumber = "123456789";
        return serialNumber;
    }

    std::optional<uint8_t> getUsbInterfaceNumber() const override {
        return std::nullopt;
    }
};

class MidiControllerTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_pController.reset(new MockMidiController());
        m_pMapping = std::make_shared<LegacyMidiControllerMapping>();
        m_pController->startEngine();
        m_pController->m_pScriptEngineLegacy->initialize();
    }

    void addMapping(const MidiInputMapping& mapping) {
        m_pMapping->addInputMapping(mapping.key.key, mapping);
    }

    void receivedShortMessage(unsigned char status, unsigned char control, unsigned char value) {
        // TODO(rryan): This test doesn't care about timestamps.
        m_pController->receivedShortMessage(status, control, value, mixxx::Time::elapsed());
    }

    void receivedShortMessage(MidiOpCode opcode, uint8_t channel, uint8_t control, uint8_t value) {
        ASSERT_TRUE((channel & 0xF) == channel);
        receivedShortMessage(
                MidiUtils::statusFromOpCodeAndChannel(opcode, channel),
                control,
                value);
    }

    bool evaluateAndAssert(const QString& code) {
        return m_pController->m_pScriptEngineLegacy->jsEngine()->evaluate(code).isError();
    }

    int getInputMappingCount() {
        return m_pController->m_pMapping->getInputMappings().count();
    }

    void shutdownController() {
        m_pController->m_pScriptEngineLegacy->shutdown();
    }

    void loadCrateFlx4Mapping() {
        const QDir controllerDir(QStringLiteral(RESOURCE_FOLDER "/controllers"));
        auto pMapping = LegacyControllerMappingFileHandler::loadMapping(
                QFileInfo(controllerDir.filePath(
                        QStringLiteral("Pioneer-DDJ-FLX4-Crate.midi.xml"))),
                controllerDir);
        ASSERT_TRUE(pMapping);
        m_pMapping = std::dynamic_pointer_cast<LegacyMidiControllerMapping>(pMapping);
        ASSERT_TRUE(m_pMapping);
        m_pController->setMapping(m_pMapping);

        QFile scriptFile(controllerDir.filePath(
                QStringLiteral("Pioneer-DDJ-FLX4-Crate-script.js")));
        ASSERT_TRUE(scriptFile.open(QIODevice::ReadOnly));
        ASSERT_FALSE(evaluateAndAssert(QString::fromUtf8(scriptFile.readAll())));
    }

    std::shared_ptr<LegacyMidiControllerMapping> m_pMapping;
    QScopedPointer<MockMidiController> m_pController;
};

TEST_F(MidiControllerTest, CrateFlx4ShiftBrowsePressTogglesKnobFocus) {
    ControlPushButton knobFocus(ConfigKey("[Crate]", "knob_focus"));
    knobFocus.setButtonMode(mixxx::control::ButtonMode::Toggle);
    ControlPushButton moveFocusBackward(ConfigKey("[Library]", "MoveFocusBackward"));
    loadCrateFlx4Mapping();

    ASSERT_DOUBLE_EQ(0.0, knobFocus.get());
    receivedShortMessage(0x90, 0x3F, 0x7F); // Deck 1 SHIFT press.
    receivedShortMessage(0x96, 0x42, 0x7F); // SHIFT + browse encoder press.
    EXPECT_DOUBLE_EQ(1.0, knobFocus.get());
    EXPECT_DOUBLE_EQ(1.0, moveFocusBackward.get());
}

TEST_F(MidiControllerTest, CrateFlx4ShiftDeck2LoadStrobesGalaxyReload) {
    ControlPushButton galaxyReload(ConfigKey("[Crate]", "galaxy_reload"));
    galaxyReload.setButtonMode(mixxx::control::ButtonMode::Push);
    ControlPushButton loadSelectedTrack(ConfigKey("[Channel2]", "LoadSelectedTrack"));
    loadCrateFlx4Mapping();

    ASSERT_DOUBLE_EQ(0.0, galaxyReload.get());
    receivedShortMessage(0x91, 0x3F, 0x7F); // Deck 2 SHIFT press.
    receivedShortMessage(0x96, 0x47, 0x7F); // Deck 2 LOAD press.
    EXPECT_DOUBLE_EQ(1.0, galaxyReload.get());
    EXPECT_DOUBLE_EQ(1.0, loadSelectedTrack.get());
}

TEST_F(MidiControllerTest, CrateFlx4BeatFxChSelectAddressesUnitExclusively) {
    // CH SELECT no longer routes the unit; it picks which effect unit the BEAT
    // FX section edits (the per-deck model). CH1 -> unit 1, CH2 -> unit 2,
    // MASTER -> both. The knob and ON must hit only the addressed unit(s); the
    // other unit stays untouched.
    ControlPotmeter super1_1(ConfigKey("[EffectRack1_EffectUnit1]", "super1"), 0.0, 1.0);
    ControlPotmeter mix1(ConfigKey("[EffectRack1_EffectUnit1]", "mix"), 0.0, 1.0);
    ControlPotmeter super1_2(ConfigKey("[EffectRack1_EffectUnit2]", "super1"), 0.0, 1.0);
    ControlPotmeter mix2(ConfigKey("[EffectRack1_EffectUnit2]", "mix"), 0.0, 1.0);
    ControlObject enabled1(ConfigKey("[EffectRack1_EffectUnit1_Effect1]", "enabled"));
    ControlObject enabled2(ConfigKey("[EffectRack1_EffectUnit2_Effect1]", "enabled"));
    super1_1.set(0.0);
    mix1.set(0.0);
    super1_2.set(0.0);
    mix2.set(0.0);
    enabled1.set(0.0);
    enabled2.set(0.0);
    loadCrateFlx4Mapping();

    // CH1 position (Note-On 0x94/0x10): the knob drives unit 1 only.
    receivedShortMessage(0x94, 0x10, 0x7F);
    receivedShortMessage(0xB4, 0x02, 0x7F); // LEVEL/DEPTH full.
    EXPECT_DOUBLE_EQ(1.0, super1_1.get());
    EXPECT_DOUBLE_EQ(1.0, mix1.get());
    EXPECT_DOUBLE_EQ(0.0, super1_2.get());
    EXPECT_DOUBLE_EQ(0.0, mix2.get());

    // ON toggles unit 1's slot only.
    receivedShortMessage(0x94, 0x47, 0x7F);
    EXPECT_DOUBLE_EQ(1.0, enabled1.get());
    EXPECT_DOUBLE_EQ(0.0, enabled2.get());

    // CH2 position (Note-On 0x95/0x11): the knob drives unit 2 only; unit 1 is
    // left exactly where it was.
    receivedShortMessage(0x95, 0x11, 0x7F);
    receivedShortMessage(0xB4, 0x02, 0x40);
    EXPECT_NEAR(0x40 / 127.0, super1_2.get(), 1e-9);
    EXPECT_NEAR(0x40 / 127.0, mix2.get(), 1e-9);
    EXPECT_DOUBLE_EQ(1.0, super1_1.get());
    EXPECT_DOUBLE_EQ(1.0, mix1.get());

    // ON now toggles unit 2 only; unit 1 keeps its earlier state.
    receivedShortMessage(0x95, 0x47, 0x7F);
    EXPECT_DOUBLE_EQ(1.0, enabled2.get());
    EXPECT_DOUBLE_EQ(1.0, enabled1.get());

    // MASTER position (Note-On 0x94/0x14): both units move together.
    receivedShortMessage(0x94, 0x14, 0x7F);
    receivedShortMessage(0xB4, 0x02, 0x60);
    EXPECT_NEAR(0x60 / 127.0, super1_1.get(), 1e-9);
    EXPECT_NEAR(0x60 / 127.0, mix1.get(), 1e-9);
    EXPECT_NEAR(0x60 / 127.0, super1_2.get(), 1e-9);
    EXPECT_NEAR(0x60 / 127.0, mix2.get(), 1e-9);

    // ON in MASTER toggles both together (off, from unit 1's current state).
    receivedShortMessage(0x94, 0x47, 0x7F);
    EXPECT_DOUBLE_EQ(0.0, enabled1.get());
    EXPECT_DOUBLE_EQ(0.0, enabled2.get());
}

TEST_F(MidiControllerTest, CrateFlx4BeatFxLevelDepthDrivesSuperAndMixShiftMixOnly) {
    // LEVEL/DEPTH (0xB4/0x02) raises super1 AND mix together on the addressed
    // unit (intensity + wet as one); +SHIFT trims mix only, leaving super1 put.
    ControlPotmeter super1(ConfigKey("[EffectRack1_EffectUnit1]", "super1"), 0.0, 1.0);
    ControlPotmeter mix(ConfigKey("[EffectRack1_EffectUnit1]", "mix"), 0.0, 1.0);
    super1.set(0.0);
    mix.set(0.0);
    loadCrateFlx4Mapping();

    receivedShortMessage(0x94, 0x10, 0x7F); // Address unit 1.

    // No SHIFT: both intensity and wet rise together.
    receivedShortMessage(0xB4, 0x02, 0x7F);
    EXPECT_DOUBLE_EQ(1.0, super1.get());
    EXPECT_DOUBLE_EQ(1.0, mix.get());

    // SHIFT held (Deck 1 SHIFT press 0x90/0x3F): the knob trims mix only and
    // leaves super1 where it was.
    receivedShortMessage(0x90, 0x3F, 0x7F);
    receivedShortMessage(0xB4, 0x02, 0x40);
    EXPECT_NEAR(0x40 / 127.0, mix.get(), 1e-9);
    EXPECT_DOUBLE_EQ(1.0, super1.get());
    receivedShortMessage(0x90, 0x3F, 0x00); // SHIFT release.
}

TEST_F(MidiControllerTest, CrateFlx4BeatFxSelectCyclesAddressedUnitSlotZero) {
    // BEAT FX SELECT cycles slot 0 (Effect1) of the addressed unit; SHIFT+SELECT
    // steps back. Switching CH SELECT to CH2 redirects it to unit 2.
    ControlObject next1(ConfigKey("[EffectRack1_EffectUnit1_Effect1]", "next_effect"));
    ControlObject prev1(ConfigKey("[EffectRack1_EffectUnit1_Effect1]", "prev_effect"));
    ControlObject next2(ConfigKey("[EffectRack1_EffectUnit2_Effect1]", "next_effect"));
    next1.set(0.0);
    prev1.set(0.0);
    next2.set(0.0);
    loadCrateFlx4Mapping();

    receivedShortMessage(0x94, 0x10, 0x7F); // Address unit 1.

    // BEAT FX SELECT: Note-On 0x94/0x63.
    receivedShortMessage(0x94, 0x63, 0x7F);
    EXPECT_DOUBLE_EQ(0x7F, next1.get());
    EXPECT_DOUBLE_EQ(0.0, next2.get());

    // SHIFT + BEAT FX SELECT: Note-On 0x94/0x64 (distinct note).
    receivedShortMessage(0x94, 0x64, 0x7F);
    EXPECT_DOUBLE_EQ(0x7F, prev1.get());

    // Address unit 2 (Note-On 0x95/0x11): SELECT now cycles unit 2's slot 0.
    receivedShortMessage(0x95, 0x11, 0x7F);
    receivedShortMessage(0x94, 0x63, 0x7F);
    EXPECT_DOUBLE_EQ(0x7F, next2.get());
}

TEST_F(MidiControllerTest, CrateFlx4FourBeatExitInstantLoopThenExitAndShiftReloop) {
    // 4 BEAT/EXIT (the RELOOP/EXIT button, note 0x4D): a cold press with no
    // active loop fires an instant quantized 4-beat loop; a press during an
    // active loop exits it (reloop_toggle, keep playing) and must NOT re-fire
    // the beatloop; SHIFT + press (note 0x50) re-enters the last loop.
    ControlObject loopEnabled(ConfigKey("[Channel1]", "loop_enabled"));
    ControlPushButton beatloop4(ConfigKey("[Channel1]", "beatloop_4_activate"));
    ControlPushButton reloopToggle(ConfigKey("[Channel1]", "reloop_toggle"));
    loopEnabled.set(0.0);
    loadCrateFlx4Mapping();

    // Cold press, no loop -> instant 4-beat loop.
    receivedShortMessage(0x90, 0x4D, 0x7F);
    EXPECT_DOUBLE_EQ(1.0, beatloop4.get());
    EXPECT_DOUBLE_EQ(0.0, reloopToggle.get());

    // Simulate the loop now being active; the next press exits it and must not
    // re-fire the beatloop.
    beatloop4.set(0.0);
    loopEnabled.set(1.0);
    receivedShortMessage(0x90, 0x4D, 0x7F);
    EXPECT_DOUBLE_EQ(1.0, reloopToggle.get());
    EXPECT_DOUBLE_EQ(0.0, beatloop4.get());

    // SHIFT + 4 BEAT/EXIT (note 0x50) re-enters the last loop via reloop_toggle.
    reloopToggle.set(0.0);
    receivedShortMessage(0x90, 0x50, 0x7F);
    EXPECT_LT(0.0, reloopToggle.get());
}

TEST_F(MidiControllerTest, CrateFlx4LoopInHoldJogAdjustsStartAndSuppressesScratch) {
    // While a loop is active, holding LOOP IN (note 0x10) and turning the jog
    // (0xB0/0x22) moves the loop-in point via loop_start_position and must NOT
    // write the jog (no scratch/nudge). Releasing IN reverts the jog to normal.
    ControlObject loopEnabled(ConfigKey("[Channel1]", "loop_enabled"));
    ControlObject loopStart(ConfigKey("[Channel1]", "loop_start_position"));
    ControlObject loopEnd(ConfigKey("[Channel1]", "loop_end_position"));
    ControlPushButton loopIn(ConfigKey("[Channel1]", "loop_in"));
    ControlObject jog(ConfigKey("[Channel1]", "jog"));
    loopEnabled.set(1.0);
    loopStart.set(1000.0);
    jog.set(0.0);
    loadCrateFlx4Mapping();

    // IN held during an active loop enters loop-in adjust; it must NOT reset the
    // loop-in point to the playhead.
    receivedShortMessage(0x90, 0x10, 0x7F);
    EXPECT_DOUBLE_EQ(0.0, loopIn.get());

    // Jog forward (value 0x50 = +16) moves loop_start_position and leaves the
    // jog untouched (no scratch/nudge).
    receivedShortMessage(0xB0, 0x22, 0x50);
    EXPECT_LT(1000.0, loopStart.get());
    EXPECT_DOUBLE_EQ(0.0, jog.get());

    // Release IN (note 0x10, value 0x00): the jog reverts to normal (nudge).
    receivedShortMessage(0x90, 0x10, 0x00);
    receivedShortMessage(0xB0, 0x22, 0x50);
    EXPECT_LT(0.0, jog.get());
}

TEST_F(MidiControllerTest, CrateFlx4LoopOutHoldJogAdjustsEndAndSuppressesScratch) {
    // Mirror of the IN case: holding LOOP OUT (note 0x11) during an active loop
    // moves the loop-out point via loop_end_position and suppresses the jog;
    // releasing OUT reverts the jog to normal.
    ControlObject loopEnabled(ConfigKey("[Channel1]", "loop_enabled"));
    ControlObject loopStart(ConfigKey("[Channel1]", "loop_start_position"));
    ControlObject loopEnd(ConfigKey("[Channel1]", "loop_end_position"));
    ControlPushButton loopOut(ConfigKey("[Channel1]", "loop_out"));
    ControlObject jog(ConfigKey("[Channel1]", "jog"));
    loopEnabled.set(1.0);
    loopEnd.set(2000.0);
    jog.set(0.0);
    loadCrateFlx4Mapping();

    receivedShortMessage(0x90, 0x11, 0x7F);
    EXPECT_DOUBLE_EQ(0.0, loopOut.get());

    receivedShortMessage(0xB0, 0x22, 0x50);
    EXPECT_LT(2000.0, loopEnd.get());
    EXPECT_DOUBLE_EQ(0.0, jog.get());

    receivedShortMessage(0x90, 0x11, 0x00);
    receivedShortMessage(0xB0, 0x22, 0x50);
    EXPECT_LT(0.0, jog.get());
}

TEST_F(MidiControllerTest, CrateFlx4LoopInOutPressWithNoLoopSetsPoints) {
    // With no active loop, a LOOP IN / LOOP OUT press sets the loop-in / loop-out
    // point (manual loop build) rather than entering jog-adjust.
    ControlObject loopEnabled(ConfigKey("[Channel1]", "loop_enabled"));
    ControlPushButton loopIn(ConfigKey("[Channel1]", "loop_in"));
    ControlPushButton loopOut(ConfigKey("[Channel1]", "loop_out"));
    loopEnabled.set(0.0);
    loadCrateFlx4Mapping();

    receivedShortMessage(0x90, 0x10, 0x7F);
    EXPECT_LT(0.0, loopIn.get());

    receivedShortMessage(0x90, 0x11, 0x7F);
    EXPECT_LT(0.0, loopOut.get());
}

TEST_F(MidiControllerTest, CrateFlx4CueLoopCallHalvesAndDoublesActiveLoop) {
    // CUE/LOOP CALL left (note 0x51) halves and right (note 0x53) doubles the
    // active loop via loop_scale (0.5 / 2.0).
    ControlObject loopEnabled(ConfigKey("[Channel1]", "loop_enabled"));
    ControlObject loopScale(ConfigKey("[Channel1]", "loop_scale"));
    loopEnabled.set(1.0);
    loadCrateFlx4Mapping();

    loopScale.set(0.0);
    receivedShortMessage(0x90, 0x51, 0x7F);
    EXPECT_DOUBLE_EQ(0.5, loopScale.get());

    receivedShortMessage(0x90, 0x53, 0x7F);
    EXPECT_DOUBLE_EQ(2.0, loopScale.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PushButtonCO_PushOnOff) {
    // Most MIDI controller send push-buttons as (NOTE_ON, 0x7F) for press and
    // (NOTE_OFF, 0x00) for release.
    ConfigKey key("[Channel1]", "hotcue_1_activate");
    ControlPushButton cpb(key);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            MidiOptions(),
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PushButtonCO_PushOnOn) {
    // Some MIDI controllers send push-buttons as (NOTE_ON, 0x7f) for press and
    // (NOTE_ON, 0x00) for release.
    ConfigKey key("[Channel1]", "hotcue_1_activate");
    ControlPushButton cpb(key);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PushButtonCO_ToggleOnOff_ButtonMidiOption) {
    // Using the button MIDI option allows you to use a MIDI toggle button as a
    // push button.
    ConfigKey key("[Channel1]", "hotcue_1_activate");
    ControlPushButton cpb(key);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    MidiOptions options;
    options.setFlag(MidiOption::Button);

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            options,
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            options,
            key));
    m_pController->setMapping(m_pMapping);

    // NOTE(rryan): This behavior is broken!

    // Toggle the switch on, sets the push button on.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());

    // The push button is stuck down here!

    // Toggle the switch off, sets the push button off.
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PushButtonCO_ToggleOnOff_SwitchMidiOption) {
    // Using the switch MIDI option interprets a MIDI toggle button as a toggle
    // button rather than a momentary push button.
    ConfigKey key("[Channel1]", "hotcue_1_activate");
    ControlPushButton cpb(key);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    MidiOptions options;
    options.setFlag(MidiOption::Switch);

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            options,
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            options,
            key));
    m_pController->setMapping(m_pMapping);

    // NOTE(rryan): This behavior is broken!

    // Toggle the switch on, sets the push button on.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());

    // The push button is stuck down here!

    // Toggle the switch off, sets the push button on again.
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_LT(0.0, cpb.get());

    // NOTE(rryan): What is supposed to happen in this case? It's an open
    // question I think. I think if you want to connect a switch MIDI control to
    // a push button CO then the switch should directly set the CO. After all,
    // the mapping author asked for the switch to be interpreted as a switch. If
    // they want the switch to act like a push button, they should use the
    // button MIDI option.
    //
    // Most of our push buttons trigger behavior on press and do nothing on
    // release, and most don't care about being "stuck down" except for hotcue
    // and cue controls that have preview behavior.

    // "reverse" is an example of a push button that is a push button because we
    // want the default behavior to be momentary press and not toggle. If I
    // mapped a switch to it, I would expect the switch to enable it (set it to
    // 1) when the switch was enabled and set it to 0 when the switch was
    // disabled. So I think we should change the switch option to behave like
    // this.
}

TEST_F(MidiControllerTest, ReceiveMessage_PushButtonCO_PushCC) {
    // Some MIDI controllers (e.g. Korg nanoKONTROL) send momentary push-buttons
    // as (CC, 0x7f) for press and (CC, 0x00) for release.
    ConfigKey key("[Channel1]", "hotcue_1_activate");
    ControlPushButton cpb(key);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::ControlChange, channel),
                    control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());

    // Receive an on/off, sets the control on/off with each press.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_ToggleCO_PushOnOff) {
    // Most MIDI controller send push-buttons as (NOTE_ON, 0x7F) for press and
    // (NOTE_OFF, 0x00) for release.
    ConfigKey key("[Channel1]", "keylock");
    ControlPushButton cpb(key);
    cpb.setButtonMode(mixxx::control::ButtonMode::Toggle);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            MidiOptions(),
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);

    EXPECT_LT(0.0, cpb.get());

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);

    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_ToggleCO_PushOnOn) {
    // Some MIDI controllers send push-buttons as (NOTE_ON, 0x7f) for press and
    // (NOTE_ON, 0x00) for release.
    ConfigKey key("[Channel1]", "keylock");
    ControlPushButton cpb(key);
    cpb.setButtonMode(mixxx::control::ButtonMode::Toggle);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x00);

    EXPECT_LT(0.0, cpb.get());

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x00);

    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_ToggleCO_ToggleOnOff_ButtonMidiOption) {
    // Using the button MIDI option allows you to use a MIDI toggle button as a
    // push button.
    ConfigKey key("[Channel1]", "keylock");
    ControlPushButton cpb(key);
    cpb.setButtonMode(mixxx::control::ButtonMode::Toggle);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    MidiOptions options;
    options.setFlag(MidiOption::Button);

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            options,
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            options,
            key));
    m_pController->setMapping(m_pMapping);

    // NOTE(rryan): If the intended behavior of the button MIDI option is to
    // make a toggle MIDI button act like a push button then this isn't
    // working. The toggle on toggles the CO but the toggle off does nothing.

    // Toggle the switch on, since it is interpreted as a button press it
    // toggles the button on.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());

    // Toggle the switch off, since it is interpreted as a button release it
    // does nothing to the toggle button.
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_LT(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_ToggleCO_ToggleOnOff_SwitchMidiOption) {
    // Using the switch MIDI option interprets a MIDI toggle button as a toggle
    // button rather than a momentary push button.
    ConfigKey key("[Channel1]", "keylock");
    ControlPushButton cpb(key);
    cpb.setButtonMode(mixxx::control::ButtonMode::Toggle);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    MidiOptions options;
    options.setFlag(MidiOption::Switch);

    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOn, channel),
                                        control),
            options,
            key));
    addMapping(MidiInputMapping(MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                                                MidiOpCode::NoteOff, channel),
                                        control),
            options,
            key));
    m_pController->setMapping(m_pMapping);

    // NOTE(rryan): If the intended behavior of switch MIDI option is to make a
    // toggle MIDI button act like a toggle button then this isn't working. The
    // toggle on presses the CO and the toggle off presses the CO. This toggles
    // the control but allows it to get out of sync.

    // Toggle the switch on, since it is interpreted as a button press it
    // toggles the control on.
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_LT(0.0, cpb.get());

    // Toggle the switch off, since it is interpreted as a button press it
    // toggles the control off.
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());

    // Meanwhile, the GUI toggles the control on again.
    // NOTE(rryan): Now the MIDI toggle button is out of sync with the toggle
    // CO.
    cpb.set(1.0);

    // Toggle the switch on, since it is interpreted as a button press it
    // toggles the control off (since it was on).
    receivedShortMessage(MidiOpCode::NoteOn, channel, control, 0x7F);
    EXPECT_DOUBLE_EQ(0.0, cpb.get());

    // Toggle the switch off, since it is interpreted as a button press it
    // toggles the control on (since it was off).
    receivedShortMessage(MidiOpCode::NoteOff, channel, control, 0x00);
    EXPECT_LT(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_ToggleCO_PushCC) {
    // Some MIDI controllers (e.g. Korg nanoKONTROL) send momentary push-buttons
    // as (CC, 0x7f) for press and (CC, 0x00) for release.
    ConfigKey key("[Channel1]", "keylock");
    ControlPushButton cpb(key);
    cpb.setButtonMode(mixxx::control::ButtonMode::Toggle);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::ControlChange, channel),
                    control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x00);

    EXPECT_LT(0.0, cpb.get());

    // Receive an on/off, toggles the control.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x7F);
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x00);

    EXPECT_DOUBLE_EQ(0.0, cpb.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PotMeterCO_7BitCC) {
    ConfigKey key("[Channel1]", "playposition");

    constexpr double kMinValue = -1234.5;
    constexpr double kMaxValue = 678.9;
    constexpr double kMiddleValue = (kMinValue + kMaxValue) * 0.5;
    ControlPotmeter potmeter(key, kMinValue, kMaxValue);

    unsigned char channel = 0x01;
    unsigned char control = 0x10;

    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::ControlChange, channel),
                    control),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive a 0, MIDI parameter should map to the min value.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x00);
    EXPECT_DOUBLE_EQ(kMinValue, potmeter.get());

    // Receive a 0x7F, MIDI parameter should map to the potmeter max value.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x7F);
    EXPECT_DOUBLE_EQ(kMaxValue, potmeter.get());

    // Receive a 0x40, MIDI parameter should map to the potmeter middle value.
    receivedShortMessage(MidiOpCode::ControlChange, channel, control, 0x40);
    EXPECT_DOUBLE_EQ(kMiddleValue, potmeter.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PotMeterCO_14BitCC) {
    ConfigKey key("[Channel1]", "playposition");

    constexpr double kMinValue = -1234.5;
    constexpr double kMaxValue = 678.9;
    constexpr double kMiddleValue = (kMinValue + kMaxValue) * 0.5;
    ControlPotmeter potmeter(key, kMinValue, kMaxValue);
    potmeter.set(0);

    unsigned char channel = 0x01;
    unsigned char lsb_control = 0x10;
    unsigned char msb_control = 0x11;

    MidiOptions lsb;
    lsb.setFlag(MidiOption::FourteenBitLSB);

    MidiOptions msb;
    msb.setFlag(MidiOption::FourteenBitMSB);

    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::ControlChange, channel),
                    lsb_control),
            lsb,
            key));
    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::ControlChange, channel),
                    msb_control),
            msb,
            key));
    m_pController->setMapping(m_pMapping);

    // If kMinValue or kMaxValue are such that the middle value is 0 then the
    // set(0) commands below allow us to hide failures.
    ASSERT_NE(0.0, kMiddleValue);

    // Receive a 0x0000 (lsb-first), MIDI parameter should map to the min value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x00);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x00);
    EXPECT_DOUBLE_EQ(kMinValue, potmeter.get());

    // Receive a 0x0000 (msb-first), MIDI parameter should map to the min value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x00);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x00);
    EXPECT_DOUBLE_EQ(kMinValue, potmeter.get());

    // Receive a 0x3FFF (lsb-first), MIDI parameter should map to the max value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x7F);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x7F);
    EXPECT_DOUBLE_EQ(kMaxValue, potmeter.get());

    // Receive a 0x3FFF (msb-first), MIDI parameter should map to the max value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x7F);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x7F);
    EXPECT_DOUBLE_EQ(kMaxValue, potmeter.get());

    // Receive a 0x2000 (lsb-first), MIDI parameter should map to the middle
    // value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x00);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x40);
    EXPECT_DOUBLE_EQ(kMiddleValue, potmeter.get());

    // Receive a 0x2000 (msb-first), MIDI parameter should map to the middle
    // value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x40);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x00);
    EXPECT_DOUBLE_EQ(kMiddleValue, potmeter.get());

    // Check the 14-bit resolution is actually present. Receive a 0x2001
    // (msb-first), MIDI parameter should map to the middle value plus a tiny
    // amount. Scaling is not quite linear for MIDI parameters so just check
    // that incrementing the LSB by 1 is greater than the middle value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x40);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x01);
    EXPECT_LT(kMiddleValue, potmeter.get());

    // Check the 14-bit resolution is actually present. Receive a 0x2001
    // (lsb-first), MIDI parameter should map to the middle value plus a tiny
    // amount. Scaling is not quite linear for MIDI parameters so just check
    // that incrementing the LSB by 1 is greater than the middle value.
    potmeter.set(0);
    receivedShortMessage(MidiOpCode::ControlChange, channel, lsb_control, 0x01);
    receivedShortMessage(MidiOpCode::ControlChange, channel, msb_control, 0x40);
    EXPECT_LT(kMiddleValue, potmeter.get());
}

TEST_F(MidiControllerTest, ReceiveMessage_PotMeterCO_14BitPitchBend) {
    ConfigKey key("[Channel1]", "rate");

    constexpr double kMinValue = -1234.5;
    constexpr double kMaxValue = 678.9;
    constexpr double kMiddleValue = (kMinValue + kMaxValue) * 0.5;
    ControlPotmeter potmeter(key, kMinValue, kMaxValue);
    unsigned char channel = 0x01;

    // The control is ignored in mappings for messages where the control is part
    // of the payload.
    addMapping(MidiInputMapping(
            MidiKey(MidiUtils::statusFromOpCodeAndChannel(
                            MidiOpCode::PitchBendChange, channel),
                    0xFF),
            MidiOptions(),
            key));
    m_pController->setMapping(m_pMapping);

    // Receive a 0x0000, MIDI parameter should map to the min value.
    receivedShortMessage(MidiOpCode::PitchBendChange, channel, 0x00, 0x00);
    EXPECT_DOUBLE_EQ(kMinValue, potmeter.get());

    // Receive a 0x3FFF, MIDI parameter should map to the potmeter max value.
    receivedShortMessage(MidiOpCode::PitchBendChange, channel, 0x7F, 0x7F);
    EXPECT_DOUBLE_EQ(kMaxValue, potmeter.get());

    // Receive a 0x2000, MIDI parameter should map to the potmeter middle value.
    receivedShortMessage(MidiOpCode::PitchBendChange, channel, 0x00, 0x40);
    EXPECT_DOUBLE_EQ(kMiddleValue, potmeter.get());

    // Check the 14-bit resolution is actually present. Receive a 0x2001, MIDI
    // parameter should map to the middle value plus a tiny amount. Scaling is
    // not quite linear for MIDI parameters so just check that incrementing the
    // LSB by 1 is greater than the middle value.
    receivedShortMessage(MidiOpCode::PitchBendChange, channel, 0x01, 0x40);
    EXPECT_LT(kMiddleValue, potmeter.get());
}

TEST_F(MidiControllerTest, JSInputHandler_BindHandler) {
    constexpr double kMinValue = -1234.5;
    constexpr double kMaxValue = 678.9;
    ControlPotmeter potmeter(ConfigKey("[Channel1]", "test_pot"), kMinValue, kMaxValue);
    m_pController->setMapping(m_pMapping);
    EXPECT_EQ(getInputMappingCount(), 0);
    evaluateAndAssert(
            "midi.makeInputHandler(0x90, 0x43, (channel, control, value, status) => {"
            "engine.setParameter('[Channel1]', 'test_pot', value);"
            "})");
    EXPECT_EQ(getInputMappingCount(), 1);
    receivedShortMessage(0x90, 0x43, 0x00);
    EXPECT_DOUBLE_EQ(potmeter.get(), kMinValue);
    receivedShortMessage(0x90, 0x43, 0x7F);
    EXPECT_DOUBLE_EQ(potmeter.get(), kMaxValue);
}

TEST_F(MidiControllerTest, JSInputHandler_ControllerShutdownSlot) {
    m_pController->setMapping(m_pMapping);
    EXPECT_EQ(getInputMappingCount(), 0);
    evaluateAndAssert(
            "midi.makeInputHandler(0x90, 0x43, (channel, control, value, status) => {})");
    EXPECT_EQ(getInputMappingCount(), 1);
    shutdownController();
    EXPECT_EQ(getInputMappingCount(), 0);
}

TEST_F(MidiControllerTest, JSInputHandler_ErrorWhenControlIsTooLarge) {
    m_pController->setMapping(m_pMapping);
    EXPECT_EQ(getInputMappingCount(), 0);
    bool isError = evaluateAndAssert(
            "midi.makeInputHandler(0x90, 0x80, (channel, control, value, status) => {})");
    ASSERT_TRUE(isError);
    EXPECT_EQ(getInputMappingCount(), 0);
}

TEST_F(MidiControllerTest, JSInputHandler_ErrorWhenStatusIsTooSmall) {
    m_pController->setMapping(m_pMapping);
    EXPECT_EQ(getInputMappingCount(), 0);
    bool isError = evaluateAndAssert(
            "midi.makeInputHandler(0x7F, 0x00, (channel, control, value, status) => {})");
    ASSERT_TRUE(isError);
    EXPECT_EQ(getInputMappingCount(), 0);
}
