// Crate v2 regression test: effects must apply to decks created after the
// standard effect chains (and their seeded effects) already exist.
//
// The Crate skin raises [App],num_decks to 4 at skin-load time, which is AFTER
// EffectsManager::setup() has built the standard effect units and (via the fork's
// loadCrateDefaultStandardEffects seam) seeded Echo/Reverb into them. Decks 3 and 4
// are therefore registered as effect input channels only after the effect is loaded.
//
// EngineEffect::m_effectEnableStateForChannelMatrix used to be populated only in the
// constructor, and SET_EFFECT_PARAMETERS only flips existing rows. A channel that
// appeared later was missing from the matrix, and EngineEffect::process() treats a
// missing row as Disabled -- so the effect silently ignored decks 3/4. The user-visible
// symptom was "FX do not apply to channels 3 and 4 in 4-channel mode".
//
// The fix makes EngineEffect::initializeInputChannel() (already called on the main
// thread for every channel that gets routed to a chain, via
// EffectChain::enableForInputChannel) also register the channel in the enable-state
// matrix, in the effect's current enable state. This test reproduces the late-add
// timeline at the EngineEffect level and asserts the late channel is actually processed.

#include <gtest/gtest.h>

#include <QSet>
#include <vector>

#include "effects/backends/builtin/gaineffect.h"
#include "effects/backends/effectmanifest.h"
#include "effects/backends/effectsbackendmanager.h"
#include "effects/defs.h"
#include "engine/channelhandle.h"
#include "engine/effects/engineeffect.h"
#include "engine/effects/groupfeaturestate.h"
#include "engine/effects/message.h"
#include "test/mixxxtest.h"
#include "util/messagepipe.h"
#include "util/types.h"

namespace {

constexpr std::size_t kNumFrames = 64;
constexpr std::size_t kNumSamples = kNumFrames * 2; // stereo
const mixxx::audio::SampleRate kSampleRate = mixxx::audio::SampleRate(44100);

class CrateFxRoutingTest : public MixxxTest {
  protected:
    CrateFxRoutingTest()
            : m_master(m_factory.getOrCreateHandle("[Master]"), "[Master]"),
              m_deck1(m_factory.getOrCreateHandle("[Channel1]"), "[Channel1]"),
              m_deck3(m_factory.getOrCreateHandle("[Channel3]"), "[Channel3]"),
              m_pBackendManager(QSharedPointer<EffectsBackendManager>::create()),
              m_pipes(makeTwoWayMessagePipe<EffectsRequest*, EffectsResponse>(1024, 1024)) {
    }

    // Send SET_EFFECT_PARAMETERS to the engine effect, as EffectSlot::updateEngineState
    // does when an effect is loaded or its enable button changes.
    void setEffectEnabled(EngineEffect* pEffect, bool enabled) {
        EffectsRequest request;
        request.type = EffectsRequest::SET_EFFECT_PARAMETERS;
        request.pTargetEffect = pEffect;
        request.SetEffectParameters.enabled = enabled;
        pEffect->processEffectsRequest(request, &m_pipes.second);
    }

    // Runs one engine callback for the given input channel with the chain fully enabled
    // for it (mirrors EngineEffectChain routing an enabled channel to the effect).
    // Returns whether the effect processed the channel.
    bool processChannel(EngineEffect* pEffect, const ChannelHandleAndGroup& input) {
        GroupFeatureState groupFeatures;
        return pEffect->process(input.handle(),
                m_master.handle(),
                m_input.data(),
                m_output.data(),
                kNumSamples,
                kSampleRate,
                EffectEnableState::Enabled,
                groupFeatures);
    }

    ChannelHandleFactory m_factory;
    ChannelHandleAndGroup m_master;
    ChannelHandleAndGroup m_deck1;
    ChannelHandleAndGroup m_deck3;
    QSharedPointer<EffectsBackendManager> m_pBackendManager;
    std::pair<EffectsRequestPipe, EffectsResponsePipe> m_pipes;
    std::vector<CSAMPLE> m_input = std::vector<CSAMPLE>(kNumSamples, 0.5f);
    std::vector<CSAMPLE> m_output = std::vector<CSAMPLE>(kNumSamples, 0.0f);
};

// Decks 3/4 are created after the effect is loaded (num_decks raised by the skin).
// Before the fix the late channel was never added to the effect's enable-state matrix,
// so process() returned false (no processing) -- FX silently skipped decks 3/4.
TEST_F(CrateFxRoutingTest, LateAddedDeckIsProcessedByLoadedEffect) {
    EffectManifestPointer pManifest =
            m_pBackendManager->getManifest(GainEffect::getId(), EffectBackendType::BuiltIn);
    ASSERT_TRUE(pManifest);

    // Only deck 1 exists when the effect is loaded (decks 1/2 at EffectsManager::setup()).
    QSet<ChannelHandleAndGroup> registeredInput{m_deck1};
    QSet<ChannelHandleAndGroup> registeredOutput{m_master};
    QSet<ChannelHandleAndGroup> activeInput{m_deck1};

    EngineEffect effect(pManifest,
            m_pBackendManager,
            activeInput,
            registeredInput,
            registeredOutput);

    // Effect loaded and enabled into the standard unit.
    setEffectEnabled(&effect, true);

    // Sanity: a deck that existed when the effect was loaded is processed. This holds
    // both before and after the fix and validates the harness.
    EXPECT_TRUE(processChannel(&effect, m_deck1))
            << "deck present at effect-load time should be processed";

    // Deck 3 is created later (4-deck mode). EffectChain::enableForInputChannel calls
    // EffectSlot::initializeInputChannel -> EngineEffect::initializeInputChannel with the
    // effect's current enable state when the deck's FX-assign switch is turned on.
    effect.initializeInputChannel(m_deck3.handle(), /*effectEnabled=*/true);

    // The regression: the late-added deck must actually be processed by the effect.
    EXPECT_TRUE(processChannel(&effect, m_deck3))
            << "FX must apply to a deck created after the effect was loaded (decks 3/4 "
               "in 4-channel mode)";
}

// A disabled effect must not spuriously process a late-added deck: initializeInputChannel
// with effectEnabled=false leaves the channel disabled until the effect is enabled.
TEST_F(CrateFxRoutingTest, LateAddedDeckStaysDisabledWhenEffectDisabled) {
    EffectManifestPointer pManifest =
            m_pBackendManager->getManifest(GainEffect::getId(), EffectBackendType::BuiltIn);
    ASSERT_TRUE(pManifest);

    QSet<ChannelHandleAndGroup> registeredInput{m_deck1};
    QSet<ChannelHandleAndGroup> registeredOutput{m_master};
    QSet<ChannelHandleAndGroup> activeInput{m_deck1};

    EngineEffect effect(pManifest,
            m_pBackendManager,
            activeInput,
            registeredInput,
            registeredOutput);

    // Effect present but switched OFF.
    setEffectEnabled(&effect, false);

    effect.initializeInputChannel(m_deck3.handle(), /*effectEnabled=*/false);
    EXPECT_FALSE(processChannel(&effect, m_deck3))
            << "a disabled effect must not process a late-added deck";

    // Turning the effect on now enables the (already registered) late deck.
    setEffectEnabled(&effect, true);
    EXPECT_TRUE(processChannel(&effect, m_deck3))
            << "enabling the effect must start processing the late-added deck";
}

} // namespace
