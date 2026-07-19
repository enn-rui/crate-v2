// Widget-level interaction tests for the galaxy chip bar: real synthesized
// mouse clicks on the buttons must switch modes. This exists because the
// interactive path shipped broken once while every mode worked via config keys
// — config-driven verification cannot catch dead controls.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "crate/galaxy/wcrategalaxy.h"
#include "preferences/usersettings.h"

namespace {

QString goldenSidecarDir() {
    // src/test/... -> repo root/golden/fixture_lib/.crate
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp(); // test
    dir.cdUp(); // src
    dir.cdUp(); // repo root
    return dir.filePath(QStringLiteral("golden/fixture_lib/.crate"));
}

class CrateGalaxyUiTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmp.isValid());
        m_pConfig = UserSettingsPointer(
                new UserSettings(m_tmp.filePath(QStringLiteral("test.cfg"))));
        m_pConfig->setValue(ConfigKey("[Crate]", "sidecar_dir"), goldenSidecarDir());
        m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(
                nullptr, /*pPlayerManager=*/nullptr, m_pConfig);
        m_pGalaxy->resize(900, 700);
        m_pGalaxy->show();
        QApplication::processEvents();
    }

    QPushButton* chip(const QString& text) {
        const auto buttons = m_pGalaxy->findChildren<QPushButton*>();
        for (QPushButton* pButton : buttons) {
            if (pButton->text() == text) {
                return pButton;
            }
        }
        return nullptr;
    }

    QString configValue(const char* key) {
        return m_pConfig->getValue(ConfigKey("[Crate]", key), QString());
    }

    QTemporaryDir m_tmp;
    UserSettingsPointer m_pConfig;
    std::unique_ptr<crate::WCrateGalaxy> m_pGalaxy;
};

TEST_F(CrateGalaxyUiTest, ChipBarExistsWithAllChips) {
    for (const char* text : {"SCATTER", "WHEEL", "BPM", "ARTIST",
                 "CLUSTER", "KEY", "TEMPO", "ENERGY", "3D", "HALO"}) {
        EXPECT_NE(chip(QString::fromLatin1(text)), nullptr) << text;
    }
}

TEST_F(CrateGalaxyUiTest, ClickingColorChipsSwitchesMode) {
    QPushButton* pKey = chip(QStringLiteral("KEY"));
    ASSERT_NE(pKey, nullptr);
    QTest::mouseClick(pKey, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_color_mode"), QStringLiteral("key"));
    EXPECT_TRUE(pKey->isChecked());

    QPushButton* pTempo = chip(QStringLiteral("TEMPO"));
    ASSERT_NE(pTempo, nullptr);
    QTest::mouseClick(pTempo, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_color_mode"), QStringLiteral("tempo"));

    QPushButton* pCluster = chip(QStringLiteral("CLUSTER"));
    ASSERT_NE(pCluster, nullptr);
    QTest::mouseClick(pCluster, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_color_mode"), QStringLiteral("cluster"));
}

TEST_F(CrateGalaxyUiTest, ClickingLayoutChipsSwitchesLayout) {
    QPushButton* pWheel = chip(QStringLiteral("WHEEL"));
    ASSERT_NE(pWheel, nullptr);
    QTest::mouseClick(pWheel, Qt::LeftButton);
    QApplication::processEvents();
    // NOTE: the wheel layout persists as "key" (naming quirk, matches the setter).
    EXPECT_EQ(configValue("galaxy_layout"), QStringLiteral("key"));

    QPushButton* pScatter = chip(QStringLiteral("SCATTER"));
    ASSERT_NE(pScatter, nullptr);
    QTest::mouseClick(pScatter, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_layout"), QStringLiteral("scatter"));
}

TEST_F(CrateGalaxyUiTest, Clicking3dTogglesProjection) {
    QPushButton* p3d = chip(QStringLiteral("3D"));
    ASSERT_NE(p3d, nullptr);
    QTest::mouseClick(p3d, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_3d"), QStringLiteral("1"));
    QTest::mouseClick(p3d, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_3d"), QStringLiteral("0"));
}

} // namespace
