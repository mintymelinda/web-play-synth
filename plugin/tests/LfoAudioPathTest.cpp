#include "PluginProcessor.h"

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

void setParameter(WebPlaySynthAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, "expected LFO test parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

std::vector<float> renderSignal(float pitchDepth, float filterDepth)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", 100.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "filterContour", 0.0f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "lfoWave", 0.0f);
    setParameter(processor, "lfoRate", 4.5f);
    setParameter(processor, "lfoPitchDepth", pitchDepth);
    setParameter(processor, "lfoFilterDepth", filterDepth);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> samples;
    samples.reserve(160 * 512);

    for (int block = 0; block < 160; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(0, sample);
            require(std::isfinite(value), "LFO-modulated audio must remain finite");
            samples.push_back(value);
        }
    }

    processor.releaseResources();
    return samples;
}

double energy(const std::vector<float>& samples)
{
    double result = 0.0;
    for (const auto sample : samples)
        result += static_cast<double>(sample) * sample;
    return result;
}

double differenceEnergy(const std::vector<float>& first, const std::vector<float>& second)
{
    require(first.size() == second.size(), "rendered LFO buffers must have equal lengths");
    double result = 0.0;
    for (size_t index = 0; index < first.size(); ++index) {
        const auto delta = first[index] - second[index];
        result += static_cast<double>(delta) * delta;
    }
    return result;
}
}

int main()
{
    const auto steadyPitch = renderSignal(0.0f, 0.0f);
    const auto modulatedPitch = renderSignal(12.0f, 0.0f);
    require(differenceEnergy(steadyPitch, modulatedPitch) > 0.01,
        "LFO pitch depth must modulate the rendered oscillator frequency");

    const auto closedFilter = renderSignal(0.0f, 0.0f);
    const auto modulatedFilter = renderSignal(0.0f, 4000.0f);
    require(energy(modulatedFilter) > energy(closedFilter) * 2.0,
        "LFO filter depth must audibly open the filter over its modulation cycle");

    std::cout << "PASS: LFO pitch and filter destinations affect rendered audio\n";
    return 0;
}
