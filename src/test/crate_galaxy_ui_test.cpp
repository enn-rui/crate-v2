// Widget-level interaction tests for the sidebar MAP controls: real synthesized
// input on the controls must switch galaxy state. This exists because the
// interactive path shipped broken once while every mode worked via config keys
// — config-driven verification cannot catch dead controls.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <memory>

#include "control/controlencoder.h"
#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "crate/galaxy/wcrategalaxy.h"
#include "crate/galaxy/wcratemapcontrols.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "preferences/usersettings.h"
#include "skin/legacy/legacyskinparser.h"
#include "track/track.h"

namespace {

QString goldenSidecarDir() {
    // src/test/... -> repo root/golden/fixture_lib/.crate
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp(); // test
    dir.cdUp(); // src
    dir.cdUp(); // repo root
    return dir.filePath(QStringLiteral("golden/fixture_lib/.crate"));
}

QString crateSkinDir() {
    QDir dir = QFileInfo(QStringLiteral(__FILE__)).dir(); // test
    dir.cdUp(); // src
    dir.cdUp(); // repo root
    return dir.filePath(QStringLiteral("res/skins/Crate"));
}

class CrateGalaxyUiTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmp.isValid());
        m_pConfig = UserSettingsPointer(
                new UserSettings(m_tmp.filePath(QStringLiteral("test.cfg"))));
        m_pConfig->setValue(ConfigKey("[Crate]", "sidecar_dir"), goldenSidecarDir());
        // The browse knob drives the stock [Library],MoveVertical encoder
        // (ignoreNops=false, exactly like LibraryControl), so repeated +1 ticks
        // each emit. Create it BEFORE the galaxy so its ControlProxy binds.
        m_pMoveVertical = std::make_unique<ControlEncoder>(
                ConfigKey("[Library]", "MoveVertical"), false);
        m_pControls = std::make_unique<crate::WCrateMapControls>(nullptr, m_pConfig);
        m_pControls->show();
        m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(
                nullptr, /*pPlayerManager=*/nullptr, m_pConfig);
        m_pGalaxy->resize(900, 700);
        m_pGalaxy->show();
        QApplication::processEvents();
    }

    // Independently recompute the walk's forward metric: nearest node to `from`
    // in the displayed 2D space, excluding self, visited, and hidden nodes.
    int expectedNearest(int from, const QSet<int>& visited) const {
        int best = -1;
        double bestDistance = 0.0;
        for (int i = 0; i < m_pGalaxy->testNodeCount(); ++i) {
            if (i == from || visited.contains(i) || !m_pGalaxy->testNodeVisible(i)) {
                continue;
            }
            const QPointF delta = m_pGalaxy->testNodeDisplayPos(i) -
                    m_pGalaxy->testNodeDisplayPos(from);
            const double distance = QPointF::dotProduct(delta, delta);
            if (best < 0 || distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    void pokeKnob(double delta) {
        ControlObject::set(ConfigKey("[Library]", "MoveVertical"), delta);
        QApplication::processEvents();
    }

    void setKnobFocusMap() {
        QPushButton* pKnob = m_pControls->findChild<QPushButton*>(
                QStringLiteral("CrateMapKnob"));
        ASSERT_NE(pKnob, nullptr);
        if (!pKnob->isChecked()) {
            QTest::mouseClick(pKnob, Qt::LeftButton);
            QApplication::processEvents();
        }
    }

    QString configValue(const char* key) {
        return m_pConfig->getValue(ConfigKey("[Crate]", key), QString());
    }

    void recreateGalaxy(bool mode3d, double debugZoom, bool dense = false) {
        m_pGalaxy.reset();
        const QString sidecarDir = m_tmp.filePath(QStringLiteral("sidecars"));
        ASSERT_TRUE(QDir().mkpath(sidecarDir));
        const QDir source(goldenSidecarDir());
        for (const QString& name : source.entryList(QDir::Files)) {
            const QString destination = QDir(sidecarDir).filePath(name);
            if (!QFile::exists(destination)) {
                ASSERT_TRUE(QFile::copy(source.filePath(name), destination));
            }
        }
        const QString connectionName = QStringLiteral("CrateGalaxyUiTest3d");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(QDir(sidecarDir).filePath(QStringLiteral("umap.sqlite")));
            ASSERT_TRUE(db.open());
            QSqlQuery query(db);
            ASSERT_TRUE(query.exec(QStringLiteral("DROP TABLE IF EXISTS coords3d")));
            ASSERT_TRUE(query.exec(QStringLiteral(
                    "CREATE TABLE coords3d AS SELECT relpath, x, y, "
                    "((rowid % 7) - 3) * 0.1 AS z FROM coords")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        m_pConfig->setValue(ConfigKey("[Crate]", "sidecar_dir"), sidecarDir);
        m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_3d"), mode3d ? 1 : 0);
        m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_test_zoom"), debugZoom);
        m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(
                nullptr, /*pPlayerManager=*/nullptr, m_pConfig);
        m_pGalaxy->resize(900, 700);
        m_pGalaxy->show();
        QApplication::processEvents();
        if (dense) {
            m_pGalaxy->testSetAllNodeDisplayPositions(QPointF(500.0, 500.0));
            m_pGalaxy->centerOn(QPointF(500.0, 500.0));
            QApplication::processEvents();
        }
    }

    int pillCount() const {
        int count = 0;
        for (QGraphicsItem* pItem : m_pGalaxy->scene()->items()) {
            if (qFuzzyCompare(pItem->zValue(), 10.0)) {
                ++count;
            }
        }
        return count;
    }

    QTemporaryDir m_tmp;
    UserSettingsPointer m_pConfig;
    std::unique_ptr<ControlEncoder> m_pMoveVertical;
    std::unique_ptr<crate::WCrateMapControls> m_pControls;
    std::unique_ptr<crate::WCrateGalaxy> m_pGalaxy;
};

TEST_F(CrateGalaxyUiTest, LayoutComboChangesGalaxyForEveryChoice) {
    auto* pCombo = m_pControls->findChild<QComboBox*>(QStringLiteral("CrateMapLayout"));
    ASSERT_NE(pCombo, nullptr);
    const QStringList expected{QStringLiteral("scatter"), QStringLiteral("key"),
            QStringLiteral("bpm"), QStringLiteral("artist")};
    for (int i = 0; i < expected.size(); ++i) {
        QTest::keyClick(pCombo, Qt::Key_Home);
        for (int step = 0; step < i; ++step) {
            QTest::keyClick(pCombo, Qt::Key_Down);
        }
        QApplication::processEvents();
        EXPECT_EQ(m_pGalaxy->testLayoutMode(), expected[i]);
        if (i > 0) {
            EXPECT_EQ(configValue("galaxy_layout"), expected[i]);
        }
    }
    QTest::keyClick(pCombo, Qt::Key_Home);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_layout"), QStringLiteral("scatter"));
}

TEST(CrateGalaxyPalette, DefaultsRemainTerminalAndCustomColorsApply) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    auto pConfig = UserSettingsPointer(
            new UserSettings(tmp.filePath(QStringLiteral("palette.cfg"))));

    crate::WCrateGalaxy defaults(nullptr, nullptr, pConfig);
    EXPECT_EQ(defaults.testPalette().ground, QColor(QStringLiteral("#05060a")));
    EXPECT_EQ(defaults.testPalette().ink, QColor(QStringLiteral("#f4f7fb")));
    EXPECT_EQ(defaults.testPalette().accentDeckA, QColor(QStringLiteral("#b4d2ff")));
    EXPECT_EQ(defaults.testPalette().accentDeckB, QColor(QStringLiteral("#ffb454")));
    EXPECT_EQ(defaults.backgroundBrush().color(), QColor(QStringLiteral("#05060a")));

    crate::GalaxyPalette palette;
    palette.ground = QColor(QStringLiteral("#fdf3f8"));
    palette.ink = QColor(QStringLiteral("#2c3357"));
    palette.accentDeckA = QColor(QStringLiteral("#5bcefa"));
    palette.accentDeckB = QColor(QStringLiteral("#ff66b3"));
    crate::WCrateGalaxy custom(nullptr, nullptr, pConfig, nullptr, palette);
    EXPECT_EQ(custom.testPalette().ground, palette.ground);
    EXPECT_EQ(custom.testPalette().ink, palette.ink);
    EXPECT_EQ(custom.testPalette().accentDeckA, palette.accentDeckA);
    EXPECT_EQ(custom.testPalette().accentDeckB, palette.accentDeckB);
    EXPECT_EQ(custom.backgroundBrush().color(), palette.ground);
}

TEST(CrateSkinSchemes, LegacyParserListsAllThreeInPreferenceOrder) {
    EXPECT_EQ(LegacySkinParser::getSchemeList(crateSkinDir()),
            (QList<QString>{QStringLiteral("Terminal"),
                    QStringLiteral("Trans Pride"),
                    QStringLiteral("Winamp Classic")}));
}

TEST_F(CrateGalaxyUiTest, ColorComboChangesGalaxyForEveryChoice) {
    auto* pCombo = m_pControls->findChild<QComboBox*>(QStringLiteral("CrateMapColor"));
    ASSERT_NE(pCombo, nullptr);
    const QStringList expected{QStringLiteral("cluster"), QStringLiteral("key"),
            QStringLiteral("tempo"), QStringLiteral("energy")};
    for (int i = 0; i < expected.size(); ++i) {
        QTest::keyClick(pCombo, Qt::Key_Home);
        for (int step = 0; step < i; ++step) {
            QTest::keyClick(pCombo, Qt::Key_Down);
        }
        QApplication::processEvents();
        EXPECT_EQ(m_pGalaxy->testColorMode(), expected[i]);
        if (i > 0) {
            EXPECT_EQ(configValue("galaxy_color_mode"), expected[i]);
        }
    }
    QTest::keyClick(pCombo, Qt::Key_Home);
    QApplication::processEvents();
    EXPECT_EQ(configValue("galaxy_color_mode"), QStringLiteral("cluster"));
}

TEST_F(CrateGalaxyUiTest, ColorModePersistsAcrossReconstruction) {
    auto* pCombo = m_pControls->findChild<QComboBox*>(QStringLiteral("CrateMapColor"));
    ASSERT_NE(pCombo, nullptr);
    pCombo->setCurrentIndex(2);
    QApplication::processEvents();
    ASSERT_EQ(configValue("galaxy_color_mode"), QStringLiteral("tempo"));

    m_pGalaxy.reset();
    m_pControls.reset();
    m_pControls = std::make_unique<crate::WCrateMapControls>(nullptr, m_pConfig);
    m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(nullptr, nullptr, m_pConfig);
    QApplication::processEvents();

    EXPECT_EQ(m_pGalaxy->testColorMode(), QStringLiteral("tempo"));
    EXPECT_EQ(m_pControls->findChild<QComboBox*>(
                      QStringLiteral("CrateMapColor"))->currentIndex(),
            2);
}

TEST_F(CrateGalaxyUiTest, LayoutPickWhile3dExits3dAndAppliesLayout) {
    QPushButton* p3d = m_pControls->findChild<QPushButton*>(QStringLiteral("CrateMap3d"));
    QComboBox* pLayout = m_pControls->findChild<QComboBox*>(
            QStringLiteral("CrateMapLayout"));
    ASSERT_NE(p3d, nullptr);
    ASSERT_NE(pLayout, nullptr);
    QTest::mouseClick(p3d, Qt::LeftButton);
    QApplication::processEvents();
    ASSERT_TRUE(m_pGalaxy->test3dMode());
    ASSERT_TRUE(pLayout->isEnabled());

    pLayout->setCurrentIndex(1);
    QApplication::processEvents();

    EXPECT_FALSE(m_pGalaxy->test3dMode());
    EXPECT_EQ(m_pGalaxy->testLayoutMode(), QStringLiteral("key"));
    EXPECT_EQ(configValue("galaxy_3d"), QStringLiteral("0"));
    EXPECT_EQ(configValue("galaxy_layout"), QStringLiteral("key"));
}

TEST_F(CrateGalaxyUiTest, ArtistLayoutReportsMissingPositions) {
    QComboBox* pLayout = m_pControls->findChild<QComboBox*>(
            QStringLiteral("CrateMapLayout"));
    QLabel* pStatus = m_pControls->findChild<QLabel*>(
            QStringLiteral("CrateMapLayoutStatus"));
    ASSERT_NE(pLayout, nullptr);
    ASSERT_NE(pStatus, nullptr);
    pLayout->setCurrentIndex(3);
    QApplication::processEvents();
    EXPECT_TRUE(pStatus->isVisible());
    EXPECT_EQ(pStatus->text(),
            QStringLiteral("artist positions missing for %1 tracks")
                    .arg(m_pGalaxy->testNodeCount()));

    pLayout->setCurrentIndex(0);
    QApplication::processEvents();
    EXPECT_FALSE(pStatus->isVisible());
    EXPECT_TRUE(pStatus->text().isEmpty());
}

TEST_F(CrateGalaxyUiTest, Clicking3dTogglesGalaxyBothWays) {
    QPushButton* p3d = m_pControls->findChild<QPushButton*>(QStringLiteral("CrateMap3d"));
    ASSERT_NE(p3d, nullptr);
    QTest::mouseClick(p3d, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_TRUE(m_pGalaxy->test3dMode());
    EXPECT_EQ(configValue("galaxy_3d"), QStringLiteral("1"));
    QTest::mouseClick(p3d, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_FALSE(m_pGalaxy->test3dMode());
    EXPECT_EQ(configValue("galaxy_3d"), QStringLiteral("0"));
}

TEST_F(CrateGalaxyUiTest, ClickingHaloTogglesGalaxyBothWays) {
    QPushButton* pHalo = m_pControls->findChild<QPushButton*>(QStringLiteral("CrateMapHalo"));
    ASSERT_NE(pHalo, nullptr);
    EXPECT_TRUE(m_pGalaxy->testHalosEnabled());
    QTest::mouseClick(pHalo, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_FALSE(m_pGalaxy->testHalosEnabled());
    QTest::mouseClick(pHalo, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_TRUE(m_pGalaxy->testHalosEnabled());
}

TEST_F(CrateGalaxyUiTest, TrailStartsEmptyAndConsecutiveDeduplicatesPlayback) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 2);
    EXPECT_TRUE(m_pGalaxy->testPlayTrail().isEmpty());
    const QString first = m_pGalaxy->testNodeRelpath(0);
    const QString second = m_pGalaxy->testNodeRelpath(1);
    const QString third = m_pGalaxy->testNodeRelpath(2);
    for (const QString& relpath : {first, second, second, third}) {
        PlayerInfo::instance().currentPlayingTrackChanged(
                Track::newTemporary(QDir(QStringLiteral("Z:/music")).filePath(relpath)));
    }
    EXPECT_EQ(m_pGalaxy->testPlayTrail(), QVector<int>({0, 1, 2}));
    EXPECT_EQ(m_pGalaxy->testTrailSegmentCount(), 2);
}

TEST_F(CrateGalaxyUiTest, TrailToggleClearsOnlyPaintCache) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 1);
    m_pGalaxy->testPlayingTrackChanged(m_pGalaxy->testNodeRelpath(0));
    m_pGalaxy->testPlayingTrackChanged(m_pGalaxy->testNodeRelpath(1));
    ASSERT_EQ(m_pGalaxy->testTrailSegmentCount(), 1);
    QPushButton* pTrail = m_pControls->findChild<QPushButton*>(QStringLiteral("CrateMapTrail"));
    ASSERT_NE(pTrail, nullptr);
    EXPECT_EQ(pTrail->text(), QStringLiteral("TRAIL"));
    QTest::mouseClick(pTrail, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_FALSE(m_pGalaxy->testTrailEnabled());
    EXPECT_EQ(m_pGalaxy->testTrailSegmentCount(), 0);
    EXPECT_EQ(m_pGalaxy->testPlayTrail().size(), 2);
}

TEST_F(CrateGalaxyUiTest, PlexusUsesPerDeckTopFortyAndClearsStoppedDeck) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 2);
    ControlObject deck1Play(ConfigKey("[Channel1]", "play"));
    ControlObject deck2Play(ConfigKey("[Channel2]", "play"));
    ControlObject::set(ConfigKey("[Channel1]", "play"), 1.0);
    ControlObject::set(ConfigKey("[Channel2]", "play"), 1.0);
    PlayerInfo::instance().setTrackInfo(PlayerManager::groupForDeck(0),
            Track::newTemporary(QDir(QStringLiteral("Z:/music")).filePath(
                    m_pGalaxy->testNodeRelpath(0))));
    PlayerInfo::instance().setTrackInfo(PlayerManager::groupForDeck(1),
            Track::newTemporary(QDir(QStringLiteral("Z:/music")).filePath(
                    m_pGalaxy->testNodeRelpath(1))));
    m_pGalaxy->testRefreshPlexus();
    EXPECT_EQ(m_pGalaxy->testPlexusDeckCount(), 2);
    for (int deck = 0; deck < 2; ++deck) {
        const auto scores = m_pGalaxy->testPlexusScores(deck);
        EXPECT_LE(scores.size(), 40);
        for (double score : scores) {
            EXPECT_GE(score, 0.5);
            EXPECT_LE(score, 1.0);
        }
    }
    QPushButton* pPlexus = m_pControls->findChild<QPushButton*>(QStringLiteral("CrateMapHalo"));
    ASSERT_NE(pPlexus, nullptr);
    EXPECT_EQ(pPlexus->text(), QStringLiteral("PLEXUS"));
    QTest::mouseClick(pPlexus, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(m_pGalaxy->testPlexusSegmentCount(), 0);
    QTest::mouseClick(pPlexus, Qt::LeftButton);
    QApplication::processEvents();
    ControlObject::set(ConfigKey("[Channel2]", "play"), 0.0);
    m_pGalaxy->testRefreshPlexus();
    EXPECT_TRUE(m_pGalaxy->testPlexusScores(1).isEmpty());
}

TEST_F(CrateGalaxyUiTest, RestoresAllSidebarStateFromConfig) {
    m_pGalaxy.reset();
    m_pControls.reset();
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_layout"), QStringLiteral("artist"));
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_color_mode"), QStringLiteral("energy"));
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_3d"), 1);
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_halos"), 0);
    m_pConfig->setValue(ConfigKey("[Crate]", "knob_focus"), 1);
    m_pControls = std::make_unique<crate::WCrateMapControls>(nullptr, m_pConfig);
    m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(nullptr, nullptr, m_pConfig);
    QApplication::processEvents();
    EXPECT_EQ(m_pGalaxy->testLayoutMode(), QStringLiteral("artist"));
    EXPECT_EQ(m_pGalaxy->testColorMode(), QStringLiteral("energy"));
    EXPECT_TRUE(m_pGalaxy->test3dMode());
    EXPECT_FALSE(m_pGalaxy->testHalosEnabled());
    EXPECT_TRUE(m_pGalaxy->testKnobFocusMap());
    EXPECT_EQ(m_pControls->findChild<QComboBox*>(QStringLiteral("CrateMapLayout"))->currentIndex(), 3);
    EXPECT_EQ(m_pControls->findChild<QComboBox*>(QStringLiteral("CrateMapColor"))->currentIndex(), 3);
}

TEST_F(CrateGalaxyUiTest, DeepZoomUsesTrackTextAndNoPersistentPills) {
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/4.0, /*dense=*/true);
    ASSERT_GT(m_pGalaxy->testVisibleSelectableNodeCount(), 1);
    EXPECT_EQ(pillCount(), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(0), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(1), 0);
    EXPECT_GT(m_pGalaxy->testLabelCount(2), 0);
}

TEST_F(CrateGalaxyUiTest, ZoomThresholdsSelectMapLabelLayers) {
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/1.0);
    EXPECT_GT(m_pGalaxy->testLabelCount(0), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(1), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(2), 0);
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/2.0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(0), 0);
    EXPECT_GT(m_pGalaxy->testLabelCount(1), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(2), 0);
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/4.0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(0), 0);
    EXPECT_EQ(m_pGalaxy->testLabelCount(1), 0);
    EXPECT_GT(m_pGalaxy->testLabelCount(2), 0);
}

