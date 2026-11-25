#include "Engine/PolySynthEngine.h"

#include <array>
#include <cmath>
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

std::array<float, webplaysynth::engine::synthParameterCount> defaultParameters()
{
    return {{
        2.0f, 3.0f, 0.0f, 0.0f, 0.72f, 0.5f,
        0.0f, 7.0f, 0.38f, 0.5f, 0.0f, 0.0f,
        1800.0f, 0.8f, 0.55f, 0.0f, 0.0f, 0.0f,
        0.025f, 0.42f, 0.32f, 0.38f,
        0.015f, 0.25f, 0.78f, 0.32f,
        0.0f, 4.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.72f, 0.0f, 0.0f, 0.0f,
        0.8f, 0.5f, 0.0f, 0.32f, 0.35f, 0.0f,
        0.0f, 0.55f, 0.4f, 0.0f, 0.18f,
        1.0f, 1.0f, 1.0f, 1.0f
    }};
}

float rms(const std::array<float, 256>& left, const std::array<float, 256>& right)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
        sum += static_cast<double>(left[i]) * left[i]
            + static_cast<double>(right[i]) * right[i];
    return static_cast<float>(std::sqrt(sum / (2.0 * left.size())));
}
}

int main()
{
    using namespace webplaysynth::engine;

    PolySynthEngine engine;
    const ProcessSpec spec{48000.0, 256, 0, 2};
    require(engine.prepare(spec) == EngineStatus::ok && engine.isPrepared(),
        "engine prepares with the requested sample rate and buses");

    auto values = defaultParameters();
    ParameterSnapshotView parameters{values.data(), synthParameterCount,
        parameterSchemaVersion, 1};
    std::array<float, 256> left{}, right{};
    std::array<float*, 2> outputChannels{left.data(), right.data()};
    const EngineEvent noteOn{EngineEventType::noteOn, 0, 1, 60, 0.8f};
    EngineProcessBlock block{
        {nullptr, 0, 0},
        {outputChannels.data(), 2, 256},
        {&noteOn, 1},
        parameters,
        {nullptr, 0, 256}
    };

    require(engine.process(block) == EngineStatus::ok,
        "portable engine accepts a valid note-on block");
    require(rms(left, right) > 0.00001f,
        "note-on produces audible output from the portable voice renderer");
    for (std::size_t i = 0; i < left.size(); ++i)
        require(std::isfinite(left[i]) && std::isfinite(right[i]),
            "rendered samples remain finite");

    const EngineEvent velocityZeroNoteOn{EngineEventType::noteOn, 0, 1, 60, 0.0f};
    block.events = {&velocityZeroNoteOn, 1};
    require(engine.process(block) == EngineStatus::ok,
        "zero-velocity note-on follows MIDI note-off semantics");

    engine.reset(ResetMode::voices);
    block.events = {nullptr, 0};
    require(engine.process(block) == EngineStatus::ok,
        "engine continues processing after a voice reset");
    require(rms(left, right) == 0.0f,
        "voice reset leaves the next silent block silent");
    const EngineEvent restartNote{EngineEventType::noteOn, 0, 1, 60, 0.8f};
    block.events = {&restartNote, 1};
    require(engine.process(block) == EngineStatus::ok && rms(left, right) > 0.00001f,
        "engine can start a fresh voice after reset");
    require(engine.prepare(spec) == EngineStatus::ok,
        "reprepare replaces the previous engine lifecycle");
    block.events = {nullptr, 0};
    require(engine.process(block) == EngineStatus::ok && rms(left, right) == 0.0f,
        "reprepare retires voices from the previous configuration");

    const EngineEvent unordered[] = {
        {EngineEventType::noteOn, 10, 1, 60, 0.8f},
        {EngineEventType::noteOff, 9, 1, 60, 0.0f}
    };
    block.events = {unordered, 2};
    require(!block.events.isValid(block.output.frameCount),
        "out-of-order events are rejected before rendering");

    block.events = {nullptr, 0};
    auto wrongSchema = parameters;
    wrongSchema.schemaVersion = parameterSchemaVersion + 1;
    block.parameters = wrongSchema;
    require(engine.process(block) == EngineStatus::incompatibleParameters,
        "unknown parameter schemas are rejected");
    block.parameters = parameters;

    engine.release();
    require(!engine.isPrepared(), "release returns the engine to unprepared state");
    require(engine.process(block) == EngineStatus::notPrepared,
        "released engine refuses to process");
    std::cout << "PASS: portable poly synth engine processing\n";
    return 0;
}
