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

    std::shared_ptr<ChannelHandleFactory> m_pHandleFactory =
            std::make_shared<ChannelHandleFactory>();
};

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