TEST_F(CrateGalaxyUiTest, ZoomedOut3dUsesProjectedClusterLabelsWithoutPills) {
    recreateGalaxy(/*mode3d=*/true, /*debugZoom=*/1.0);
    EXPECT_EQ(pillCount(), 0);
    EXPECT_GT(m_pGalaxy->testLabelCount(0), 0);
}

TEST_F(CrateGalaxyUiTest, ClusterAutoNameUsesFilingArtistsRenameWinsAndNoiseExcluded) {
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/1.0);
    EXPECT_EQ(m_pGalaxy->testClusterName(8), QStringLiteral("CRRDR"));
    EXPECT_TRUE(m_pGalaxy->testClusterName(-1).isEmpty());
    m_pConfig->setValue(ConfigKey("[Crate]", "cluster_name_8"),
            QStringLiteral("Perreo district"));
    m_pGalaxy->testRebuildLabels();
    EXPECT_EQ(m_pGalaxy->testClusterName(8), QStringLiteral("Perreo district"));
    EXPECT_TRUE(m_pGalaxy->testLabelTexts(0).contains(
            QStringLiteral("Perreo district")));
}

TEST_F(CrateGalaxyUiTest, ArtistClumpingEmitsEachMultiTrackRegionOnly) {
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/2.0);
    QVector<int> gabba;
    for (int i = 0; i < m_pGalaxy->testNodeCount(); ++i) {
        if (m_pGalaxy->testNodeArtist(i) == QStringLiteral("DR. GABBA")) {
            gabba.append(i);
        }
    }
    ASSERT_EQ(gabba.size(), 4);
    m_pGalaxy->testSetNodeDisplayPosition(gabba[0], QPointF(700, 900));
    m_pGalaxy->testSetNodeDisplayPosition(gabba[1], QPointF(710, 900));
    m_pGalaxy->testSetNodeDisplayPosition(gabba[2], QPointF(1300, 1100));
    m_pGalaxy->testSetNodeDisplayPosition(gabba[3], QPointF(1310, 1100));
    m_pGalaxy->testRebuildLabels();
    EXPECT_EQ(m_pGalaxy->testLabelTexts(1).count(QStringLiteral("DR. GABBA")), 2);
    EXPECT_FALSE(m_pGalaxy->testLabelTexts(1).contains(
            QStringLiteral("Benji Bassline")));
}

