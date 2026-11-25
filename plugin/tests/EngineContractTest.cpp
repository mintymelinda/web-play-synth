#include "Engine/EngineApi.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class ContractProbe final : public webplaysynth::engine::ISynthEngine {
public:
    webplaysynth::engine::EngineStatus prepare(
        const webplaysynth::engine::ProcessSpec& value) override
    {
        if (!value.isValid())
            return webplaysynth::engine::EngineStatus::invalidSpecification;
        spec = value;
        prepared = true;
        return webplaysynth::engine::EngineStatus::ok;
    }

    webplaysynth::engine::EngineStatus process(
        const webplaysynth::engine::EngineProcessBlock& block) noexcept override
    {
        if (!prepared)
            return webplaysynth::engine::EngineStatus::notPrepared;
        return block.isValid(spec)
            ? webplaysynth::engine::EngineStatus::ok
            : webplaysynth::engine::EngineStatus::invalidAudioBlock;
    }

    void reset(webplaysynth::engine::ResetMode mode) noexcept override
    {
        lastReset = mode;
    }

    void release() noexcept override { prepared = false; }
    bool isPrepared() const noexcept override { return prepared; }

    webplaysynth::engine::ResetMode lastReset = webplaysynth::engine::ResetMode::all;

private:
    webplaysynth::engine::ProcessSpec spec;
    bool prepared = false;
};
}

int main()
{
    using namespace webplaysynth::engine;

    const ProcessSpec spec{48000.0, 128, 0, 2};
    std::array<float, 16> left{}, right{}, lfo{};
    std::array<float*, 2> outputChannels{left.data(), right.data()};
    std::array<const float*, 1> modulationSources{lfo.data()};
    const std::array<float, 3> parameterValues{0.25f, 0.5f, 0.75f};
    const std::array<EngineEvent, 2> events{{
        {EngineEventType::noteOn, 0, 1, 60, 0.8f},
        {EngineEventType::noteOff, 12, 1, 60, 0.0f}
    }};

    EngineProcessBlock block{
        {nullptr, 0, 0},
        {outputChannels.data(), 2, 16},
        {events.data(), static_cast<std::uint32_t>(events.size())},
        {parameterValues.data(), static_cast<std::uint32_t>(parameterValues.size()),
            parameterSchemaVersion, 9},
        {modulationSources.data(), 1, 16}
    };

    require(block.isValid(spec),
        "valid planar output, ordered events, parameters, and modulation must be accepted");

    auto unexpectedInput = block;
    unexpectedInput.input = {inputChannels.data(), 1, 16};
    require(!unexpectedInput.isValid(spec),
        "input channels must be absent when the prepared specification has no input bus");

    auto invalidModulation = block;
    invalidModulation.modulation.frameCount = 15;
    require(!invalidModulation.isValid(spec),
        "modulation frame count must match the audio block");

    auto invalidEvent = block;
    const EngineEvent outOfRange{EngineEventType::noteOn, 16, 1, 60, 0.8f};
    invalidEvent.events = {&outOfRange, 1};
    require(!invalidEvent.isValid(spec),
        "events at the block boundary must be rejected");

    auto invalidSchema = block;
    invalidSchema.parameters.schemaVersion = parameterSchemaVersion + 1;
    require(!invalidSchema.isValid(spec),
        "unknown parameter schema versions must be rejected");

    auto oversized = block;
    oversized.output.frameCount = 129;
    oversized.modulation.frameCount = 129;
    require(!oversized.isValid(spec),
        "blocks larger than the prepared maximum must be rejected");

    ContractProbe engine;
    require(!engine.isPrepared(), "engine starts unprepared");
    require(engine.process(block) == EngineStatus::notPrepared,
        "processing before prepare must report notPrepared");
    require(engine.prepare(spec) == EngineStatus::ok && engine.isPrepared(),
        "valid prepare must establish the engine lifecycle");
    require(engine.process(block) == EngineStatus::ok,
        "valid process block must be accepted after prepare");
    engine.reset(ResetMode::effects);
    require(engine.lastReset == ResetMode::effects,
        "reset mode must be forwarded without ambiguity");
    engine.release();
    engine.release();
    require(!engine.isPrepared(),
        "release must be idempotent and return the engine to unprepared state");

    std::cout << "PASS: shared DSP engine lifecycle and block contract\n";
    return 0;
}
