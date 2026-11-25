#include "Engine/VoiceEngine.h"

#include <algorithm>
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
}

int main()
{
    using namespace webplaysynth::engine;

    require(std::abs(midiNoteToFrequency(69) - 440.0) < 1.0e-12,
        "portable MIDI frequency conversion must preserve A4 tuning");
    require(std::abs(midiNoteToFrequency(81) - 880.0) < 1.0e-10,
        "portable MIDI frequency conversion must preserve octave relationships");

    SynthParameters parameters;
    parameters.osc1Wave = 0;
    parameters.osc2Wave = 0;
    parameters.osc1Level = 1.0f;
    parameters.osc2Level = 0.0f;
    parameters.vco1Enabled = true;
    parameters.vco2Enabled = false;
    parameters.ampEnvEnabled = false;
    parameters.filterEnvEnabled = false;
    parameters.cutoff = 18000.0f;
    parameters.resonance = 0.1f;
    parameters.filterType = 0;
    parameters.unisonVoices = 0;
    parameters.osc1FrequencyRatio = 1.0;
    parameters.osc2FrequencyRatio = 1.0;
    parameters.glideCoefficient = 1.0;
    parameters.unisonDetuneRatios = {1.0, 1.0, 1.0, 1.0};
    parameters.unisonLeftGains = {1.0f, 1.0f, 1.0f, 1.0f};
    parameters.unisonRightGains = {1.0f, 1.0f, 1.0f, 1.0f};

    Voice first;
    Voice second;
    first.prepare(48000.0);
    second.prepare(48000.0);
    first.start(69, 0.8f, 1, 0.0f, 0.0f, parameters, 0.0);
    second.start(69, 0.8f, 1, 0.0f, 0.0f, parameters, 0.0);

    double energy = 0.0;
    float peak = 0.0f;
    for (int frame = 0; frame < 1024; ++frame) {
        float leftA = 0.0f, rightA = 0.0f;
        float leftB = 0.0f, rightB = 0.0f;
        first.render(parameters, 48000.0, leftA, rightA);
        second.render(parameters, 48000.0, leftB, rightB);
        require(std::isfinite(leftA) && std::isfinite(rightA),
            "portable voice output must remain finite");
        require(leftA == leftB && rightA == rightB,
            "identical portable voices must render deterministically");
        energy += static_cast<double>(leftA) * leftA
            + static_cast<double>(rightA) * rightA;
        peak = std::max(peak, std::max(std::abs(leftA), std::abs(rightA)));
    }
    require(energy > 1.0, "prepared note must produce audible output");
    require(peak > 0.01f, "prepared note must produce a non-trivial peak");

    first.stop();
    second.stop();
    for (int frame = 0; frame < 1024; ++frame) {
        float leftA = 0.0f, rightA = 0.0f;
        float leftB = 0.0f, rightB = 0.0f;
        first.render(parameters, 48000.0, leftA, rightA);
        second.render(parameters, 48000.0, leftB, rightB);
        require(std::isfinite(leftA) && std::isfinite(rightA),
            "voice release output must remain finite");
        require(leftA == leftB && rightA == rightB,
            "voice release must remain deterministic");
    }

    std::cout << "PASS: portable voice engine render and lifecycle\n";
    return 0;
}