TEST_F(CrateGalaxyUiTest, TrackTextCollisionIsStableAndUsesClusterColor) {
    recreateGalaxy(/*mode3d=*/false, /*debugZoom=*/4.0, /*dense=*/true);
    const QStringList first = m_pGalaxy->testLabelTexts(2);
    ASSERT_FALSE(first.isEmpty());
    m_pGalaxy->testRebuildLabels();
    EXPECT_EQ(m_pGalaxy->testLabelTexts(2), first);
    for (int i = 0; i < m_pGalaxy->testLabelCount(2); ++i) {
        const int clusterId = m_pGalaxy->testLabelClusterId(2, i);
        EXPECT_EQ(m_pGalaxy->testLabelColor(2, i),
                m_pGalaxy->testClusterColor(clusterId));
    }
}

TEST_F(CrateGalaxyUiTest, HoverAndOrbitKeep3dPillOnProjectedDot) {
    recreateGalaxy(/*mode3d=*/true, /*debugZoom=*/1.0);
    QGraphicsItem* pDot = nullptr;
    for (QGraphicsItem* pItem : m_pGalaxy->scene()->items()) {
        if (pItem->isVisible() && pItem->data(0).isValid() &&
                !qFuzzyCompare(pItem->zValue(), 10.0)) {
            pDot = pItem;
            break;
        }
    }
    ASSERT_NE(pDot, nullptr);
    const int index = pDot->data(0).toInt();
    QTest::mouseMove(m_pGalaxy->viewport(), m_pGalaxy->mapFromScene(pDot->pos()));
    QApplication::processEvents();
    ASSERT_EQ(pillCount(), 1);

    QGraphicsItem* pPill = nullptr;
    for (QGraphicsItem* pItem : m_pGalaxy->scene()->items()) {
        if (qFuzzyCompare(pItem->zValue(), 10.0) && pItem->data(0).toInt() == index) {
            pPill = pItem;
            break;
        }
    }
    ASSERT_NE(pPill, nullptr);
    EXPECT_EQ(pPill->pos(), pDot->pos());
    const QPointF oldPosition = pDot->pos();
    const QPoint orbitStart = m_pGalaxy->viewport()->rect().center();
    QTest::mousePress(m_pGalaxy->viewport(), Qt::LeftButton, Qt::NoModifier, orbitStart);
    QTest::mouseMove(m_pGalaxy->viewport(), orbitStart + QPoint(60, 30));
    QTest::mouseRelease(
            m_pGalaxy->viewport(), Qt::LeftButton, Qt::NoModifier, orbitStart + QPoint(60, 30));
    QApplication::processEvents();
    EXPECT_NE(pDot->pos(), oldPosition);
    EXPECT_EQ(pPill->pos(), pDot->pos());
}

