#include "Dsp/Delay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Independent copy of the pre-extraction processor loop. This is the behavioral
// reference: it deliberately retains the original buffer sizing, JUCE-style
// positive rounding, cross-channel feedback, and per-sample update order.
class LegacyDelayReference {
public:
    explicit LegacyDelayReference(double sampleRate)
        : left(static_cast<size_t>(std::ceil(sampleRate * 2.0)) + 2, 0.0f),
          right(left.size(), 0.0f), rate(sampleRate)
    {
    }

    void reset()
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        writePosition = 0;
    }

    void process(float* leftSamples, float* rightSamples, int channels, int count,
                 float delayTime, float feedback, float mix)
    {
        const auto bufferSize = static_cast<int>(left.size());
        if (bufferSize <= 2 || channels <= 0)
            return;
        const auto delaySamples = std::clamp(
            static_cast<int>(std::floor(delayTime * static_cast<float>(rate) + 0.5f)),
            1, bufferSize - 1);
        const auto boundedFeedback = std::clamp(feedback, 0.0f, 0.85f);
        const auto boundedMix = std::clamp(mix, 0.0f, 1.0f);
        const auto stereo = channels > 1;
        for (int sample = 0; sample < count; ++sample) {
            auto readPosition = writePosition - delaySamples;
            if (readPosition < 0)
                readPosition += bufferSize;
            const auto delayedLeft = left[static_cast<size_t>(readPosition)];
            const auto delayedRight = stereo
                ? right[static_cast<size_t>(readPosition)] : delayedLeft;
            const auto inputLeft = leftSamples[sample];
            if (stereo) {
                const auto inputRight = rightSamples[sample];
                left[static_cast<size_t>(writePosition)] = inputLeft + delayedRight * boundedFeedback;
                right[static_cast<size_t>(writePosition)] = inputRight + delayedLeft * boundedFeedback;
                leftSamples[sample] = inputLeft * (1.0f - boundedMix) + delayedLeft * boundedMix;
                rightSamples[sample] = inputRight * (1.0f - boundedMix) + delayedRight * boundedMix;
            } else {
                left[static_cast<size_t>(writePosition)] = inputLeft + delayedLeft * boundedFeedback;
                leftSamples[sample] = inputLeft * (1.0f - boundedMix) + delayedLeft * boundedMix;
            }
            if (++writePosition >= bufferSize)
                writePosition = 0;
        }
    }

private:
    std::vector<float> left, right;
    double rate;
    int writePosition = 0;
};

void compareScenario(double sampleRate, int channels)
{
    webplaysynth::dsp::StereoDelay candidate;
    candidate.prepare(sampleRate);
    LegacyDelayReference reference(sampleRate);
    constexpr std::array<int, 7> blockSizes { 1, 17, 64, 127, 256, 3, 511 };
    constexpr std::array<float, 6> times { 0.001f, 0.02f, 0.137f, 0.5f, 1.25f, 2.0f };
    constexpr std::array<float, 5> feedbacks { 0.0f, 0.25f, 0.6f, 0.85f, 1.0f };
    constexpr std::array<float, 5> mixes { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f };

    int globalSample = 0;
    for (int block = 0; block < 180; ++block) {
        const auto count = blockSizes[static_cast<size_t>(block) % blockSizes.size()];
        const auto time = times[static_cast<size_t>(block) % times.size()];
        const auto feedback = feedbacks[static_cast<size_t>(block * 3) % feedbacks.size()];
        const auto mix = mixes[static_cast<size_t>(block * 2) % mixes.size()];
        std::vector<float> actualLeft(static_cast<size_t>(count));
        std::vector<float> expectedLeft(static_cast<size_t>(count));
        std::vector<float> actualRight(static_cast<size_t>(count));
        std::vector<float> expectedRight(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const auto n = globalSample + i;
            const auto left = static_cast<float>(0.47 * std::sin(n * 0.071) + 0.13 * std::cos(n * 0.013));
            const auto right = static_cast<float>(0.31 * std::cos(n * 0.043) - 0.19 * std::sin(n * 0.017));
            actualLeft[static_cast<size_t>(i)] = expectedLeft[static_cast<size_t>(i)] = left;
            actualRight[static_cast<size_t>(i)] = expectedRight[static_cast<size_t>(i)] = right;
        }
        candidate.process(actualLeft.data(), channels > 1 ? actualRight.data() : nullptr,
            channels, count, time, feedback, mix);
        reference.process(expectedLeft.data(), channels > 1 ? expectedRight.data() : nullptr,
            channels, count, time, feedback, mix);
        for (int i = 0; i < count; ++i) {
            require(actualLeft[static_cast<size_t>(i)] == expectedLeft[static_cast<size_t>(i)],
                "portable delay left output must exactly match the original processor loop");
            if (channels > 1)
                require(actualRight[static_cast<size_t>(i)] == expectedRight[static_cast<size_t>(i)],
                    "portable stereo delay right output must exactly match the original cross-feedback loop");
        }
        globalSample += count;
    }

    candidate.reset();
    reference.reset();
    std::array<float, 8> actualLeft { 1.0f, 0, 0, 0, 0, 0, 0, 0 };
    std::array<float, 8> expectedLeft = actualLeft;
    std::array<float, 8> actualRight { 0.5f, 0, 0, 0, 0, 0, 0, 0 };
    std::array<float, 8> expectedRight = actualRight;
    candidate.process(actualLeft.data(), channels > 1 ? actualRight.data() : nullptr,
        channels, static_cast<int>(actualLeft.size()), 0.001f, 0.5f, 1.0f);
    reference.process(expectedLeft.data(), channels > 1 ? expectedRight.data() : nullptr,
        channels, static_cast<int>(expectedLeft.size()), 0.001f, 0.5f, 1.0f);
    require(actualLeft == expectedLeft, "reset must clear delay history and write position");
    if (channels > 1)
        require(actualRight == expectedRight, "stereo reset must clear both delay channels");
}
}

int main()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0 }) {
        compareScenario(rate, 1);
        compareScenario(rate, 2);
    }
    std::cout << "PASS: portable stereo delay matches legacy mono/stereo processing, feedback, automation, and reset\n";
    return 0;
}
