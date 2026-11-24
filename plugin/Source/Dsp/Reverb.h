#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace webplaysynth::dsp {

// JUCE 8.0.6 Reverb-compatible FreeVerb network. Call prepare() before use;
// it allocates delay storage, while parameter updates and processing allocate nothing.
class StereoReverb {
public:
    struct Parameters {
        float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.33f;
        float dryLevel = 0.4f, width = 1.0f, freezeMode = 0.0f;
    };

    void prepare(double newSampleRate, const Parameters& initial)
    {
        sampleRate = newSampleRate;
        setParameters(initial);
        constexpr std::array<int, 8> combTunings {1116,1188,1277,1356,1422,1491,1557,1617};
        constexpr std::array<int, 4> allPassTunings {556,441,341,225};
        constexpr int stereoSpread = 23;
        const auto rate = static_cast<int>(sampleRate);
        for (int channel = 0; channel < 2; ++channel) {
            for (size_t i = 0; i < combTunings.size(); ++i)
                comb[static_cast<size_t>(channel)][i].prepare(
                    (rate * (combTunings[i] + (channel == 0 ? 0 : stereoSpread))) / 44100);
            for (size_t i = 0; i < allPassTunings.size(); ++i)
                allPass[static_cast<size_t>(channel)][i].prepare(
                    (rate * (allPassTunings[i] + (channel == 0 ? 0 : stereoSpread))) / 44100);
        }
        const auto rampSamples = static_cast<int>(std::floor(sampleRate * 0.01));
        damping.reset(rampSamples); feedback.reset(rampSamples);
        dryGain.reset(rampSamples); wetGain1.reset(rampSamples); wetGain2.reset(rampSamples);
        reset();
    }

    void setParameters(const Parameters& p) noexcept
    {
        const auto wet = p.wetLevel * 3.0f;
        dryGain.setTargetValue(p.dryLevel * 2.0f);
        wetGain1.setTargetValue(0.5f * wet * (1.0f + p.width));
        wetGain2.setTargetValue(0.5f * wet * (1.0f - p.width));
        gain = p.freezeMode >= 0.5f ? 0.0f : 0.015f;
        parameters = p;
        if (p.freezeMode >= 0.5f) {
            damping.setTargetValue(0.0f);
            feedback.setTargetValue(1.0f);
        } else {
            damping.setTargetValue(p.damping * 0.4f);
            feedback.setTargetValue(p.roomSize * 0.28f + 0.7f);
        }
    }

    const Parameters& getParameters() const noexcept { return parameters; }

    void reset() noexcept
    {
        for (auto& channel : comb) for (auto& filter : channel) filter.clear();
        for (auto& channel : allPass) for (auto& filter : channel) filter.clear();
    }

    void processStereo(float* left, float* right, int count) noexcept
    {
        if (left == nullptr || right == nullptr) return;
        for (int i = 0; i < count; ++i) {
            const auto input = (left[i] + right[i]) * gain;
            float outL = 0.0f, outR = 0.0f;
            const auto damp = damping.getNextValue();
            const auto fb = feedback.getNextValue();
            for (size_t j = 0; j < 8; ++j) {
                outL += comb[0][j].process(input, damp, fb);
                outR += comb[1][j].process(input, damp, fb);
            }
            for (size_t j = 0; j < 4; ++j) {
                outL = allPass[0][j].process(outL);
                outR = allPass[1][j].process(outR);
            }
            const auto dry = dryGain.getNextValue();
            const auto wet1 = wetGain1.getNextValue();
            const auto wet2 = wetGain2.getNextValue();
            left[i] = outL * wet1 + outR * wet2 + left[i] * dry;
            right[i] = outR * wet1 + outL * wet2 + right[i] * dry;
        }
    }

    void processMono(float* samples, int count) noexcept
    {
        if (samples == nullptr) return;
        for (int i = 0; i < count; ++i) {
            const auto input = samples[i] * gain;
            float output = 0.0f;
            const auto damp = damping.getNextValue();
            const auto fb = feedback.getNextValue();
            for (auto& filter : comb[0]) output += filter.process(input, damp, fb);
            for (auto& filter : allPass[0]) output = filter.process(output);
            const auto dry = dryGain.getNextValue();
            const auto wet = wetGain1.getNextValue();
            samples[i] = output * wet + samples[i] * dry;
        }
    }

private:
    class Smoother {
    public:
        void reset(int steps) noexcept { stepsToTarget = steps; current = target; countdown = 0; }
        void setTargetValue(float value) noexcept
        {
            if (std::abs(value - target) <= 1.0e-8f) return;
            if (stepsToTarget <= 0) { current = target = value; countdown = 0; return; }
            target = value;
            countdown = stepsToTarget;
            step = (target - current) / static_cast<float>(countdown);
        }
        float getNextValue() noexcept
        {
            if (countdown <= 0) return target;
            --countdown;
            if (countdown > 0) current += step;
            else current = target;
            return current;
        }
    private:
        float current = 0.0f, target = 0.0f, step = 0.0f;
        int countdown = 0, stepsToTarget = 0;
    };

    class Comb {
    public:
        void prepare(int size) { buffer.assign(static_cast<size_t>(size), 0.0f); index = 0; last = 0.0f; }
        void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); index = 0; last = 0.0f; }
        float process(float input, float damp, float fb) noexcept
        {
            const auto output = buffer[static_cast<size_t>(index)];
            last = output * (1.0f - damp) + last * damp;
            float temp = input + last * fb;
            buffer[static_cast<size_t>(index)] = temp;
            index = (index + 1) % static_cast<int>(buffer.size());
            return output;
        }
    private:
        std::vector<float> buffer;
        int index = 0;
        float last = 0.0f;
    };

    class AllPass {
    public:
        void prepare(int size) { buffer.assign(static_cast<size_t>(size), 0.0f); index = 0; }
        void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); index = 0; }
        float process(float input) noexcept
        {
            const auto buffered = buffer[static_cast<size_t>(index)];
            const auto temp = input + buffered * 0.5f;
            buffer[static_cast<size_t>(index)] = temp;
            index = (index + 1) % static_cast<int>(buffer.size());
            return buffered - input;
        }
    private:
        std::vector<float> buffer;
        int index = 0;
    };

    Parameters parameters;
    float gain = 0.015f;
    std::array<std::array<Comb, 8>, 2> comb;
    std::array<std::array<AllPass, 4>, 2> allPass;
    Smoother damping, feedback, dryGain, wetGain1, wetGain2;
    double sampleRate = 44100.0;
};
} // namespace webplaysynth::dsp
