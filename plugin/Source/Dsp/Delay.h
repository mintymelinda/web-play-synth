#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace webplaysynth::dsp {

// Allocation-free stereo feedback delay. prepare() owns all buffer allocation;
// process() is safe to call from the audio callback without resizing storage.
class StereoDelay {
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto bufferSize = static_cast<size_t>(std::ceil(sampleRate * 2.0)) + 2;
        leftBuffer.assign(bufferSize, 0.0f);
        rightBuffer.assign(bufferSize, 0.0f);
        writePosition = 0;
    }

    void reset() noexcept
    {
        std::fill(leftBuffer.begin(), leftBuffer.end(), 0.0f);
        std::fill(rightBuffer.begin(), rightBuffer.end(), 0.0f);
        writePosition = 0;
    }

    void process(float* left, float* right, int channelCount, int sampleCount,
                 float delayTime, float feedback, float mix) noexcept
    {
        const auto bufferSize = static_cast<int>(leftBuffer.size());
        if (bufferSize <= 2 || channelCount <= 0 || left == nullptr)
            return;

        const auto delaySamples = std::clamp(
            static_cast<int>(std::floor(delayTime * static_cast<float>(sampleRate) + 0.5f)),
            1, bufferSize - 1);
        const auto boundedFeedback = std::clamp(feedback, 0.0f, 0.85f);
        const auto boundedMix = std::clamp(mix, 0.0f, 1.0f);
        const auto stereo = channelCount > 1 && right != nullptr;

        for (int sample = 0; sample < sampleCount; ++sample) {
            auto readPosition = writePosition - delaySamples;
            if (readPosition < 0)
                readPosition += bufferSize;
            const auto delayedLeft = leftBuffer[static_cast<size_t>(readPosition)];
            const auto delayedRight = stereo
                ? rightBuffer[static_cast<size_t>(readPosition)] : delayedLeft;
            const auto inputLeft = left[sample];
            if (stereo) {
                const auto inputRight = right[sample];
                leftBuffer[static_cast<size_t>(writePosition)] =
                    inputLeft + delayedRight * boundedFeedback;
                rightBuffer[static_cast<size_t>(writePosition)] =
                    inputRight + delayedLeft * boundedFeedback;
                left[sample] = inputLeft * (1.0f - boundedMix) + delayedLeft * boundedMix;
                right[sample] = inputRight * (1.0f - boundedMix) + delayedRight * boundedMix;
            } else {
                leftBuffer[static_cast<size_t>(writePosition)] =
                    inputLeft + delayedLeft * boundedFeedback;
                left[sample] = inputLeft * (1.0f - boundedMix) + delayedLeft * boundedMix;
            }
            if (++writePosition >= bufferSize)
                writePosition = 0;
        }
    }

private:
    std::vector<float> leftBuffer, rightBuffer;
    double sampleRate = 44100.0;
    int writePosition = 0;
};

} // namespace webplaysynth::dsp
