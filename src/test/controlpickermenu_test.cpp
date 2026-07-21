#include <gtest/gtest.h>

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
