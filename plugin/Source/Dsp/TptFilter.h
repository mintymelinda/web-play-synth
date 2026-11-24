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
// filter. The state update and output equations intentionally mirror JUCE's
// implementation so the primitive can be shared with the future WASM core.
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
        cutoff = newCutoff;
        updateCoefficients();
    }

    void setResonance(float newResonance) noexcept
    {
        resonance = newResonance;
        updateCoefficients();
    }

    void reset() noexcept
    {
        state1 = 0.0f;
        state2 = 0.0f;
    }

    float processSample(float input) noexcept
    {
        const auto highpass = h * (input - state1 * (g + r2) - state2);
        const auto bandpass = highpass * g + state1;
        state1 = highpass * g + bandpass;

        const auto lowpass = bandpass * g + state2;
        state2 = bandpass * g + lowpass;

        switch (type) {
            case TptFilterType::highpass:
                return highpass;
            case TptFilterType::bandpass:
                return bandpass;
            case TptFilterType::lowpass:
            default:
                return lowpass;
        }
    }

private:
    void updateCoefficients() noexcept
    {
        if (sampleRate <= 0.0)
            return;

        // Preserve JUCE's precision and conversion points: tan() is evaluated
        // in double precision, then each coefficient is stored as float.
        g = static_cast<float>(std::tan(3.14159265358979323846 * cutoff / sampleRate));
        r2 = static_cast<float>(1.0 / resonance);
        h = static_cast<float>(1.0 / (1.0 + r2 * g + g * g));
    }

    double sampleRate = 44100.0;
    float cutoff = 1000.0f;
    float resonance = 0.7071067811865475f;
    float g = 0.0f;
    float h = 1.0f;
    float r2 = 1.4142135623730951f;
    float state1 = 0.0f;
    float state2 = 0.0f;
    TptFilterType type = TptFilterType::lowpass;
};

} // namespace webplaysynth::dsp