TEST_F(CrateGalaxyUiTest, KnobSidebarToggleChangesGalaxyBothWays) {
    QPushButton* pKnob = m_pControls->findChild<QPushButton*>(
            QStringLiteral("CrateMapKnob"));
    ASSERT_NE(pKnob, nullptr);
    // Default TABLE = stock behavior (least surprise).
    EXPECT_FALSE(pKnob->isChecked());
    EXPECT_EQ(pKnob->text(), QStringLiteral("KNOB:TABLE"));
    EXPECT_EQ(ControlObject::get(ConfigKey("[Crate]", "knob_focus")), 0.0);

    QTest::mouseClick(pKnob, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_TRUE(pKnob->isChecked());
    EXPECT_EQ(pKnob->text(), QStringLiteral("KNOB:MAP"));
    EXPECT_EQ(ControlObject::get(ConfigKey("[Crate]", "knob_focus")), 1.0);
    EXPECT_TRUE(m_pGalaxy->testKnobFocusMap());
    QTest::mouseClick(pKnob, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_FALSE(m_pGalaxy->testKnobFocusMap());
}

TEST_F(CrateGalaxyUiTest, KnobForwardWalksNearestUnvisited) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 3);
    setKnobFocusMap();

    // First tick seeds the cursor; the next two must each land on the
    // nearest-unvisited node by the port's own metric.
    pokeKnob(1.0);
    const int c1 = m_pGalaxy->testCursorNode();
    ASSERT_GE(c1, 0);

    pokeKnob(1.0);
    const int c2 = m_pGalaxy->testCursorNode();
    pokeKnob(1.0);
    const int c3 = m_pGalaxy->testCursorNode();

    // Three distinct nodes.
    EXPECT_NE(c1, c2);
    EXPECT_NE(c2, c3);
    EXPECT_NE(c1, c3);

    // Each forward step is the nearest unvisited node, not merely "changed".
    QSet<int> visited;
    visited.insert(c1);
    EXPECT_EQ(c2, expectedNearest(c1, visited));
    visited.insert(c2);
    EXPECT_EQ(c3, expectedNearest(c2, visited));
}

