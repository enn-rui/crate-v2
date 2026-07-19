// Widget-level interaction tests for the sidebar MAP controls: real synthesized
// input on the controls must switch galaxy state. This exists because the
// interactive path shipped broken once while every mode worked via config keys
// — config-driven verification cannot catch dead controls.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
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

    void recreateGalaxy(bool mode3d, double debugZoom) {
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
            ASSERT_TRUE(query.exec(QStringLiteral(
                    "CREATE TABLE coords3d AS SELECT relpath, x, y, "
                    "((rowid % 7) - 3) * 0.1 AS z FROM coords")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        m_pConfig->setValue(ConfigKey("[Crate]", "sidecar_dir"), sidecarDir);
        m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_3d"), mode3d ? 1 : 0);
        m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_debug_zoom"), debugZoom);
        m_pGalaxy = std::make_unique<crate::WCrateGalaxy>(
                nullptr, /*pPlayerManager=*/nullptr, m_pConfig);
        m_pGalaxy->resize(900, 700);
        m_pGalaxy->show();
        QApplication::processEvents();
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

TEST_F(CrateGalaxyUiTest, ZoomedIn3dShowsCulledPills) {
    recreateGalaxy(/*mode3d=*/true, /*debugZoom=*/4.0);
    const int count = pillCount();
    EXPECT_GT(count, 0);
    EXPECT_LT(count, 40);
}

TEST_F(CrateGalaxyUiTest, ZoomedOut3dHidesLodPills) {
    recreateGalaxy(/*mode3d=*/true, /*debugZoom=*/1.0);
    EXPECT_EQ(pillCount(), 0);
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

TEST(CrateGalaxyPickDeck, NextPrepDeckRuleCoversStateTable) {
    using crate::WCrateGalaxy;
    // Nothing playing -> deck 1.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{false, false}, -1), 1);
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(
                      QVector<bool>{false, false, false, false}, -1),
            1);
    // Deck 1 playing -> deck 2 (opposite side).
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{true, false}, 0), 2);
    // Deck 2 playing -> deck 1 (opposite side).
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{false, true}, 1), 1);
    // Decks 1+2 playing, 3/4 stopped -> the opposite side of the last-started.
    // Last-started deck 2 (right) -> opposite left -> deck 3.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(
                      QVector<bool>{true, true, false, false}, 1),
            3);
    // Last-started deck 1 (left) -> opposite right -> deck 4.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(
                      QVector<bool>{true, true, false, false}, 0),
            4);
    // All playing -> no-load sentinel (never steal).
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{true, true}, 0), 0);
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(
                      QVector<bool>{true, true, true, true}, 2),
            0);
    // Unknown last-started falls back to the lowest playing deck as reference.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{true, false}, -1), 2);
    // Opposite side full -> fall back to the lowest-numbered stopped deck.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(
                      QVector<bool>{true, false, true, true}, 3),
            2);
    // Empty / no decks -> sentinel.
    EXPECT_EQ(WCrateGalaxy::pickNextPrepDeck(QVector<bool>{}, -1), 0);
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
