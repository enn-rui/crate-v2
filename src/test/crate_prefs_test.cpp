#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>

#include "crate/prefs/dlgprefcrate.h"
#include "test/mixxxtest.h"

class CratePrefsTest : public MixxxTest {};

TEST_F(CratePrefsTest, ApplyAndReloadRoundTrip) {
    DlgPrefCrate page(nullptr, config());
    page.findChild<QLineEdit*>(QStringLiteral("sidecarDir"))->setText(QStringLiteral("D:/sidecars"));
    page.findChild<QLineEdit*>(QStringLiteral("musicRoot"))->setText(QStringLiteral("D:/music"));
    page.findChild<QLineEdit*>(QStringLiteral("grabServiceUrl"))->setText(QStringLiteral("https://grab.test"));
    page.findChild<QLineEdit*>(QStringLiteral("grabServiceToken"))->setText(QStringLiteral("secret"));
    page.findChild<QCheckBox*>(QStringLiteral("autoAnalyze"))->setChecked(false);
    page.findChild<QSpinBox*>(QStringLiteral("analyzerThreads"))->setValue(7);
    page.findChild<QCheckBox*>(QStringLiteral("galaxyTrail"))->setChecked(false);
    page.findChild<QCheckBox*>(QStringLiteral("galaxyHalos"))->setChecked(true);
    page.slotApply();

    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "sidecar_dir"), QString()), QStringLiteral("D:/sidecars"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "music_root"), QString()), QStringLiteral("D:/music"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "grab_service_url"), QString()), QStringLiteral("https://grab.test"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "grab_service_token"), QString()), QStringLiteral("secret"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "auto_analyze"), 1), 0);
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "analyzer_threads"), 0), 7);
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "galaxy_trail"), 1), 0);
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "galaxy_halos"), 0), 1);

    saveAndReloadConfig();
    DlgPrefCrate reloadedPage(nullptr, config());
    EXPECT_EQ(reloadedPage.findChild<QLineEdit*>(QStringLiteral("sidecarDir"))->text(), QStringLiteral("D:/sidecars"));
    EXPECT_EQ(reloadedPage.findChild<QLineEdit*>(QStringLiteral("grabServiceToken"))->text(), QStringLiteral("secret"));
    EXPECT_EQ(reloadedPage.findChild<QSpinBox*>(QStringLiteral("analyzerThreads"))->value(), 7);
    EXPECT_FALSE(reloadedPage.findChild<QCheckBox*>(QStringLiteral("autoAnalyze"))->isChecked());
}

TEST_F(CratePrefsTest, AutomaticThreadsRemovesOverride) {
    config()->setValue(ConfigKey("[Crate]", "analyzer_threads"), 12);
    DlgPrefCrate page(nullptr, config());
    page.findChild<QSpinBox*>(QStringLiteral("analyzerThreads"))->setValue(0);
    page.slotApply();
    EXPECT_FALSE(config()->exists(ConfigKey("[Crate]", "analyzer_threads")));
    page.slotUpdate();
    EXPECT_EQ(page.findChild<QSpinBox*>(QStringLiteral("analyzerThreads"))->value(), 0);
}