TEST_F(CrateGalaxyUiTest, KnobBackRetracesHistory) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 3);
    setKnobFocusMap();
    pokeKnob(1.0);
    const int c1 = m_pGalaxy->testCursorNode();
    pokeKnob(1.0);
    const int c2 = m_pGalaxy->testCursorNode();
    pokeKnob(1.0);
    const int c3 = m_pGalaxy->testCursorNode();
    ASSERT_NE(c1, c2);
    ASSERT_NE(c2, c3);

    // Back = pop history (return where you came from), NOT nearest-neighbor.
    pokeKnob(-1.0);
    EXPECT_EQ(m_pGalaxy->testCursorNode(), c2);
    pokeKnob(-1.0);
    EXPECT_EQ(m_pGalaxy->testCursorNode(), c1);
    // At the seed, further back is a no-op (cursor holds).
    pokeKnob(-1.0);
    EXPECT_EQ(m_pGalaxy->testCursorNode(), c1);
}

TEST_F(CrateGalaxyUiTest, GalaxyLoadTargetsFirstStoppedDeck) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 0);
    // Deck 1 playing, deck 2 stopped -> load must target deck 2.
    ControlObject deck1Play(ConfigKey("[Channel1]", "play"));
    ControlObject deck2Play(ConfigKey("[Channel2]", "play"));
    ControlPushButton deck1Load(ConfigKey("[Channel1]", "LoadSelectedTrack"));
    ControlPushButton deck2Load(ConfigKey("[Channel2]", "LoadSelectedTrack"));
    deck1Play.set(1.0);
    deck2Play.set(0.0);

    setKnobFocusMap();
    pokeKnob(1.0); // seed a cursor
    ASSERT_GE(m_pGalaxy->testCursorNode(), 0);

    ControlObject::set(ConfigKey("[Crate]", "galaxy_load"), 1.0);
    QApplication::processEvents();

    // The stopped deck (2) received the load; the playing deck (1) was spared.
    EXPECT_EQ(ControlObject::get(ConfigKey("[Channel2]", "LoadSelectedTrack")), 1.0);
    EXPECT_EQ(ControlObject::get(ConfigKey("[Channel1]", "LoadSelectedTrack")), 0.0);
}

TEST_F(CrateGalaxyUiTest, TableFocusIgnoresKnob) {
    ASSERT_GT(m_pGalaxy->testNodeCount(), 3);
    setKnobFocusMap();
    pokeKnob(1.0);
    const int seeded = m_pGalaxy->testCursorNode();
    ASSERT_GE(seeded, 0);

    // Toggle back to TABLE; the galaxy cursor must stop responding to the knob.
    QPushButton* pKnob = m_pControls->findChild<QPushButton*>(
            QStringLiteral("CrateMapKnob"));
    ASSERT_NE(pKnob, nullptr);
    QTest::mouseClick(pKnob, Qt::LeftButton);
    QApplication::processEvents();
    ASSERT_FALSE(pKnob->isChecked());

    pokeKnob(1.0);
    pokeKnob(1.0);
    EXPECT_EQ(m_pGalaxy->testCursorNode(), seeded);
}

TEST_F(CrateGalaxyUiTest, SubsetGhostsOnlyExcludedNodesWithoutMovingOrRebuilding) {
    const int count = m_pGalaxy->testNodeCount();
    ASSERT_GT(count, 4);
    QSet<QString> subset;
    QVector<QPointF> positions;
    QVector<quintptr> identities;
    for (int i = 0; i < count; ++i) {
        positions.append(m_pGalaxy->testNodeDisplayPos(i));
        identities.append(m_pGalaxy->testDotItemIdentity(i));
        if (i < 3) {
            subset.insert(m_pGalaxy->testNodeRelpath(i));
        }
    }

    m_pGalaxy->testApplySubsetByRelpaths(subset);
    for (int i = 0; i < count; ++i) {
        EXPECT_EQ(m_pGalaxy->testNodeGhosted(i), i >= 3);
        EXPECT_EQ(m_pGalaxy->testNodeDisplayPos(i), positions[i]);
        EXPECT_EQ(m_pGalaxy->testDotItemIdentity(i), identities[i]);
    }

    m_pGalaxy->testClearSubset();
    for (int i = 0; i < count; ++i) {
        EXPECT_FALSE(m_pGalaxy->testNodeGhosted(i));
        EXPECT_EQ(m_pGalaxy->testNodeDisplayPos(i), positions[i]);
        EXPECT_EQ(m_pGalaxy->testDotItemIdentity(i), identities[i]);
    }
}

