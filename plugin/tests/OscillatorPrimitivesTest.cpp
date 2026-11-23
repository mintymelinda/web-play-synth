#include "Dsp/Oscillator.h"

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
    using namespace webplaysynth::dsp;
    constexpr double phase = 0.25;
    require(std::abs(waveformSample(phase, 0) - 1.0f) < 1.0e-6f,
        "sine waveform must preserve phase convention");
    require(std::abs(waveformSample(phase, 1) - 0.0f) < 1.0e-6f,
        "triangle waveform must preserve phase convention");
    require(std::abs(waveformSample(phase, 2) + 0.5f) < 1.0e-6f,
        "saw waveform must preserve phase convention");
    require(waveformSample(phase, 3) == 1.0f,
        "pulse waveform must preserve phase convention");
    require(polyBlep(0.0, 0.01) == -1.0f,
        "PolyBLEP must correct the start-of-cycle discontinuity");
    require(polyBlep(0.5, 0.01) == 0.0f,
        "PolyBLEP must be zero away from discontinuities");
    require(polyBlep(0.0, 0.0) == 0.0f,
        "PolyBLEP must safely handle a zero phase increment");

    for (int waveform = 0; waveform < 4; ++waveform) {
        for (int sample = 0; sample < 4096; ++sample) {
            const auto p = static_cast<double>(sample) / 4096.0;
            const auto value = oscillatorSample(p, 0.013, waveform, 0.37f);
            require(std::isfinite(value), "oscillator output must remain finite");
        }
    }
    require(oscillatorSample(0.1, 0.01, 99) == 0.0f,
        "unknown waveform IDs must remain silent");
    std::cout << "PASS: portable oscillator primitives\n";
    return 0;
}
