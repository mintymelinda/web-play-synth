#include "PolySynthEngine.h"

#include <algorithm>
#include <cmath>

namespace webplaysynth::engine {
namespace {
float parameter(const ParameterSnapshotView& snapshot, SynthParameter index) noexcept
{
    return snapshot.values[static_cast<std::uint32_t>(index)];
}
}

bool PolySynthEngine::decodeParameters(const ParameterSnapshotView& snapshot,
                                       double sampleRate, SynthParameters& p) noexcept
{
    if (!snapshot.isValid() || snapshot.valueCount != synthParameterCount
        || !std::isfinite(sampleRate) || sampleRate <= 0.0)
        return false;
    for (std::uint32_t i = 0; i < snapshot.valueCount; ++i)
        if (!std::isfinite(snapshot.values[i]))
            return false;

    const auto v = [&snapshot](SynthParameter id) { return parameter(snapshot, id); };
    p.osc1Wave = static_cast<int>(std::lround(v(SynthParameter::osc1Wave)));
    p.osc2Wave = static_cast<int>(std::lround(v(SynthParameter::osc2Wave)));
    p.osc1Octave = v(SynthParameter::osc1Octave);
    p.osc1Tune = v(SynthParameter::osc1Tune);
    p.osc1Level = v(SynthParameter::osc1Level);
    p.osc1PulseWidth = v(SynthParameter::osc1PulseWidth);
    p.osc2Octave = v(SynthParameter::osc2Octave);
    p.osc2Tune = v(SynthParameter::osc2Tune);
    p.osc2Level = v(SynthParameter::osc2Level);
    p.osc2PulseWidth = v(SynthParameter::osc2PulseWidth);
    p.hardSync = v(SynthParameter::hardSync) > 0.5f;
    p.crossMod = v(SynthParameter::crossMod);
    p.cutoff = v(SynthParameter::cutoff);
    p.resonance = v(SynthParameter::resonance);
    p.filterContour = v(SynthParameter::filterContour);
    p.filterType = static_cast<int>(std::lround(v(SynthParameter::filterType)));
    p.filterDrive = v(SynthParameter::filterDrive);
    p.aftertouchDepth = v(SynthParameter::aftertouchDepth);
    p.filterAttack = v(SynthParameter::filterAttack);
    p.filterDecay = v(SynthParameter::filterDecay);
    p.filterSustain = v(SynthParameter::filterSustain);
    p.filterRelease = v(SynthParameter::filterRelease);
    p.ampAttack = v(SynthParameter::ampAttack);
    p.ampDecay = v(SynthParameter::ampDecay);
    p.ampSustain = v(SynthParameter::ampSustain);
    p.ampRelease = v(SynthParameter::ampRelease);
    p.lfoWave = static_cast<int>(std::lround(v(SynthParameter::lfoWave)));
    p.lfoRate = v(SynthParameter::lfoRate);
    p.lfoPitchDepth = v(SynthParameter::lfoPitchDepth);
    p.lfoFilterDepth = v(SynthParameter::lfoFilterDepth);
    p.lfoAmpDepth = v(SynthParameter::lfoAmpDepth);
    p.lfoPanDepth = v(SynthParameter::lfoPanDepth);
    p.lfoPulseDepth = v(SynthParameter::lfoPulseDepth);
    p.master = v(SynthParameter::master);
    p.unisonVoices = static_cast<int>(std::lround(v(SynthParameter::unisonVoices)));
    p.unisonDetune = v(SynthParameter::unisonDetune);
    p.stereoSpread = v(SynthParameter::stereoSpread);
    p.chorusRate = v(SynthParameter::chorusRate);
    p.chorusDepth = v(SynthParameter::chorusDepth);
    p.chorusMix = v(SynthParameter::chorusMix);
    p.delayTime = v(SynthParameter::delayTime);
    p.delayFeedback = v(SynthParameter::delayFeedback);
    p.delayMix = v(SynthParameter::delayMix);
    p.reverbMix = v(SynthParameter::reverbMix);
    p.reverbRoom = v(SynthParameter::reverbRoom);
    p.reverbDamping = v(SynthParameter::reverbDamping);
    p.glideEnabled = v(SynthParameter::glideEnabled) > 0.5f;
    p.glideTime = v(SynthParameter::glideTime);
    p.vco1Enabled = v(SynthParameter::vco1Enabled) > 0.5f;
    p.vco2Enabled = v(SynthParameter::vco2Enabled) > 0.5f;
    p.filterEnvEnabled = v(SynthParameter::filterEnvEnabled) > 0.5f;
    p.ampEnvEnabled = v(SynthParameter::ampEnvEnabled) > 0.5f;

    p.glideCoefficient = p.glideEnabled
        ? 1.0 - std::exp(-std::log(1000.0) /
            (static_cast<double>(p.glideTime) * sampleRate))
        : 1.0;
    p.osc1FrequencyRatio = std::pow(2.0, (p.osc1Octave * 12.0 + p.osc1Tune) / 12.0);
    p.osc2FrequencyRatio = std::pow(2.0, (p.osc2Octave * 12.0 + p.osc2Tune) / 12.0);
    p.hardSyncRatio = std::clamp(p.osc2FrequencyRatio / p.osc1FrequencyRatio, 0.25, 8.0);
    p.filterDriveGain = std::pow(10.0f, p.filterDrive / 20.0f);

    const auto unisonCount = 1 << std::clamp(p.unisonVoices, 0, 2);
    const auto layerGain = 1.0f / std::sqrt(static_cast<float>(unisonCount));
    p.unisonLayerGain = layerGain;
    constexpr auto sqrtTwo = 1.4142135623730950488f;
    constexpr auto pi = 3.14159265358979323846f;
    for (int layer = 0; layer < 4; ++layer) {
        const auto position = unisonCount == 1 ? 0.0
            : (layer < unisonCount
                ? -1.0 + 2.0 * layer / static_cast<double>(unisonCount - 1)
                : 0.0);
        p.unisonDetuneRatios[static_cast<std::size_t>(layer)] =
            std::pow(2.0, p.unisonDetune * position / 1200.0);
        p.unisonPanPositions[static_cast<std::size_t>(layer)] =
            static_cast<float>(position * p.stereoSpread);
        const auto angle = (p.unisonPanPositions[static_cast<std::size_t>(layer)] + 1.0f)
            * pi * 0.25f;
        p.unisonLeftGains[static_cast<std::size_t>(layer)] =
            std::cos(angle) * sqrtTwo * layerGain;
        p.unisonRightGains[static_cast<std::size_t>(layer)] =
            std::sin(angle) * sqrtTwo * layerGain;
    }
    return true;
}

EngineStatus PolySynthEngine::prepare(const ProcessSpec& spec)
{
    if (!spec.isValid())
        return EngineStatus::invalidSpecification;
    reset(ResetMode::voices);
    specification = spec;
    for (auto& voice : voices)
        voice.prepare(spec.sampleRateHz);
    channelPitchBendSemitones.fill(0.0f);
    channelAftertouch.fill(0.0f);
    sustainPedalDown.fill(false);
    lastPlayedFrequency = 0.0;
    voiceAge = 0;
    prepared = true;
    return EngineStatus::ok;
}

EngineStatus PolySynthEngine::process(const EngineProcessBlock& block) noexcept
{
    if (!prepared)
        return EngineStatus::notPrepared;
    if (!block.parameters.isValid() || block.parameters.valueCount != synthParameterCount)
        return EngineStatus::incompatibleParameters;
    if (!block.events.isValid(block.output.frameCount))
        return EngineStatus::invalidEvents;
    if (!block.isValid(specification))
        return EngineStatus::invalidAudioBlock;

    SynthParameters parameters;
    if (!decodeParameters(block.parameters, specification.sampleRateHz, parameters))
        return EngineStatus::incompatibleParameters;

    for (std::uint32_t channel = 0; channel < block.output.channelCount; ++channel)
        std::fill_n(block.output.channels[channel], block.output.frameCount, 0.0f);

    std::uint32_t cursor = 0;
    if (block.events.source != nullptr)
        block.events.source->rewind();
    for (std::uint32_t i = 0; i < block.events.eventCount; ++i) {
        EngineEvent event;
        if (block.events.source != nullptr) {
            if (!block.events.source->next(event))
                return EngineStatus::invalidEvents;
        } else {
            event = block.events.events[i];
        }
        if (event.frameOffset < cursor)
            return EngineStatus::invalidEvents;
        renderRange(block.output, cursor, event.frameOffset, parameters);
        handleEvent(event, parameters);
        cursor = event.frameOffset;
    }
    renderRange(block.output, cursor, block.output.frameCount, parameters);
    return EngineStatus::ok;
}

void PolySynthEngine::renderRange(const AudioBlockView& output, std::uint32_t start,
                                  std::uint32_t end, const SynthParameters& parameters) noexcept
{
    for (auto frame = start; frame < end; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        for (auto& voice : voices) {
            float voiceLeft = 0.0f;
            float voiceRight = 0.0f;
            voice.render(parameters, specification.sampleRateHz, voiceLeft, voiceRight);
            left += voiceLeft;
            right += voiceRight;
        }
        if (output.channelCount == 1)
            output.channels[0][frame] = (left + right) * 0.5f;
        else {
            output.channels[0][frame] = left;
            output.channels[1][frame] = right;
        }
    }
}

Voice* PolySynthEngine::findVoiceToStart() noexcept
{
    for (auto& voice : voices)
        if (!voice.active)
            return &voice;
    auto* oldest = &voices.front();
    for (auto& voice : voices)
        if (voice.age < oldest->age)
            oldest = &voice;
    return oldest;
}

void PolySynthEngine::startNote(const EngineEvent& event,
                                const SynthParameters& parameters) noexcept
{
    const auto channel = static_cast<int>(event.channel);
    const auto channelIndex = static_cast<std::size_t>(channel - 1);
    auto* voice = findVoiceToStart();
    voice->age = ++voiceAge;
    voice->start(event.note, event.value, channel,
        channelPitchBendSemitones[channelIndex], channelAftertouch[channelIndex],
        parameters, lastPlayedFrequency);
    lastPlayedFrequency = midiNoteToFrequency(event.note);
}

void PolySynthEngine::stopNote(int note, int channel) noexcept
{
    const auto channelIndex = static_cast<std::size_t>(channel - 1);
    for (auto& voice : voices) {
        if (voice.active && voice.note == note && voice.midiChannel == channel && voice.keyDown) {
            voice.keyDown = false;
            if (sustainPedalDown[channelIndex])
                voice.sustainHeld = true;
            else
                voice.release();
        }
    }
}

void PolySynthEngine::handleEvent(const EngineEvent& event,
                                  const SynthParameters& parameters) noexcept
{
    const auto channel = static_cast<int>(event.channel);
    const auto channelIndex = static_cast<std::size_t>(channel - 1);
    switch (event.type) {
        case EngineEventType::noteOn:
            if (event.value <= 0.0f)
                stopNote(event.note, channel);
            else
                startNote(event, parameters);
            break;
        case EngineEventType::noteOff:
            stopNote(event.note, channel);
            break;
        case EngineEventType::pitchBend: {
            const auto bend = event.value * 2.0f;
            channelPitchBendSemitones[channelIndex] = bend;
            for (auto& voice : voices) {
                if (voice.active && voice.midiChannel == channel) {
                    voice.pitchBendSemitones = bend;
                    voice.targetFrequency = midiNoteToFrequency(voice.note)
                        * std::pow(2.0, static_cast<double>(bend) / 12.0);
                }
            }
            break;
        }
        case EngineEventType::channelPressure:
            channelAftertouch[channelIndex] = event.value;
            for (auto& voice : voices)
                if (voice.active && voice.midiChannel == channel)
                    voice.aftertouch = event.value;
            break;
        case EngineEventType::polyphonicKeyPressure:
            for (auto& voice : voices)
                if (voice.active && voice.midiChannel == channel && voice.note == event.note)
                    voice.aftertouch = event.value;
            break;
        case EngineEventType::sustainPedal: {
            const auto down = event.value >= 0.5f;
            const auto wasDown = sustainPedalDown[channelIndex];
            sustainPedalDown[channelIndex] = down;
            if (wasDown && !down) {
                for (auto& voice : voices)
                    if (voice.active && voice.midiChannel == channel && voice.sustainHeld)
                        voice.release();
            }
            break;
        }
        case EngineEventType::allNotesOff:
            for (auto& voice : voices) {
                if (voice.active && voice.midiChannel == channel && voice.keyDown) {
                    voice.keyDown = false;
                    if (sustainPedalDown[channelIndex])
                        voice.sustainHeld = true;
                    else
                        voice.release();
                }
            }
            break;
        case EngineEventType::allSoundOff:
            for (auto& voice : voices) {
                if (voice.active && voice.midiChannel == channel) {
                    voice.active = false;
                    voice.keyDown = false;
                    voice.sustainHeld = false;
                    voice.ampEnvelope.reset();
                    voice.filterEnvelope.reset();
                    voice.note = -1;
                }
            }
            break;
        case EngineEventType::resetControllers:
            sustainPedalDown[channelIndex] = false;
            channelPitchBendSemitones[channelIndex] = 0.0f;
            channelAftertouch[channelIndex] = 0.0f;
            for (auto& voice : voices) {
                if (voice.active && voice.midiChannel == channel) {
                    voice.targetFrequency = midiNoteToFrequency(voice.note);
                    voice.pitchBendSemitones = 0.0f;
                    voice.aftertouch = 0.0f;
                    if (voice.sustainHeld)
                        voice.release();
                }
            }
            break;
    }
}

void PolySynthEngine::reset(ResetMode mode) noexcept
{
    if (mode == ResetMode::effects)
        return;
    for (auto& voice : voices) {
        voice.active = false;
        voice.keyDown = false;
        voice.sustainHeld = false;
        voice.ampEnvelope.reset();
        voice.filterEnvelope.reset();
        voice.note = -1;
    }
    channelPitchBendSemitones.fill(0.0f);
    channelAftertouch.fill(0.0f);
    sustainPedalDown.fill(false);
    lastPlayedFrequency = 0.0;
    voiceAge = 0;
}

void PolySynthEngine::release() noexcept
{
    reset(ResetMode::voices);
    prepared = false;
    specification = {};
}

} // namespace webplaysynth::engine