// Regression: a tiny subset (a 3-track crate — or the 3-track capture profile
// that shipped this bug) must NOT erase the map's wayfinding. Cluster and
// artist names are geography and ignore the subset; only track text is
// subset-gated like the pills it replaced.
TEST_F(CrateGalaxyUiTest, WayfindingLabelsSurviveTinySubset) {
    const int count = m_pGalaxy->testNodeCount();
    ASSERT_GT(count, 4);

    m_pGalaxy->testRebuildLabels();
    const int clusterLabelsFull = m_pGalaxy->testLabelCount(0);
    ASSERT_GT(clusterLabelsFull, 0);

    QSet<QString> subset;
    for (int i = 0; i < 3; ++i) {
        subset.insert(m_pGalaxy->testNodeRelpath(i));
    }
    m_pGalaxy->testApplySubsetByRelpaths(subset);
    m_pGalaxy->testRebuildLabels();
    EXPECT_EQ(m_pGalaxy->testLabelCount(0), clusterLabelsFull)
            << "cluster names must not shrink when a subset ghosts the map";

    m_pGalaxy->testClearSubset();
}

TEST_F(CrateGalaxyUiTest, WheelZoomIsClampedBothDirections) {
    // Regression: unclamped wheel zoom ran the view transform to extremes —
    // "sometimes it will get to a point where i just see a blank page".
    const auto zoomRatio = [this] {
        return m_pGalaxy->transform().m11();
    };
    const double fitScale = zoomRatio();
    ASSERT_GT(fitScale, 0.0);
    const auto sendWheel = [this](int delta) {
        QWheelEvent event(QPointF(m_pGalaxy->viewport()->rect().center()),
                QPointF(),
                QPoint(),
                QPoint(0, delta),
                Qt::NoButton,
                Qt::NoModifier,
                Qt::NoScrollPhase,
                false);
        QApplication::sendEvent(m_pGalaxy->viewport(), &event);
    };
    for (int i = 0; i < 60; ++i) {
        sendWheel(-120);
    }
    EXPECT_GE(zoomRatio() / fitScale, 0.5 - 1e-6)
            << "zoom-out escaped the floor";
    for (int i = 0; i < 200; ++i) {
        sendWheel(+120);
    }
    EXPECT_LE(zoomRatio() / fitScale, 40.0 + 1e-6)
            << "zoom-in escaped the ceiling";
    for (int i = 0; i < 200; ++i) {
        sendWheel(-120);
    }
    EXPECT_GE(zoomRatio() / fitScale, 0.5 - 1e-6);
}

TEST_F(CrateGalaxyUiTest, PickResolvesDotsEvenWhenPillsOverlap) {
    // Regression: hover pills are large screen-anchored QGraphicsItems above
    // the dots; the old itemAt() pick returned the pill instead of the dot
    // beneath the cursor, so with a pill up, double-click load / right-click
    // menus resolved the wrong node or none ("hitbox gets really funky when
    // the pill shows up"). The pick is geometric now — pills must be invisible
    // to it.
    const int count = m_pGalaxy->testNodeCount();
    ASSERT_GT(count, 3);
    // Hover node 0 to spawn its pill (synthesized directly — QTest::mouseMove
    // produces no event when the OS cursor is already at the target).
    const QPoint hoverPos =
            m_pGalaxy->mapFromScene(m_pGalaxy->testNodeDisplayPos(0));
    QMouseEvent hover(QEvent::MouseMove,
            QPointF(hoverPos),
            m_pGalaxy->viewport()->mapToGlobal(hoverPos),
            Qt::NoButton,
            Qt::NoButton,
            Qt::NoModifier);
    QApplication::sendEvent(m_pGalaxy->viewport(), &hover);
    QApplication::processEvents();
    // Picking at a node's own viewport position must resolve to that node —
    // or, if dots crowd inside the pick radius, to one at least as close
    // (nearest-wins is the contract; the pill must never divert the pick).
    for (int i = 0; i < qMin(count, 8); ++i) {
        const QPoint pos =
                m_pGalaxy->mapFromScene(m_pGalaxy->testNodeDisplayPos(i));
        if (!m_pGalaxy->viewport()->rect().contains(pos)) {
            continue;
        }
        const int picked = m_pGalaxy->testNodeAtViewportPos(pos);
        ASSERT_GE(picked, 0) << "no pick at node " << i;
        const QPoint pickedPos = m_pGalaxy->mapFromScene(
                m_pGalaxy->testNodeDisplayPos(picked));
        const auto d2 = [&pos](const QPoint& p) {
            const QPoint d = p - pos;
            return d.x() * d.x() + d.y() * d.y();
        };
        // The click sits exactly on node i, so the picked node must render at
        // (or within integer-rounding of) the click point — anything farther
        // means the pick was diverted (the old pill-shadow failure mode).
        EXPECT_LE(d2(pickedPos), 4)
                << "pick diverted away from node " << i << " to " << picked;
    }
    // Far away from any dot: no pick.
    EXPECT_EQ(m_pGalaxy->testNodeAtViewportPos(QPoint(-500, -500)), -1);
}

