#include "Dsp/Chorus.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
constexpr double halfPi = 1.57079632679489661923;
constexpr double twoPi = 6.28318530717958647692;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Independent transcription of the pre-extraction processor chorus loop.
class LegacyChorusReference {
public:
    explicit LegacyChorusReference(double rate)
        : left(static_cast<size_t>(std::ceil(rate * 0.05)) + 2, 0.0f),
          right(left.size(), 0.0f), sampleRate(rate)
    {
    }

    void reset()
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        writePosition = 0;
        phase = 0.0;
    }

    void process(float* l, float* r, int channels, int count,
                 float chorusRate, float depth, float mix)
    {
        const auto size = static_cast<int>(left.size());
        if (size <= 2 || channels <= 0)
            return;
        const auto boundedMix = std::clamp(mix, 0.0f, 1.0f);
        const auto depthSeconds = 0.008f * std::clamp(depth, 0.0f, 1.0f);
        const auto boundedRate = std::clamp(chorusRate, 0.1f, 5.0f);
        const auto stereo = channels > 1;
        constexpr float baseDelaySeconds = 0.020f;
        for (int sample = 0; sample < count; ++sample) {
            const auto inputLeft = l[sample];
            const auto inputRight = stereo ? r[sample] : inputLeft;
            left[static_cast<size_t>(writePosition)] = inputLeft;
            right[static_cast<size_t>(writePosition)] = inputRight;
            const auto modLeft = std::sin(phase);
            const auto modRight = stereo ? std::sin(phase + halfPi) : modLeft;
            const auto delayLeft = (baseDelaySeconds + depthSeconds * modLeft) * sampleRate;
            const auto delayRight = (baseDelaySeconds + depthSeconds * modRight) * sampleRate;
            const auto wetLeft = readWet(left, delayLeft, size);
            const auto wetRight = stereo ? readWet(right, delayRight, size) : wetLeft;
            l[sample] = inputLeft * (1.0f - boundedMix) + wetLeft * boundedMix;
            if (stereo)
                r[sample] = inputRight * (1.0f - boundedMix) + wetRight * boundedMix;
            phase += boundedRate / sampleRate;
            if (phase >= twoPi)
                phase -= twoPi;
            if (++writePosition >= size)
                writePosition = 0;
        }
    }

private:
    float readWet(const std::vector<float>& buffer, double delaySamples, int size) const
    {
        const auto whole = static_cast<int>(std::floor(delaySamples));
        const auto fraction = static_cast<float>(delaySamples - whole);
        auto first = writePosition - whole;
        while (first < 0)
            first += size;
        auto second = first - 1;
        if (second < 0)
            second += size;
        const auto a = buffer[static_cast<size_t>(first)];
        const auto b = buffer[static_cast<size_t>(second)];
        return a + (b - a) * fraction;
    }

    std::vector<float> left, right;
    double sampleRate;
    double phase = 0.0;
    int writePosition = 0;
};

void compare(double sampleRate, int channels)
{
    webplaysynth::dsp::StereoChorus actual;
    actual.prepare(sampleRate);
    LegacyChorusReference expected(sampleRate);
    constexpr std::array<int, 7> blocks { 1, 17, 64, 127, 256, 3, 511 };
    constexpr std::array<float, 6> rates { 0.1f, 0.25f, 0.8f, 2.0f, 4.5f, 5.0f };
    constexpr std::array<float, 5> depths { 0.0f, 0.1f, 0.5f, 0.9f, 1.0f };
    constexpr std::array<float, 5> mixes { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f };
    int globalSample = 0;
    for (int block = 0; block < 1200; ++block) {
        const auto count = blocks[static_cast<size_t>(block) % blocks.size()];
        const auto rate = rates[static_cast<size_t>(block * 3) % rates.size()];
        const auto depth = depths[static_cast<size_t>(block * 2) % depths.size()];
        const auto mix = mixes[static_cast<size_t>(block * 4) % mixes.size()];
        std::vector<float> actualLeft(static_cast<size_t>(count));
        std::vector<float> expectedLeft(static_cast<size_t>(count));
        std::vector<float> actualRight(static_cast<size_t>(count));
        std::vector<float> expectedRight(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const auto n = globalSample + i;
            const auto l = static_cast<float>(0.47 * std::sin(n * 0.071) + 0.13 * std::cos(n * 0.013));
            const auto r = static_cast<float>(0.31 * std::cos(n * 0.043) - 0.19 * std::sin(n * 0.017));
            actualLeft[static_cast<size_t>(i)] = expectedLeft[static_cast<size_t>(i)] = l;
            actualRight[static_cast<size_t>(i)] = expectedRight[static_cast<size_t>(i)] = r;
        }
        actual.process(actualLeft.data(), channels > 1 ? actualRight.data() : nullptr,
            channels, count, rate, depth, mix);
        expected.process(expectedLeft.data(), channels > 1 ? expectedRight.data() : nullptr,
            channels, count, rate, depth, mix);
        for (int i = 0; i < count; ++i) {
            require(actualLeft[static_cast<size_t>(i)] == expectedLeft[static_cast<size_t>(i)],
                "portable chorus left output must exactly match the original loop");
            if (channels > 1)
                require(actualRight[static_cast<size_t>(i)] == expectedRight[static_cast<size_t>(i)],
                    "portable chorus right output must exactly match quadrature modulation");
        }
        globalSample += count;
    }

    actual.reset();
    expected.reset();
    std::array<float, 16> actualLeft { 1.0f };
    auto expectedLeft = actualLeft;
    std::array<float, 16> actualRight { 0.5f };
    auto expectedRight = actualRight;
    actual.process(actualLeft.data(), channels > 1 ? actualRight.data() : nullptr,
        channels, static_cast<int>(actualLeft.size()), 0.8f, 0.5f, 1.0f);
    expected.process(expectedLeft.data(), channels > 1 ? expectedRight.data() : nullptr,
        channels, static_cast<int>(expectedLeft.size()), 0.8f, 0.5f, 1.0f);
    require(actualLeft == expectedLeft, "reset must clear chorus history, phase, and write position");
    if (channels > 1)
        require(actualRight == expectedRight, "stereo reset must clear both chorus channels");
}
}

int main()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 }) {
        compare(rate, 1);
        compare(rate, 2);
    }
    std::cout << "PASS: portable chorus exactly matches legacy mono/stereo processing, modulation, automation, and reset\n";
    return 0;
}
