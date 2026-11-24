#pragma once

#include <algorithm>
#include <cmath>

namespace webplaysynth::dsp {

// These helpers preserve the native voice's established modulation equations.
// Keep them independent of JUCE so the same DSP behavior can be shared by the
// plugin and the future browser/WASM engine.
inline double modulateFrequency(double baseFrequency, double detuneRatio,
                                float lfo, float depth) noexcept
{
    return baseFrequency * detuneRatio + static_cast<double>(lfo * depth);
}

inline float modulatePulseWidth(float basePulseWidth, float lfo, float depth) noexcept
{
    return std::clamp(basePulseWidth + static_cast<float>(lfo) * depth, 0.05f, 0.95f);
}

inline float modulatePan(float basePan, float lfo, float depth) noexcept
{
    return std::clamp(basePan + static_cast<float>(lfo * depth), -1.0f, 1.0f);
}

inline float modulateFilterCutoff(float smoothedCutoff, float filterEnvelope,
                                  float contour, float lfo, float depth) noexcept
{
    const auto envelopeMultiplier =
        static_cast<float>(std::pow(2.0, contour * filterEnvelope * 3.0));
    return std::clamp(smoothedCutoff * envelopeMultiplier + lfo * depth, 30.0f, 19000.0f);
}

inline float modulateAmplitude(float lfo, float depth) noexcept
{
    return (1.0f + static_cast<float>(lfo) * depth) / (1.0f + depth);
}

} // namespace webplaysynth::dsp
