#pragma once

#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>

#include "audio/types.h"
#include "effects/backends/effectmanifest.h"
#include "effects/backends/effectprocessor.h"
#include "engine/channelhandle.h"
#include "engine/effects/message.h"
#include "util/types.h"

/// EngineEffect is a generic wrapper around an EffectProcessor which intermediates
/// between an EffectSlot and the EffectProcessor. It implements the logic to handle
/// changes of state (enable switch, chain routing switches, parameters' state) so
/// so EffectProcessor subclasses only need to implement their specific DSP logic.
class EngineEffect final : public EffectsRequestHandler {
  public:
    /// Called in main thread by EffectSlot
    EngineEffect(EffectManifestPointer pManifest,
            EffectsBackendManagerPointer pBackendManager,
            const QSet<ChannelHandleAndGroup>& activeInputChannels,
            const QSet<ChannelHandleAndGroup>& registeredInputChannels,
            const QSet<ChannelHandleAndGroup>& registeredOutputChannels);
    /// Called in main thread by EffectSlot
    // Doesn't deal with ownership; only for conditional debug output
    ~EngineEffect();

    /// Called from the main thread to make sure that the channel already has states.
    /// Also ensures the channel is present in m_effectEnableStateForChannelMatrix so
    /// that channels registered after this effect was loaded (e.g. decks 3/4 created
    /// when a skin raises [App],num_decks) are not silently ignored by process().
    /// effectEnabled must reflect the effect slot's current enable state so a newly
    /// added channel starts processing immediately if the effect is already on.
    void initializeInputChannel(ChannelHandle inputChannel, bool effectEnabled);

    /// Called in audio thread
    bool processEffectsRequest(
            const EffectsRequest& message,
            EffectsResponsePipe* pResponsePipe) override;

    /// Called in audio thread
    bool process(const ChannelHandle& inputHandle,
            const ChannelHandle& outputHandle,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const std::size_t numSamples,
            const mixxx::audio::SampleRate sampleRate,
            const EffectEnableState chainEnableState,
            const GroupFeatureState& groupFeatures);

    const EffectManifestPointer getManifest() const {
        return m_pManifest;
    }

    const QString& name() const {
        return m_pManifest->name();
    }

    SINT getGroupDelayFrames() {
        return m_pProcessor->getGroupDelayFrames();
    }

  private:
    QString debugString() const {
        return QString("EngineEffect(%1)").arg(m_pManifest->name());
    }

    EffectManifestPointer m_pManifest;
    std::unique_ptr<EffectProcessor> m_pProcessor;
    ChannelHandleMap<ChannelHandleMap<EffectEnableState>> m_effectEnableStateForChannelMatrix;
    // Template output-channel map (every registered output -> Disabled), stored so
    // input channels registered after construction can be added to the matrix in
    // initializeInputChannel() without re-deriving the output set.
    ChannelHandleMap<EffectEnableState> m_disabledOutputChannelMap;
    bool m_effectRampsFromDry;
    // Must not be modified after construction.
    QVector<EngineEffectParameterPointer> m_parameters;
    QMap<QString, EngineEffectParameterPointer> m_parametersById;

};
