// Widget-level interaction tests for the galaxy chip bar: real synthesized
// mouse clicks on the buttons must switch modes. This exists because the
// interactive path shipped broken once while every mode worked via config keys
// — config-driven verification cannot catch dead controls.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPointF>
#include <QPushButton>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "control/controlencoder.h"
#include "control/controlobject.h"
#include "control/controlpushbutton.h"
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
        // The browse knob drives the stock [Library],MoveVertical encoder
        // (ignoreNops=false, exactly like LibraryControl), so repeated +1 ticks
        // each emit. Create it BEFORE the galaxy so its ControlProxy binds.
        m_pMoveVertical = std::make_unique<ControlEncoder>(
                ConfigKey("[Library]", "MoveVertical"), false);
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
        QPushButton* pKnob = m_pGalaxy->findChild<QPushButton*>(
                QStringLiteral("KnobFocusChip"));
        ASSERT_NE(pKnob, nullptr);
        if (!pKnob->isChecked()) {
            QTest::mouseClick(pKnob, Qt::LeftButton);
            QApplication::processEvents();
        }
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

TEST_F(CrateGalaxyUiTest, KnobChipDefaultsTableAndToggles) {
    QPushButton* pKnob = m_pGalaxy->findChild<QPushButton*>(
            QStringLiteral("KnobFocusChip"));
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
    QPushButton* pKnob = m_pGalaxy->findChild<QPushButton*>(
            QStringLiteral("KnobFocusChip"));
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

} // namespace
