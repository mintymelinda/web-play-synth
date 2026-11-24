#pragma once

#include <algorithm>
#include <cmath>

namespace webplaysynth::dsp {

enum class TptFilterType {
    lowpass,
    highpass,
    bandpass
};

// JUCE StateVariableTPTFilter-compatible topology-preserving state-variable
// filter. This header is deliberately independent of JUCE so the same DSP
// primitive can be compiled into the native plugin and the future WASM core.
class TptFilter {
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficients();
        reset();
    }

    void setType(TptFilterType newType) noexcept
    {
        type = newType;
    }

    void setCutoffFrequency(float newCutoff) noexcept
    {
        cutoff = std::clamp(newCutoff, 1.0f, static_cast<float>(sampleRate * 0.5));
        updateCoefficients();
    }

    void setResonance(float newResonance) noexcept
    {
        resonance = newResonance;
        updateCoefficients();
    }

    void reset() noexcept
    {
        integrator1 = 0.0f;
        integrator2 = 0.0f;
    }

    float processSample(float input) noexcept
    {
        const auto v1 = h * (integrator1 + g * (input - integrator2));
        const auto v2 = integrator2 + g * v1;
        integrator1 = 2.0f * v1 - integrator1;
        integrator2 = 2.0f * v2 - integrator2;

        switch (type) {
            case TptFilterType::highpass:
                return input - r2 * v1 - v2;
            case TptFilterType::bandpass:
                return v1;
            case TptFilterType::lowpass:
            default:
                return v2;
        }
    }

private:
    void updateCoefficients() noexcept
    {
        if (sampleRate <= 0.0)
            return;

        r2 = 1.0f / resonance;
        g = std::tan(3.14159265358979323846f * cutoff / sampleRate);
        h = 1.0f / (1.0f + r2 * g + g * g);
    }

    double sampleRate = 44100.0;
    float cutoff = 1000.0f;
    float resonance = 1.0f;
    float g = 0.0f;
    float h = 1.0f;
    float r2 = 1.0f;
    float integrator1 = 0.0f;
    float integrator2 = 0.0f;
    TptFilterType type = TptFilterType::lowpass;
};

} // namespace webplaysynth::dsp
