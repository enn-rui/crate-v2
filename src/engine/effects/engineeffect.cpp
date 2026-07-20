#include "engine/effects/engineeffect.h"

#include "effects/backends/effectsbackendmanager.h"
#include "engine/effects/engineeffectparameter.h"
#include "engine/engine.h"
#include "util/defs.h"
#include "util/sample.h"

namespace {

// Used during initialization where the SoundSevice is not set up
constexpr auto kInitalSampleRate = mixxx::audio::SampleRate(96000);

} // namespace

EngineEffect::EngineEffect(EffectManifestPointer pManifest,
        EffectsBackendManagerPointer pBackendManager,
        const QSet<ChannelHandleAndGroup>& activeInputChannels,
        const QSet<ChannelHandleAndGroup>& registeredInputChannels,
        const QSet<ChannelHandleAndGroup>& registeredOutputChannels)
        : m_pManifest(pManifest),
          m_pProcessor(pBackendManager->createProcessor(pManifest)),
          m_parameters(pManifest->parameters().size()) {
    const QList<EffectManifestParameterPointer>& parameters = m_pManifest->parameters();
    for (int i = 0; i < parameters.size(); ++i) {
        EffectManifestParameterPointer param = parameters.at(i);
        EngineEffectParameterPointer pParameter(new EngineEffectParameter(param));
        m_parameters[i] = pParameter;
        m_parametersById[param->id()] = pParameter;
    }

    for (const ChannelHandleAndGroup& outputChannel : registeredOutputChannels) {
        m_disabledOutputChannelMap.insert(outputChannel.handle(), EffectEnableState::Disabled);
    }
    for (const ChannelHandleAndGroup& inputChannel : registeredInputChannels) {
        m_effectEnableStateForChannelMatrix.insert(
                inputChannel.handle(), m_disabledOutputChannelMap);
    }

    m_pProcessor->loadEngineEffectParameters(m_parametersById);

    // At this point the SoundDevice is not set up so we use the kInitalSampleRate.
    const mixxx::EngineParameters engineParameters(
            kInitalSampleRate,
            kMaxEngineFrames);
    m_pProcessor->initialize(activeInputChannels, registeredOutputChannels, engineParameters);
    m_effectRampsFromDry = pManifest->effectRampsFromDry();
}

EngineEffect::~EngineEffect() {
    if constexpr (kEffectDebugOutput) {
        qDebug() << debugString() << "destroyed";
    }
}

void EngineEffect::initializeInputChannel(ChannelHandle inputChannel, bool effectEnabled) {
    if (!m_pProcessor->hasStatesForInputChannel(inputChannel)) {
        // At this point the SoundDevice is not set up so we use the kInitalSampleRate.
        const mixxx::EngineParameters engineParameters(
                kInitalSampleRate,
                kMaxEngineFrames);
        m_pProcessor->initializeInputChannel(inputChannel, engineParameters);
    }

    // m_effectEnableStateForChannelMatrix is only populated in the constructor and
    // its entries are only ever flipped (never inserted) by SET_EFFECT_PARAMETERS.
    // An input channel registered after this effect was loaded is therefore missing
    // from the matrix, and process() treats a missing entry as Disabled, silently
    // ignoring the channel. This is what caused effects to have no audible effect on
    // decks 3/4, which are created when the Crate skin raises [App],num_decks to 4
    // after the standard effect chains (and their seeded effects) already exist.
    //
    // Add the channel now, mirroring the constructor. Running on the main thread
    // before the caller sends ENABLE_EFFECT_CHAIN_FOR_INPUT_CHANNEL is the same
    // ordering the EffectProcessor state allocation above already relies on, and the
    // channel is not routed to this effect (so the audio thread does not touch its
    // row) until that later message is processed.
    if (m_effectEnableStateForChannelMatrix[inputChannel].isEmpty()) {
        m_effectEnableStateForChannelMatrix.insert(
                inputChannel, m_disabledOutputChannelMap);
        if (effectEnabled) {
            // Bring the fresh row in line with the effect's current enable state so
            // processing starts on the next callback, exactly as SET_EFFECT_PARAMETERS
            // would have done had the channel existed when the effect was enabled.
            for (auto& enableState : m_effectEnableStateForChannelMatrix[inputChannel]) {
                enableState = EffectEnableState::Enabling;
            }
        }
    }
}

