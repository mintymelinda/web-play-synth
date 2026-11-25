#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace webplaysynth::engine {

// Framework-neutral boundary types. These are non-owning views: callers retain
// ownership and must keep referenced storage alive for the duration of the call.
constexpr std::uint32_t parameterSchemaVersion = 1;
constexpr std::uint32_t maximumAudioChannels = 2;
constexpr std::uint32_t maximumMidiChannel = 16;
constexpr std::uint32_t maximumMidiNote = 127;

struct ProcessSpec {
    double sampleRateHz = 0.0;
    std::uint32_t maximumFramesPerBlock = 0;
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 0;

    bool isValid() const noexcept
    {
        return std::isfinite(sampleRateHz) && sampleRateHz > 0.0
            && maximumFramesPerBlock > 0
            && inputChannels <= maximumAudioChannels
            && outputChannels > 0 && outputChannels <= maximumAudioChannels;
    }
};

struct ConstAudioBlockView {
    const float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint32_t frameCount = 0;

    bool isValid() const noexcept
    {
        if (channelCount > maximumAudioChannels)
            return false;
        if (frameCount == 0)
            return true;
        if (channelCount == 0 || channels == nullptr)
            return false;
        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            if (channels[channel] == nullptr)
                return false;
        }
        return true;
    }
};

struct AudioBlockView {
    float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint32_t frameCount = 0;

    bool isValid() const noexcept
    {
        if (channelCount == 0 || channelCount > maximumAudioChannels)
            return false;
        if (frameCount == 0)
            return true;
        if (channels == nullptr)
            return false;
        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            if (channels[channel] == nullptr)
                return false;
        }
        return true;
    }
};

enum class EngineEventType : std::uint8_t {
    noteOn,
    noteOff,
    pitchBend,
    channelPressure,
    polyphonicKeyPressure,
    sustainPedal,
    allNotesOff,
    allSoundOff,
    resetControllers
};

// value is velocity [0, 1] for noteOn, normalized bend [-1, 1] for pitchBend,
// pressure [0, 1] for pressure events, and pedal state [0, 1] for sustainPedal.
// channel is MIDI's 1-based channel number. note is used by note and poly
// pressure events. Event arrays are consumed in supplied order; equal-offset
// events are never reordered by the engine.
struct EngineEvent {
    EngineEventType type = EngineEventType::noteOn;
    std::uint32_t frameOffset = 0;
    std::uint8_t channel = 1;
    std::uint8_t note = 0;
    float value = 0.0f;

    bool isValid(std::uint32_t blockFrames) const noexcept
    {
        if (frameOffset >= blockFrames || channel == 0 || channel > maximumMidiChannel
            || !std::isfinite(value))
            return false;

        switch (type) {
            case EngineEventType::noteOn:
                return note <= maximumMidiNote && value >= 0.0f && value <= 1.0f;
            case EngineEventType::noteOff:
                return note <= maximumMidiNote;
            case EngineEventType::pitchBend:
                return value >= -1.0f && value <= 1.0f;
            case EngineEventType::channelPressure:
            case EngineEventType::sustainPedal:
                return value >= 0.0f && value <= 1.0f;
            case EngineEventType::polyphonicKeyPressure:
                return note <= maximumMidiNote && value >= 0.0f && value <= 1.0f;
            case EngineEventType::allNotesOff:
            case EngineEventType::allSoundOff:
            case EngineEventType::resetControllers:
                return true;
        }
        return false;
    }
};

struct EngineEventView {
    const EngineEvent* events = nullptr;
    std::uint32_t eventCount = 0;

    bool isValid(std::uint32_t blockFrames) const noexcept
    {
        if (eventCount == 0)
            return true;
        if (events == nullptr || blockFrames == 0)
            return false;
        for (std::uint32_t i = 0; i < eventCount; ++i) {
            if (!events[i].isValid(blockFrames))
                return false;
        }
        return true;
    }
};

// Values are plain floats in engine-defined order and native units. The engine
// copies/consumes the view at a block boundary and must not retain its pointer.
// schemaVersion allows incompatible parameter layouts to be rejected safely.
struct ParameterSnapshotView {
    const float* values = nullptr;
    std::uint32_t valueCount = 0;
    std::uint32_t schemaVersion = parameterSchemaVersion;
    std::uint64_t revision = 0;

    bool isValid() const noexcept
    {
        return schemaVersion == parameterSchemaVersion
            && (valueCount == 0 || values != nullptr);
    }
};

enum class ResetMode : std::uint8_t {
    voices,
    effects,
    all
};

enum class EngineStatus : std::uint8_t {
    ok,
    notPrepared,
    invalidSpecification,
    invalidAudioBlock,
    invalidEvents,
    unsupportedBlockSize,
    incompatibleParameters
};

} // namespace webplaysynth::engine


// A modulation block is planar: each source points to frameCount floats.
// Source ordering is defined by the engine/host integration contract, not by
// this transport type. A zero-source view is valid and represents no external
// modulation. The engine must not retain any source pointer after process().
struct ModulationInputView {
    const float* const* sources = nullptr;
    std::uint32_t sourceCount = 0;
    std::uint32_t frameCount = 0;

    bool isValid(std::uint32_t expectedFrames) const noexcept
    {
        if (frameCount != expectedFrames)
            return false;
        if (sourceCount == 0)
            return true;
        if (sources == nullptr)
            return false;
        for (std::uint32_t source = 0; source < sourceCount; ++source) {
            if (sources[source] == nullptr)
                return false;
        }
        return true;
    }
};

struct EngineProcessBlock {
    ConstAudioBlockView input;
    AudioBlockView output;
    EngineEventView events;
    ParameterSnapshotView parameters;
    ModulationInputView modulation;

    bool isValid(const ProcessSpec& spec) const noexcept
    {
        if (!spec.isValid())
            return false;
        if (!output.isValid() || output.channelCount != spec.outputChannels)
            return false;
        if (spec.inputChannels == 0) {
            if (input.channelCount != 0 || input.frameCount != 0)
                return false;
        } else if (!input.isValid() || input.channelCount != spec.inputChannels
                   || input.frameCount != output.frameCount) {
            return false;
        }
        if (output.frameCount > spec.maximumFramesPerBlock)
            return false;
        if (!events.isValid(output.frameCount) || !parameters.isValid())
            return false;
        return modulation.isValid(output.frameCount);
    }
};

// Stable host-independent lifecycle and block-processing contract. prepare()
// may allocate and must only be called outside the real-time callback. process()
// and reset() must not allocate, block, perform I/O, or retain any view pointer.
// Implementations consume events in array order and split rendering at each
// event frame offset so event timing is sample-accurate. release() is idempotent.
class ISynthEngine {
public:
    virtual ~ISynthEngine() = default;
    virtual EngineStatus prepare(const ProcessSpec& spec) = 0;
    virtual EngineStatus process(const EngineProcessBlock& block) noexcept = 0;
    virtual void reset(ResetMode mode) noexcept = 0;
    virtual void release() noexcept = 0;
    virtual bool isPrepared() const noexcept = 0;
};

} // namespace webplaysynth::engine
