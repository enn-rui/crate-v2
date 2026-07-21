#include <gtest/gtest.h>

#include "control/controlobject.h"
#include "controllers/controlpickermenu.h"
#include "preferences/configobject.h"

TEST(ControlPickerMenuTest, ExposesCrateControls) {
    ControlPickerMenu menu(nullptr);
    const QStringList controls{
            QStringLiteral("knob_focus"),
            QStringLiteral("galaxy_move"),
            QStringLiteral("galaxy_load"),
            QStringLiteral("galaxy_3d"),
            QStringLiteral("galaxy_halos"),
            QStringLiteral("galaxy_trail"),
            QStringLiteral("galaxy_layout_control"),
            QStringLiteral("galaxy_color_control"),
            QStringLiteral("galaxy_reload"),
    };
    for (const QString& control : controls) {
        EXPECT_TRUE(menu.controlExists(ConfigKey(QStringLiteral("[Crate]"), control)))
                << control.toStdString();
    }
}

TEST(ControlPickerMenuTest, ExposesPerDeckDownbeatShift) {
    // The picker enumerates per-deck controls up to [App],num_decks.
    ControlObject numDecks(ConfigKey(QStringLiteral("[App]"), QStringLiteral("num_decks")));
    numDecks.set(2);
    ControlPickerMenu menu(nullptr);
    // crate_downbeat_shift is a per-deck ([ChannelN]) push control registered in
    // the Crate submenu so any controller can MIDI-learn it.
    EXPECT_TRUE(menu.controlExists(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("crate_downbeat_shift"))));
    EXPECT_TRUE(menu.controlExists(
            ConfigKey(QStringLiteral("[Channel2]"), QStringLiteral("crate_downbeat_shift"))));
}