TEST_F(CrateGalaxyUiTest, PillIsAnAliasOfItsDotAndDotStaysVisible) {
    // Interview 2026-07-19 round 2: "pill = the dot, everywhere" and "always
    // show both". Clicking anywhere on a pill's body must resolve to its node
    // (unless a real dot is nearer — dots take precedence), and the dot must
    // stay fully visible while its pill shows.
    ASSERT_GT(m_pGalaxy->testNodeCount(), 0);
    const QPoint anchor =
            m_pGalaxy->mapFromScene(m_pGalaxy->testNodeDisplayPos(0));
    // Synthesize the hover directly — QTest::mouseMove silently produces no
    // move event when the OS cursor already sits at the target position.
    QMouseEvent hover(QEvent::MouseMove,
            QPointF(anchor),
            m_pGalaxy->viewport()->mapToGlobal(anchor),
            Qt::NoButton,
            Qt::NoButton,
            Qt::NoModifier);
    QApplication::sendEvent(m_pGalaxy->viewport(), &hover);
    QApplication::processEvents();

    // The hovered node's dot is still visible at full opacity.
    QGraphicsItem* pDot = nullptr;
    for (QGraphicsItem* pItem : m_pGalaxy->scene()->items()) {
        if (pItem->data(0).isValid() && pItem->data(0).toInt() == 0 &&
                !qFuzzyCompare(pItem->zValue(), 10.0)) {
            pDot = pItem;
            break;
        }
    }
    ASSERT_NE(pDot, nullptr);
    EXPECT_TRUE(pDot->isVisible());
    EXPECT_DOUBLE_EQ(pDot->opacity(), 1.0);

    // A point on the pill body (right of the dot, outside the 14px dot radius)
    // resolves to node 0 — or to a strictly nearer real dot, never to nothing.
    const QPoint onPill = anchor + QPoint(60, 0);
    const int picked = m_pGalaxy->testNodeAtViewportPos(onPill);
    ASSERT_GE(picked, 0) << "click on the pill body resolved to nothing";
    if (picked != 0) {
        const QPoint pickedPos = m_pGalaxy->mapFromScene(
                m_pGalaxy->testNodeDisplayPos(picked));
        const QPoint d = pickedPos - onPill;
        EXPECT_LE(d.x() * d.x() + d.y() * d.y(), 14 * 14)
                << "pill click diverted to a non-adjacent node " << picked;
    }
}

TEST_F(CrateGalaxyUiTest, LocationResolvesUnderAnyRootSpelling) {
    // Regression: her real profile stored music_root as the UNC share while the
    // Mixxx library held Z:/ locations — the old music_root prefix strip
    // resolved nothing, so every node ghosted and pills/halos died. Matching is
    // now root-independent (suffix walk over known relpaths).
    ASSERT_GT(m_pGalaxy->testNodeCount(), 0);
    const QString relpath = m_pGalaxy->testNodeRelpath(0);
    ASSERT_FALSE(relpath.isEmpty());
    const QStringList spellings = {
            QStringLiteral("Z:/") + relpath,
            QStringLiteral("Z:\\") + QString(relpath).replace('/', '\\'),
            QStringLiteral("//192.168.5.203/media/") + relpath,
            QStringLiteral("\\\\192.168.5.203\\media\\") +
                    QString(relpath).replace('/', '\\'),
            QStringLiteral("D:/some/other/mount/") + relpath,
            QStringLiteral("Z:/") + relpath.toUpper(),
    };
    for (const QString& location : spellings) {
        EXPECT_EQ(m_pGalaxy->testRelpathForLocation(location), relpath)
                << "location spelling failed: " << location.toStdString();
    }
    EXPECT_TRUE(m_pGalaxy
                    ->testRelpathForLocation(
                            QStringLiteral("Z:/not/on/the/map/at all.flac"))
                    .isEmpty());
    EXPECT_TRUE(m_pGalaxy->testRelpathForLocation(QString()).isEmpty());
}

TEST_F(CrateGalaxyUiTest, Ghosted3dNodeIsNotProjectedPickable) {
    recreateGalaxy(/*mode3d=*/true, /*debugZoom=*/1.0);
    ASSERT_GT(m_pGalaxy->testNodeCount(), 1);
    const int ghost = 0;
    QSet<QString> subset;
    for (int i = 1; i < m_pGalaxy->testNodeCount(); ++i) {
        subset.insert(m_pGalaxy->testNodeRelpath(i));
    }
    m_pGalaxy->testApplySubsetByRelpaths(subset);
    const QPoint viewportPos = m_pGalaxy->mapFromScene(
            m_pGalaxy->testNodeDisplayPos(ghost));
    EXPECT_TRUE(m_pGalaxy->testNodeGhosted(ghost));
    EXPECT_NE(m_pGalaxy->testProjectedNodeAt(viewportPos), ghost);
}

TEST_F(CrateGalaxyUiTest, WalkAndHalosNeverUseGhostedNodes) {
    const int count = m_pGalaxy->testNodeCount();
    ASSERT_GT(count, 4);
    QSet<QString> subset;
    for (int i = 0; i < count; i += 2) {
        subset.insert(m_pGalaxy->testNodeRelpath(i));
    }
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_debug_seed"),
            m_pGalaxy->testNodeRelpath(0));
    m_pGalaxy->testApplySubsetByRelpaths(subset);
    setKnobFocusMap();
    for (int step = 0; step < 50; ++step) {
        pokeKnob(1.0);
        const int cursor = m_pGalaxy->testCursorNode();
        ASSERT_GE(cursor, 0);
        EXPECT_FALSE(m_pGalaxy->testNodeGhosted(cursor));
    }
    for (int i = 0; i < count; ++i) {
        if (m_pGalaxy->testNodeGhosted(i)) {
            EXPECT_FALSE(m_pGalaxy->testNodeHasHalo(i));
        }
    }
}

TEST(CrateGalaxyPickDeck, NextPrepDeckFillsEmptyDecksFirst) {
    using crate::WCrateGalaxy;
    const auto pick = [](std::initializer_list<bool> playing,
                              std::initializer_list<bool> loaded) {
        return WCrateGalaxy::pickNextPrepDeck(
                QVector<bool>(playing), QVector<bool>(loaded));
    };
    // Her rule: loaded-or-playing = taken. Fill empty decks in order; when all
    // are loaded, replace the lowest-numbered STOPPED deck; never steal a
    // playing one.
    // All empty -> deck 1.
    EXPECT_EQ(pick({false, false}, {false, false}), 1);
    // Deck 1 loaded (stopped) -> deck 2, NOT overwrite deck 1 (her live bug:
    // "clicking always puts something on deck 1").
    EXPECT_EQ(pick({false, false}, {true, false}), 2);
    // Decks 1+2 loaded, 4-deck rig -> deck 3, then 4.
    EXPECT_EQ(pick({false, false, false, false}, {true, true, false, false}), 3);
    EXPECT_EQ(pick({false, false, false, false}, {true, true, true, false}), 4);
    // Everything loaded, nothing playing -> replace deck 1.
    EXPECT_EQ(pick({false, false}, {true, true}), 1);
    // Everything loaded, deck 1 playing -> replace deck 2.
    EXPECT_EQ(pick({true, false}, {true, true}), 2);
    // Deck 1 playing but EMPTY... cannot happen live, but playing wins: skip it.
    EXPECT_EQ(pick({true, false}, {false, false}), 2);
    // All playing -> no-load sentinel (never steal).
    EXPECT_EQ(pick({true, true}, {true, true}), 0);
    EXPECT_EQ(pick({true, true, true, true}, {true, true, true, true}), 0);
    // Empty / no decks -> sentinel.
    EXPECT_EQ(pick({}, {}), 0);
}

