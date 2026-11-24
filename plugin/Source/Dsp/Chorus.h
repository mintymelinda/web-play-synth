#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace webplaysynth::dsp {

// Allocation-free stereo modulated delay chorus. prepare() owns storage;
// process() performs no allocation and preserves the original processor's
// interpolation, quadrature stereo modulation, and phase update order.
class StereoChorus {
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto bufferSize = static_cast<size_t>(std::ceil(sampleRate * 0.05)) + 2;
        leftBuffer.assign(bufferSize, 0.0f);
        rightBuffer.assign(bufferSize, 0.0f);
        writePosition = 0;
        phase = 0.0;
    }

    void reset() noexcept
    {
        std::fill(leftBuffer.begin(), leftBuffer.end(), 0.0f);
        std::fill(rightBuffer.begin(), rightBuffer.end(), 0.0f);
        writePosition = 0;
        phase = 0.0;
    }

    void process(float* left, float* right, int channelCount, int sampleCount,
                 float rate, float depth, float mix) noexcept
    {
        const auto bufferSize = static_cast<int>(leftBuffer.size());
        if (bufferSize <= 2 || channelCount <= 0 || left == nullptr)
            return;

        const auto boundedMix = std::clamp(mix, 0.0f, 1.0f);
        const auto depthSeconds = 0.008f * std::clamp(depth, 0.0f, 1.0f);
        const auto boundedRate = std::clamp(rate, 0.1f, 5.0f);
        const auto stereo = channelCount > 1 && right != nullptr;
        constexpr float baseDelaySeconds = 0.020f;
        constexpr double halfPi = 1.57079632679489661923;
        constexpr double twoPi = 6.28318530717958647692;

        for (int sample = 0; sample < sampleCount; ++sample) {
            const auto inputLeft = left[sample];
            const auto inputRight = stereo ? right[sample] : inputLeft;
            leftBuffer[static_cast<size_t>(writePosition)] = inputLeft;
            rightBuffer[static_cast<size_t>(writePosition)] = inputRight;

            const auto modLeft = std::sin(phase);
            const auto modRight = stereo ? std::sin(phase + halfPi) : modLeft;
            const auto delayLeft = (baseDelaySeconds + depthSeconds * modLeft) * sampleRate;
            const auto delayRight = (baseDelaySeconds + depthSeconds * modRight) * sampleRate;
            const auto wetLeft = readWet(leftBuffer, delayLeft, bufferSize);
            const auto wetRight = stereo ? readWet(rightBuffer, delayRight, bufferSize) : wetLeft;

            left[sample] = inputLeft * (1.0f - boundedMix) + wetLeft * boundedMix;
            if (stereo)
                right[sample] = inputRight * (1.0f - boundedMix) + wetRight * boundedMix;

            phase += boundedRate / sampleRate;
            if (phase >= twoPi)
                phase -= twoPi;
            if (++writePosition >= bufferSize)
                writePosition = 0;
        }
    }

private:
    float readWet(const std::vector<float>& buffer, double delaySamples, int bufferSize) const noexcept
    {
        const auto wholeDelay = static_cast<int>(std::floor(delaySamples));
        const auto fraction = static_cast<float>(delaySamples - wholeDelay);
        auto first = writePosition - wholeDelay;
        while (first < 0)
            first += bufferSize;
        auto second = first - 1;
        if (second < 0)
            second += bufferSize;
        const auto a = buffer[static_cast<size_t>(first)];
        const auto b = buffer[static_cast<size_t>(second)];
        return a + (b - a) * fraction;
    }

    std::vector<float> leftBuffer, rightBuffer;
    double sampleRate = 44100.0;
    double phase = 0.0;
    int writePosition = 0;
};

} // namespace webplaysynth::dsp
