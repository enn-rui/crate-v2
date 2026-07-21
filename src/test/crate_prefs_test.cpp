#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "crate/prefs/dlgprefcrate.h"
#include "control/controlobject.h"
#include "crate/tempo/tempocheck.h"
#include "test/mixxxtest.h"

class CratePrefsTest : public MixxxTest {};

TEST_F(CratePrefsTest, AnalysisLauncherControlsExist) {
    DlgPrefCrate page(nullptr, config());
    ASSERT_NE(page.findChild<QPushButton*>(QStringLiteral("analyzeLibrary")), nullptr);
    auto* status = page.findChild<QLabel*>(QStringLiteral("analysisStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().isEmpty());
    EXPECT_NE(page.findChild<QPushButton*>(QStringLiteral("checkTempos")), nullptr);
}

TEST(CrateTempoCheckTest, CleanDoubleErrorWithBothSignalsIsFixed) {
    crate::TempoProbe probe;
    probe.bpm = 240.0;
    probe.sidecarBpm = 120.0;
    probe.clusterMedianBpm = 121.0;
    const auto result = crate::evaluateTempo(probe);
    EXPECT_EQ(result.verdict, crate::TempoVerdict::FixHalve);
    EXPECT_DOUBLE_EQ(result.newBpm, 120.0);
}

TEST(CrateTempoCheckTest, CleanHalfErrorWithSingleSignalIsFixed) {
    crate::TempoProbe probe;
    probe.bpm = 60.0;
    probe.sidecarBpm = 120.0;
    EXPECT_EQ(crate::evaluateTempo(probe).verdict,
            crate::TempoVerdict::FixDouble);
}

TEST(CrateTempoCheckTest, ConflictingAndNonOctaveSignalsAreSuspects) {
    crate::TempoProbe probe;
    probe.bpm = 180.0;
    probe.sidecarBpm = 120.0;
    probe.clusterMedianBpm = 90.0;
    EXPECT_EQ(crate::evaluateTempo(probe).verdict,
            crate::TempoVerdict::Suspect);
    probe.bpm = 240.0;
    EXPECT_EQ(crate::evaluateTempo(probe).verdict,
            crate::TempoVerdict::Suspect);
}

TEST(CrateTempoCheckTest, ConsistentTempoIsIdempotentAndNoReferenceSkips) {
    crate::TempoProbe probe;
    probe.bpm = 120.0;
    probe.sidecarBpm = 120.0;
    probe.clusterMedianBpm = 121.0;
    EXPECT_EQ(crate::evaluateTempo(probe).verdict,
            crate::TempoVerdict::Consistent);
    probe.sidecarBpm.reset();
    probe.clusterMedianBpm.reset();
    EXPECT_EQ(crate::evaluateTempo(probe).verdict,
            crate::TempoVerdict::Consistent);
}

TEST_F(CratePrefsTest, ApplyAndReloadRoundTrip) {
    ControlObject trailControl(ConfigKey("[Crate]", "galaxy_trail"));
    ControlObject haloControl(ConfigKey("[Crate]", "galaxy_halos"));
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
    EXPECT_DOUBLE_EQ(trailControl.get(), 0.0);
    EXPECT_DOUBLE_EQ(haloControl.get(), 1.0);

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

TEST_F(CratePrefsTest, AnalysisDefaultsSidecarWithoutClobbering) {
    DlgPrefCrate page(nullptr, config());
    page.findChild<QLineEdit*>(QStringLiteral("musicRoot"))->setText(QStringLiteral("D:/music"));
    QString musicRoot;
    QString sidecarDir;
    ASSERT_TRUE(page.prepareAnalysisDirectories(&musicRoot, &sidecarDir));
    EXPECT_EQ(musicRoot, QStringLiteral("D:/music"));
    EXPECT_EQ(sidecarDir, QStringLiteral("D:/music/.crate"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "sidecar_dir"), QString()),
            QStringLiteral("D:/music/.crate"));
}

TEST_F(CratePrefsTest, AnalysisPreservesConfiguredSidecar) {
    config()->setValue(ConfigKey("[Crate]", "sidecar_dir"), QStringLiteral("E:/sidecars"));
    DlgPrefCrate page(nullptr, config());
    page.findChild<QLineEdit*>(QStringLiteral("musicRoot"))->setText(QStringLiteral("D:/music"));
    QString musicRoot;
    QString sidecarDir;
    ASSERT_TRUE(page.prepareAnalysisDirectories(&musicRoot, &sidecarDir));
    EXPECT_EQ(sidecarDir, QStringLiteral("E:/sidecars"));
    EXPECT_EQ(config()->getValue(ConfigKey("[Crate]", "sidecar_dir"), QString()),
            QStringLiteral("E:/sidecars"));
}
