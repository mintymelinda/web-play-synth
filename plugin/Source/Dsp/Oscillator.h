#pragma once

#include <algorithm>
#include <cmath>

namespace webplaysynth::dsp {

inline float polyBlep(double phase, double increment) noexcept
{
    if (increment <= 0.0)
        return 0.0f;
    if (phase < increment) {
        const auto x = phase / increment;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (phase > 1.0 - increment) {
        const auto x = (phase - 1.0) / increment;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

inline float waveformSample(double phase, int waveform) noexcept
{
    constexpr double twoPi = 6.283185307179586476925286766559;
    switch (waveform) {
        case 0: return static_cast<float>(std::sin(phase * twoPi));
        case 1: return static_cast<float>(1.0 - 4.0 * std::abs(phase - 0.5));
        case 2: return static_cast<float>(2.0 * phase - 1.0);
        case 3: return phase < 0.5 ? 1.0f : -1.0f;
        default: return 0.0f;
    }
}

inline float oscillatorSample(double phase, double increment, int waveform,
                              float pulseWidth = 0.5f) noexcept
{
    if (waveform == 3) {
        const auto width = std::clamp(pulseWidth, 0.05f, 0.95f);
        auto sample = phase < width ? 1.0f : -1.0f;
        sample += polyBlep(phase, increment);
        auto pulseEdge = phase - width;
        if (pulseEdge < 0.0)
            pulseEdge += 1.0;
        sample -= polyBlep(pulseEdge, increment);
        return sample;
    }

    auto sample = waveformSample(phase, waveform);
    if (waveform == 2)
        sample -= polyBlep(phase, increment);
    return sample;
}

} // namespace webplaysynth::dsp
