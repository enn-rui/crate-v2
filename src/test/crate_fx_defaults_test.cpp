#include <gtest/gtest.h>

#include "control/controlobject.h"
#include "effects/effectchain.h"
#include "effects/effectslot.h"
#include "effects/effectsmanager.h"
#include "engine/channelhandle.h"
#include "test/mixxxtest.h"

namespace {

class CrateFxDefaultsTest : public MixxxTest {
  protected:
    std::unique_ptr<EffectsManager> makeManager() {
        auto pManager = std::make_unique<EffectsManager>(
                config(), m_pHandleFactory);
        const ChannelHandleAndGroup masterOutput(
                m_pHandleFactory->getOrCreateHandle("[MasterOutput]"),
                "[MasterOutput]");
        pManager->registerOutputChannel(masterOutput);
        pManager->registerInputChannel(masterOutput);
        for (int deck = 1; deck <= 4; ++deck) {
            const QString group = QStringLiteral("[Channel%1]").arg(deck);
            pManager->registerInputChannel(ChannelHandleAndGroup(
                    m_pHandleFactory->getOrCreateHandle(group), group));
        }
        pManager->setup();
        return pManager;
    }

    QStringList standardEffectIds(EffectsManager* pManager) {
        QStringList ids;
        for (int unit = 0; unit < kNumStandardEffectUnits; ++unit) {
            auto pSlot = pManager->getStandardEffectChain(unit)->getEffectSlot(0);
            ids.append(pSlot ? pSlot->id() : QString());
        }
        return ids;
    }

    void unloadUnit(EffectsManager* pManager, int unit) {
        auto pSlot = pManager->getStandardEffectChain(unit)->getEffectSlot(0);
        ASSERT_TRUE(pSlot);
        pSlot->loadEffectWithDefaults(nullptr);
    }

    // Routing enable ControlObject for a (one-based) unit -> channel group.
    static double routeEnable(int unit, const QString& deckGroup) {
        return ControlObject::get(ConfigKey(
                QStringLiteral("[EffectRack1_EffectUnit%1]").arg(unit),
                QStringLiteral("group_%1_enable").arg(deckGroup)));
    }
    static void setRouteEnable(int unit, const QString& deckGroup, double v) {
        ControlObject::set(ConfigKey(
                                   QStringLiteral("[EffectRack1_EffectUnit%1]").arg(unit),
                                   QStringLiteral("group_%1_enable").arg(deckGroup)),
                v);
    }

    std::shared_ptr<ChannelHandleFactory> m_pHandleFactory =
            std::make_shared<ChannelHandleFactory>();
};

// The FLX4 per-deck BEAT FX model requires unit 1 to edit deck A and unit 2 to
// edit deck B, each exclusively (never bleeding to the other deck or master).
TEST_F(CrateFxDefaultsTest, EffectUnitsRouteToOwnDeckExclusively) {
    auto pManager = makeManager();

    EXPECT_DOUBLE_EQ(1.0, routeEnable(1, "[Channel1]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel2]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel3]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel4]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Master]"));

    EXPECT_DOUBLE_EQ(1.0, routeEnable(2, "[Channel2]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(2, "[Channel1]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(2, "[Channel3]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(2, "[Channel4]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(2, "[Master]"));
}

// The seed must re-route a unit that has NO routing at all, and re-running it
// must be a no-op (idempotent).
TEST_F(CrateFxDefaultsTest, SeedRoutesUnroutedUnitAndIsIdempotent) {
    auto pManager = makeManager();

    // Simulate a unit that ended up with no routing (e.g. a stale saved state).
    setRouteEnable(1, "[Channel1]", 0.0);
    ASSERT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel1]"));

    pManager->seedCrateEffectUnitDeckRouting();
    EXPECT_DOUBLE_EQ(1.0, routeEnable(1, "[Channel1]"))
            << "an unrouted unit 1 must be seeded back to deck A";
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel2]"));

    // Idempotent: a second pass changes nothing.
    pManager->seedCrateEffectUnitDeckRouting();
    EXPECT_DOUBLE_EQ(1.0, routeEnable(1, "[Channel1]"));
    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel2]"));
}

// A unit the user manually re-routed (any channel enabled) must be left alone.
TEST_F(CrateFxDefaultsTest, SeedLeavesManuallyRoutedUnitAlone) {
    auto pManager = makeManager();

    // Manual re-route: unit 1 now feeds deck 3 only, not its default deck A.
    setRouteEnable(1, "[Channel1]", 0.0);
    setRouteEnable(1, "[Channel3]", 1.0);

    pManager->seedCrateEffectUnitDeckRouting();

    EXPECT_DOUBLE_EQ(0.0, routeEnable(1, "[Channel1]"))
            << "the seed must not fight a manual re-route";
    EXPECT_DOUBLE_EQ(1.0, routeEnable(1, "[Channel3]"));
}

TEST_F(CrateFxDefaultsTest, EmptySavedRackSeedsAllUnitsOnceAndControlsResolve) {
    {
        auto pManager = makeManager();
        for (int unit = 0; unit < kNumStandardEffectUnits; ++unit) {
            unloadUnit(pManager.get(), unit);
        }
    }

    const QStringList expected{
            QStringLiteral("org.mixxx.effects.echo"),
            QStringLiteral("org.mixxx.effects.reverb"),
            QStringLiteral("org.mixxx.effects.filter"),
            QStringLiteral("org.mixxx.effects.filter"),
    };
    {
        auto pManager = makeManager();
        EXPECT_EQ(standardEffectIds(pManager.get()), expected);
        for (int unit = 1; unit <= kNumStandardEffectUnits; ++unit) {
            const QString chainGroup =
                    QStringLiteral("[EffectRack1_EffectUnit%1]").arg(unit);
            EXPECT_EQ(ControlObject::get(ConfigKey(
                              chainGroup, QStringLiteral("focused_effect"))),
                    1.0);
            EXPECT_TRUE(ControlObject::exists(ConfigKey(
                    QStringLiteral("[EffectRack1_EffectUnit%1_Effect1]").arg(unit),
                    QStringLiteral("meta"))));
        }
    }
    {
        auto pManager = makeManager();
        EXPECT_EQ(standardEffectIds(pManager.get()), expected)
                << "restoring the seeded rack must be a no-op";
    }
}

TEST_F(CrateFxDefaultsTest, PartiallyLoadedRackIsUntouched) {
    {
        auto pManager = makeManager();
        unloadUnit(pManager.get(), 0);
        unloadUnit(pManager.get(), 1);
        unloadUnit(pManager.get(), 3);
    }
    auto pManager = makeManager();
    EXPECT_EQ(standardEffectIds(pManager.get()),
            (QStringList{QString(),
                    QString(),
                    QStringLiteral("org.mixxx.effects.filter"),
                    QString()}));
}

} // namespace
