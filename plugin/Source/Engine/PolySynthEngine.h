#pragma once

#include "EngineApi.h"
#include "VoiceEngine.h"

#include <array>
#include <cstdint>

namespace webplaysynth::engine {

// The parameter snapshot is a stable, versioned array of native-unit values.
// Keep this order synchronized with the host adapter and the snapshot tests.
enum class SynthParameter : std::uint32_t {
    osc1Wave, osc2Wave, osc1Octave, osc1Tune, osc1Level, osc1PulseWidth,
    osc2Octave, osc2Tune, osc2Level, osc2PulseWidth, hardSync, crossMod,
    cutoff, resonance, filterContour, filterType, filterDrive, aftertouchDepth,
    filterAttack, filterDecay, filterSustain, filterRelease,
    ampAttack, ampDecay, ampSustain, ampRelease,
    lfoWave, lfoRate, lfoPitchDepth, lfoFilterDepth, lfoAmpDepth, lfoPanDepth, lfoPulseDepth,
    master, unisonVoices, unisonDetune, stereoSpread,
    chorusRate, chorusDepth, chorusMix, delayTime, delayFeedback, delayMix,
    reverbMix, reverbRoom, reverbDamping, glideEnabled, glideTime,
    vco1Enabled, vco2Enabled, filterEnvEnabled, ampEnvEnabled,
    count
};

constexpr std::uint32_t synthParameterCount =
    static_cast<std::uint32_t>(SynthParameter::count);

class PolySynthEngine final : public ISynthEngine {
public:
    EngineStatus prepare(const ProcessSpec& spec) override;
    EngineStatus process(const EngineProcessBlock& block) noexcept override;
    void reset(ResetMode mode) noexcept override;
    void release() noexcept override;
    bool isPrepared() const noexcept override { return prepared; }

private:
    static bool decodeParameters(const ParameterSnapshotView& snapshot,
                                 double sampleRate, SynthParameters& result) noexcept;
    void renderRange(const AudioBlockView& output, std::uint32_t start,
                     std::uint32_t end, const SynthParameters& parameters) noexcept;
    void handleEvent(const EngineEvent& event, const SynthParameters& parameters) noexcept;
    Voice* findVoiceToStart() noexcept;
    void startNote(const EngineEvent& event, const SynthParameters& parameters) noexcept;
    void stopNote(int note, int channel) noexcept;

    ProcessSpec specification {};
    std::array<Voice, 16> voices {};
    std::array<float, 16> channelPitchBendSemitones {};
    std::array<float, 16> channelAftertouch {};
    std::array<bool, 16> sustainPedalDown {};
    double lastPlayedFrequency = 0.0;
    int voiceAge = 0;
    bool prepared = false;
};

} // namespace webplaysynth::engine
