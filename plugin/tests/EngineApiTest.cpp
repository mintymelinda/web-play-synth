#include "Engine/EngineApi.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace webplaysynth::engine;

    require(ProcessSpec{44100.0, 512, 0, 2}.isValid(),
        "valid stereo output process specification must be accepted");
    require(ProcessSpec{0.0, 512, 0, 2}.isValid() == false,
        "zero sample rate must be rejected");
    require(ProcessSpec{48000.0, 0, 0, 2}.isValid() == false,
        "zero maximum block size must be rejected");
    require(ProcessSpec{48000.0, 512, 0, 3}.isValid() == false,
        "unsupported output channel count must be rejected");
    require(ProcessSpec{std::numeric_limits<double>::infinity(), 512, 0, 2}.isValid() == false,
        "non-finite sample rate must be rejected");

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    std::array<float*, 2> outputChannels {left.data(), right.data()};
    AudioBlockView output{outputChannels.data(), 2, 8};
    require(output.isValid(), "valid planar output view must be accepted");
    require(AudioBlockView{nullptr, 2, 8}.isValid() == false,
        "non-empty output with null channel table must be rejected");
    std::array<float*, 2> invalidOutputChannels {left.data(), nullptr};
    require(AudioBlockView{invalidOutputChannels.data(), 2, 8}.isValid() == false,
        "non-empty output with a null channel must be rejected");
    require(AudioBlockView{nullptr, 0, 0}.isValid() == false,
        "output must expose at least one channel even for an empty block");

    std::array<const float*, 2> inputChannels {left.data(), right.data()};
    require(ConstAudioBlockView{inputChannels.data(), 2, 8}.isValid(),
        "valid planar input view must be accepted");
    require(ConstAudioBlockView{nullptr, 0, 0}.isValid(),
        "empty input view must be accepted");

    const EngineEvent noteOn{EngineEventType::noteOn, 0, 1, 60, 0.75f};
    const EngineEvent bend{EngineEventType::pitchBend, 7, 16, 0, -1.0f};
    const EngineEvent pedal{EngineEventType::sustainPedal, 3, 2, 0, 1.0f};
    require(noteOn.isValid(8), "valid note-on must be accepted");
    require(bend.isValid(8), "valid boundary pitch bend must be accepted");
    require(pedal.isValid(8), "valid sustain pedal event must be accepted");
    require(EngineEvent{EngineEventType::noteOn, 0, 1, 128, 0.5f}.isValid(8) == false,
        "out-of-range MIDI note must be rejected");
    require(EngineEvent{EngineEventType::noteOn, 0, 0, 60, 0.5f}.isValid(8) == false,
        "MIDI channel zero must be rejected");
    require(EngineEvent{EngineEventType::pitchBend, 0, 1, 0, 1.01f}.isValid(8) == false,
        "out-of-range pitch bend must be rejected");
    require(EngineEvent{EngineEventType::noteOff, 8, 1, 60, 0.0f}.isValid(8) == false,
        "event at block end must be rejected instead of deferred ambiguously");
    require(EngineEvent{EngineEventType::channelPressure, 0, 1, 0,
        std::numeric_limits<float>::quiet_NaN()}.isValid(8) == false,
        "non-finite event values must be rejected");

    const std::array<EngineEvent, 3> orderedEvents {{
        noteOn,
        EngineEvent{EngineEventType::noteOff, 0, 1, 60, 0.0f},
        EngineEvent{EngineEventType::noteOn, 4, 1, 64, 0.5f}
    }};
    require(EngineEventView{orderedEvents.data(), 3}.isValid(8),
        "valid ordered event view must be accepted");
    require(EngineEventView{nullptr, 1}.isValid(8) == false,
        "non-empty event view with null storage must be rejected");
    require(EngineEventView{orderedEvents.data(), 3}.isValid(0) == false,
        "events cannot be applied to an empty block");
    require(EngineEventView{nullptr, 0}.isValid(0),
        "empty event view must be accepted");

    const std::array<float, 2> values {0.25f, 0.75f};
    require(ParameterSnapshotView{values.data(), 2, parameterSchemaVersion, 42}.isValid(),
        "known parameter schema with values must be accepted");
    require(ParameterSnapshotView{nullptr, 2, parameterSchemaVersion, 42}.isValid() == false,
        "non-empty parameter snapshot with null values must be rejected");
    require(ParameterSnapshotView{nullptr, 0, parameterSchemaVersion + 1, 42}.isValid() == false,
        "unknown parameter schema must be rejected");

    std::cout << "PASS: framework-neutral shared engine API types\n";
    return 0;
}