TEST(CrateGalaxyOrbitSensitivity, ScreenMotionConstantAcrossZoom) {
    using crate::WCrateGalaxy;
    const int extent = 800; // viewport width in px
    const int pixels = 40;  // a mouse drag of 40px

    // At the fitted scale (zoomRatio 1.0) a full-width drag sweeps 360 degrees.
    const double atFit = WCrateGalaxy::orbitAngleDelta(extent, extent, 1.0);
    EXPECT_NEAR(atFit, 360.0, 1e-9);

    // Zooming in must SHRINK the angle per pixel by exactly the zoom ratio, so
    // the on-screen (scaled) motion of a node is identical. This is the fix for
    // "way too high when zoomed in".
    const double d1 = WCrateGalaxy::orbitAngleDelta(pixels, extent, 1.0);
    const double d8 = WCrateGalaxy::orbitAngleDelta(pixels, extent, 8.0);
    EXPECT_GT(d1, 0.0);
    EXPECT_NEAR(d1 / d8, 8.0, 1e-9); // 8x zoom -> 1/8 the angle
    // Screen-pixel motion = angle * zoom must match at both levels.
    EXPECT_NEAR(d1 * 1.0, d8 * 8.0, 1e-9);

    // A mid zoom scales proportionally too.
    const double d4 = WCrateGalaxy::orbitAngleDelta(pixels, extent, 4.0);
    EXPECT_NEAR(d1 / d4, 4.0, 1e-9);

    // Sign is preserved (dragging the other way orbits the other way).
    EXPECT_NEAR(WCrateGalaxy::orbitAngleDelta(-pixels, extent, 2.0),
            -WCrateGalaxy::orbitAngleDelta(pixels, extent, 2.0),
            1e-9);

    // Degenerate guards: zero/negative extent and non-positive zoom never crash
    // and never explode (treated as extent>=1, ratio 1.0).
    EXPECT_NEAR(WCrateGalaxy::orbitAngleDelta(pixels, 0, 1.0),
            pixels * 360.0, 1e-9);
    EXPECT_NEAR(WCrateGalaxy::orbitAngleDelta(pixels, extent, 0.0),
            WCrateGalaxy::orbitAngleDelta(pixels, extent, 1.0), 1e-9);
    EXPECT_NEAR(WCrateGalaxy::orbitAngleDelta(pixels, extent, -3.0),
            WCrateGalaxy::orbitAngleDelta(pixels, extent, 1.0), 1e-9);
}

TEST_F(CrateGalaxyUiTest, GhostedNodeOffersNoDeckLoadMenu) {
    ControlObject deck1Play(ConfigKey("[Channel1]", "play"));
    ControlObject deck2Play(ConfigKey("[Channel2]", "play"));
    ControlObject deck3Play(ConfigKey("[Channel3]", "play"));
    ControlObject deck4Play(ConfigKey("[Channel4]", "play"));
    deck1Play.set(0.0);
    deck2Play.set(0.0);
    deck3Play.set(0.0);
    deck4Play.set(0.0);

    const int count = m_pGalaxy->testNodeCount();
    ASSERT_GT(count, 4);
    // Ghost node 0 by making the subset every OTHER node.
    QSet<QString> subset;
    for (int i = 1; i < count; ++i) {
        subset.insert(m_pGalaxy->testNodeRelpath(i));
    }
    m_pGalaxy->testApplySubsetByRelpaths(subset);
    ASSERT_TRUE(m_pGalaxy->testNodeGhosted(0));

    // Ghosted node -> no deck-load entries at all.
    EXPECT_TRUE(m_pGalaxy->testDeckLoadLabels(0).isEmpty());
    // A live node -> one entry per existing deck.
    ASSERT_FALSE(m_pGalaxy->testNodeGhosted(1));
    EXPECT_EQ(m_pGalaxy->testDeckLoadLabels(1).size(), 4);

    // A playing deck's entry is annotated (and disabled -> not loadable).
    deck1Play.set(1.0);
    const QStringList labels = m_pGalaxy->testDeckLoadLabels(1);
    ASSERT_EQ(labels.size(), 4);
    EXPECT_TRUE(labels.at(0).contains(QStringLiteral("playing")));
    EXPECT_FALSE(labels.at(1).contains(QStringLiteral("playing")));
}

TEST_F(CrateGalaxyUiTest, MapLoadRequestsTableJumpToLoadedTrack) {
    ControlObject deck1Play(ConfigKey("[Channel1]", "play"));
    ControlObject deck2Play(ConfigKey("[Channel2]", "play"));
    ControlPushButton deck1Load(ConfigKey("[Channel1]", "LoadSelectedTrack"));
    ControlPushButton deck2Load(ConfigKey("[Channel2]", "LoadSelectedTrack"));
    deck1Play.set(1.0); // deck 1 busy -> next-prep = deck 2
    deck2Play.set(0.0);

    ASSERT_GT(m_pGalaxy->testNodeCount(), 0);
    setKnobFocusMap();
    pokeKnob(1.0); // seed a cursor (does not itself request a table jump)
    const int cursor = m_pGalaxy->testCursorNode();
    ASSERT_GE(cursor, 0);

    const int before = m_pGalaxy->testTableJumpRequests();
    ControlObject::set(ConfigKey("[Crate]", "galaxy_load"), 1.0);
    QApplication::processEvents();

    // The load asked the table to jump to exactly the loaded track...
    EXPECT_EQ(m_pGalaxy->testTableJumpRequests(), before + 1);
    EXPECT_EQ(m_pGalaxy->testLastTableJumpRelpath(),
            m_pGalaxy->testNodeRelpath(cursor));
    // ...and targeted the stopped deck, sparing the playing one (never-steal).
    EXPECT_EQ(ControlObject::get(ConfigKey("[Channel2]", "LoadSelectedTrack")), 1.0);
    EXPECT_EQ(ControlObject::get(ConfigKey("[Channel1]", "LoadSelectedTrack")), 0.0);
}

} // namespace