bool EngineEffect::processEffectsRequest(const EffectsRequest& message,
        EffectsResponsePipe* pResponsePipe) {
    EngineEffectParameterPointer pParameter;
    EffectsResponse response(message);

    switch (message.type) {
    case EffectsRequest::SET_EFFECT_PARAMETERS:
        if (kEffectDebugOutput) {
            qDebug() << debugString() << "SET_EFFECT_PARAMETERS"
                     << "enabled" << message.SetEffectParameters.enabled;
        }

        for (auto& outputMap : m_effectEnableStateForChannelMatrix) {
            for (auto& enableState : outputMap) {
                if (enableState != EffectEnableState::Disabled &&
                        !message.SetEffectParameters.enabled) {
                    enableState = EffectEnableState::Disabling;
                    // If an input is not routed to the chain, and the effect gets
                    // a message to disable, then the effect gets the message to enable,
                    // process() will not have executed, so the enableState will still be
                    // DISABLING instead of DISABLED.
                } else if ((enableState == EffectEnableState::Disabled ||
                                   enableState ==
                                           EffectEnableState::Disabling) &&
                        message.SetEffectParameters.enabled) {
                    enableState = EffectEnableState::Enabling;
                }
            }
        }

        response.success = true;
        pResponsePipe->writeMessage(response);
        return true;
        break;
    case EffectsRequest::SET_PARAMETER_PARAMETERS:
        if (kEffectDebugOutput) {
            qDebug() << debugString() << "SET_PARAMETER_PARAMETERS"
                     << "parameter" << message.SetParameterParameters.iParameter
                     << "value" << message.value;
        }
        pParameter = m_parameters.value(
                message.SetParameterParameters.iParameter, EngineEffectParameterPointer());
        if (pParameter) {
            pParameter->setValue(message.value);
            response.success = true;
        } else {
            response.success = false;
            response.status = EffectsResponse::NO_SUCH_PARAMETER;
        }
        pResponsePipe->writeMessage(response);
        return true;
    default:
        break;
    }
    return false;
}

bool EngineEffect::process(const ChannelHandle& inputHandle,
        const ChannelHandle& outputHandle,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const std::size_t numSamples,
        const mixxx::audio::SampleRate sampleRate,
        const EffectEnableState chainEnableState,
        const GroupFeatureState& groupFeatures) {
    // Compute the effective enable state from the combination of the effect's state
    // for the channel and the state passed from the EngineEffectChain.

    // When the chain's input routing switch or chain enable switches are changed,
    // the chain sends an intermediate enabling/disabling signal. The chain also sends
    // intermediate enabling/disabling signals when its dry/wet knob is turned down to
    // fully dry then turned back up to let some wet signal through.

    // Analagously, when the Effect is switched on/off, it sends this EngineEffect an
    // intermediate enabling/disabling signal.

    // The effective enable state is then passed down to the EffectProcessor, which is
    // responsible for taking appropriate action when it gets an intermediate
    // enabling/disabling signal. For example, the Echo effect clears its
    // internal buffer for the channel when it gets the intermediate disabling signal.

    EffectEnableState effectiveEffectEnableState =
            m_effectEnableStateForChannelMatrix[inputHandle][outputHandle];

    // If the EngineEffect is fully disabled, do not let
    // intermediate enabling/disabling signals from the chain override
    // the EngineEffect's state.
    if (effectiveEffectEnableState != EffectEnableState::Disabled) {
        if (chainEnableState == EffectEnableState::Disabled) {
            // If the chain is fully disabled, skip calling the EffectProcessor.
            effectiveEffectEnableState = EffectEnableState::Disabled;
        } else if (chainEnableState == EffectEnableState::Disabling) {
            // If the chain happens to be in the intermediate disabling state
            // in the same callback as the effect is in the intermediate enabling
            // state, the EffectProcessor should get the disabling signal, not the
            // enabling signal.
            effectiveEffectEnableState = EffectEnableState::Disabling;
        } else if (chainEnableState == EffectEnableState::Enabling) {
            // If the chain happens to be in the intermediate enabling state
            // in the same callback as the effect is in the intermediate disabling
            // state, the EffectProcessor should get the disabling signal, not the
            // enabling signal.
            if (effectiveEffectEnableState != EffectEnableState::Disabling) {
                effectiveEffectEnableState = EffectEnableState::Enabling;
            }
        }
    }

    bool processingOccured = false;

    if (effectiveEffectEnableState != EffectEnableState::Disabled) {
        //TODO: refactor rest of audio engine to use mixxx::AudioParameters
        const mixxx::EngineParameters engineParameters(
                sampleRate,
                numSamples / mixxx::kEngineChannelOutputCount);

        m_pProcessor->process(inputHandle,
                outputHandle,
                pInput,
                pOutput,
                engineParameters,
                effectiveEffectEnableState,
                groupFeatures);

        processingOccured = true;

        if (!m_effectRampsFromDry) {
            // the effect does not fade, so we care for it
            if (effectiveEffectEnableState == EffectEnableState::Disabling) {
                DEBUG_ASSERT(pInput != pOutput); // Fade to dry only works if pInput is not touched by pOutput
                // Fade out (fade to dry signal)
                SampleUtil::linearCrossfadeBuffersOut(
                        pOutput,
                        pInput,
                        numSamples,
                        mixxx::kEngineChannelOutputCount);
            } else if (effectiveEffectEnableState == EffectEnableState::Enabling) {
                DEBUG_ASSERT(pInput != pOutput); // Fade to dry only works if pInput is not touched by pOutput
                // Fade in (fade to wet signal)
                SampleUtil::linearCrossfadeBuffersOut(
                        pOutput,
                        pInput,
                        numSamples,
                        mixxx::kEngineChannelOutputCount);
            }
        }
    }

    // Now that the EffectProcessor has been sent the intermediate enabling/disabling
    // signal, set the channel state to fully enabled/disabled for the next engine callback.
    EffectEnableState& effectOnChannelState = m_effectEnableStateForChannelMatrix[inputHandle][outputHandle];
    if (effectOnChannelState == EffectEnableState::Disabling) {
        effectOnChannelState = EffectEnableState::Disabled;
    } else if (effectOnChannelState == EffectEnableState::Enabling) {
        effectOnChannelState = EffectEnableState::Enabled;
    }

    return processingOccured;
}
