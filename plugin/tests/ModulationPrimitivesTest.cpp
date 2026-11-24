#include "Dsp/Modulation.h"

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

bool same(float actual, float expected)
{
    return actual == expected;
}

void testFrequencyModulationPreservesLegacyEquation()
{
    constexpr std::array<double, 3> baseFrequencies { 55.0, 440.0, 12000.0 };
    constexpr std::array<double, 3> detuneRatios { 0.5, 1.0, 1.125 };
    constexpr std::array<float, 5> lfoValues { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    constexpr std::array<float, 4> depths { 0.0f, 1.0f, 12.0f, 25.0f };

    for (const auto base : baseFrequencies) {
        for (const auto ratio : detuneRatios) {
            for (const auto lfo : lfoValues) {
                for (const auto depth : depths) {
                    const auto expected = base * ratio + static_cast<double>(lfo * depth);
                    require(webplaysynth::dsp::modulateFrequency(base, ratio, lfo, depth) == expected,
                        "portable pitch modulation must preserve the legacy additive-Hz equation");
                }
            }
        }
    }
}

void testPulseWidthAndPanModulation()
{
    constexpr std::array<float, 5> lfoValues { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    constexpr std::array<float, 4> depths { 0.0f, 0.1f, 0.4f, 1.0f };
    constexpr std::array<float, 3> pulseWidths { 0.05f, 0.5f, 0.95f };
    constexpr std::array<float, 3> panPositions { -1.0f, 0.0f, 1.0f };

    for (const auto lfo : lfoValues) {
        for (const auto depth : depths) {
            for (const auto pulseWidth : pulseWidths) {
                const auto expected = std::clamp(pulseWidth + static_cast<float>(lfo) * depth,
                    0.05f, 0.95f);
                require(same(webplaysynth::dsp::modulatePulseWidth(pulseWidth, lfo, depth), expected),
                    "portable pulse-width modulation must preserve the legacy clamp and equation");
            }
            for (const auto pan : panPositions) {
                const auto expected = std::clamp(pan + static_cast<float>(lfo * depth), -1.0f, 1.0f);
                require(same(webplaysynth::dsp::modulatePan(pan, lfo, depth), expected),
                    "portable pan modulation must preserve the legacy clamp and equation");
            }
        }
    }
}

void testFilterCutoffAndAmplitudeModulation()
{
    constexpr std::array<float, 5> lfoValues { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    constexpr std::array<float, 4> depths { 0.0f, 500.0f, 3000.0f, 5000.0f };
    constexpr std::array<float, 3> cutoffs { 30.0f, 1200.0f, 18000.0f };
    constexpr std::array<float, 3> contours { 0.0f, 0.55f, 1.0f };
    constexpr std::array<float, 3> envelopes { 0.0f, 0.35f, 1.0f };
    constexpr std::array<float, 4> ampDepths { 0.0f, 0.25f, 0.65f, 1.0f };

    for (const auto lfo : lfoValues) {
        for (const auto depth : depths) {
            for (const auto cutoff : cutoffs) {
                for (const auto contour : contours) {
                    for (const auto envelope : envelopes) {
                        const auto expected = std::clamp(cutoff
                            * static_cast<float>(std::pow(2.0, contour * envelope * 3.0))
                            + lfo * depth, 30.0f, 19000.0f);
                        require(same(webplaysynth::dsp::modulateFilterCutoff(
                                cutoff, envelope, contour, lfo, depth), expected),
                            "portable filter modulation must preserve envelope, LFO, and cutoff clamp order");
                    }
                }
            }
            for (const auto ampDepth : ampDepths) {
                const auto expected = (1.0f + static_cast<float>(lfo) * ampDepth)
                    / (1.0f + ampDepth);
                require(same(webplaysynth::dsp::modulateAmplitude(lfo, ampDepth), expected),
                    "portable amplitude modulation must preserve normalized legacy gain");
            }
        }
    }
}
}

int main()
{
    testFrequencyModulationPreservesLegacyEquation();
    testPulseWidthAndPanModulation();
    testFilterCutoffAndAmplitudeModulation();
    std::cout << "PASS: portable LFO destination modulation primitives preserve legacy equations\n";
    return 0;
}
