#include "Dsp/TptFilter.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
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

juce::dsp::StateVariableTPTFilterType juceType(webplaysynth::dsp::TptFilterType type)
{
    switch (type) {
        case webplaysynth::dsp::TptFilterType::highpass:
            return juce::dsp::StateVariableTPTFilterType::highpass;
        case webplaysynth::dsp::TptFilterType::bandpass:
            return juce::dsp::StateVariableTPTFilterType::bandpass;
        case webplaysynth::dsp::TptFilterType::lowpass:
        default:
            return juce::dsp::StateVariableTPTFilterType::lowpass;
    }
}

void compareScenario(double sampleRate, webplaysynth::dsp::TptFilterType type)
{
    constexpr int blockSize = 127;
    constexpr int blockCount = 96;
    constexpr float tolerance = 1.0e-6f;

    webplaysynth::dsp::TptFilter candidate;
    candidate.prepare(sampleRate);
    candidate.setType(type);

    juce::dsp::StateVariableTPTFilter<float> reference;
    reference.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), 1 });
    reference.setType(juceType(type));
    reference.reset();

    double sumSquaredError = 0.0;
    double signedError = 0.0;
    float maximumError = 0.0f;
    int maximumErrorSample = 0;
    int sampleIndex = 0;

    for (int block = 0; block < blockCount; ++block) {
        // Exercise coefficient updates at block boundaries as well as
        // continuous cutoff/resonance automation within the render.
        if (block % 8 == 0) {
            const auto cutoff = block % 16 == 0 ? 320.0f : 12500.0f;
            const auto resonance = block % 24 == 0 ? 0.1f : 18.0f;
            candidate.setCutoffFrequency(cutoff);
            candidate.setResonance(resonance);
            reference.setCutoffFrequency(cutoff);
            reference.setResonance(resonance);
        }

        for (int offset = 0; offset < blockSize; ++offset, ++sampleIndex) {
            if (offset == 41) {
                const auto cutoff = 80.0f + static_cast<float>((block * 173) % 17000);
                const auto resonance = 0.1f + static_cast<float>((block * 7) % 179) / 10.0f;
                candidate.setCutoffFrequency(cutoff);
                candidate.setResonance(resonance);
                reference.setCutoffFrequency(cutoff);
                reference.setResonance(resonance);
            }

            const auto time = static_cast<double>(sampleIndex) / sampleRate;
            const auto input = static_cast<float>(
                0.61 * std::sin(2.0 * juce::MathConstants<double>::pi * 173.0 * time)
                + 0.27 * std::sin(2.0 * juce::MathConstants<double>::pi * 2711.0 * time)
                + 0.09 * std::cos(2.0 * juce::MathConstants<double>::pi * 8111.0 * time));

            const auto expected = reference.processSample(0, input);
            const auto actual = candidate.processSample(input);
            const auto error = actual - expected;
            const auto absoluteError = std::abs(error);
            signedError += error;
            sumSquaredError += static_cast<double>(error) * error;
            if (absoluteError > maximumError) {
                maximumError = absoluteError;
                maximumErrorSample = sampleIndex;
            }
            require(std::isfinite(actual), "portable TPT filter output must remain finite");
        }
    }

    const auto sampleCount = static_cast<double>(blockCount * blockSize);
    const auto rmsError = std::sqrt(sumSquaredError / sampleCount);
    const auto meanError = signedError / sampleCount;
    std::cout << "TPT parity: sampleRate=" << sampleRate
              << ", mode=" << static_cast<int>(type)
              << ", mean=" << meanError
              << ", rms=" << rmsError
              << ", max=" << maximumError
              << " at " << maximumErrorSample << '\n';

    require(maximumError <= tolerance,
        "portable TPT filter must match JUCE sample-for-sample within 1e-6");
}

void testResetClearsFilterState()
{
    webplaysynth::dsp::TptFilter candidate;
    candidate.prepare(48000.0);
    candidate.setCutoffFrequency(2200.0f);
    candidate.setResonance(4.0f);
    candidate.setType(webplaysynth::dsp::TptFilterType::lowpass);
    for (int sample = 0; sample < 512; ++sample)
        candidate.processSample(sample == 0 ? 1.0f : 0.0f);
    candidate.reset();
    require(candidate.processSample(0.0f) == 0.0f,
        "portable TPT reset must clear both integrator states");
}

void testAllModesAndSampleRates()
{
    constexpr double sampleRates[] { 44100.0, 48000.0, 96000.0 };
    constexpr webplaysynth::dsp::TptFilterType modes[] {
        webplaysynth::dsp::TptFilterType::lowpass,
        webplaysynth::dsp::TptFilterType::highpass,
        webplaysynth::dsp::TptFilterType::bandpass
    };

    for (const auto sampleRate : sampleRates)
        for (const auto mode : modes)
            compareScenario(sampleRate, mode);
}
}

int main()
{
    testResetClearsFilterState();
    testAllModesAndSampleRates();
    std::cout << "PASS: portable TPT filter matches JUCE across modes, sample rates, and automation\n";
    return 0;
}
