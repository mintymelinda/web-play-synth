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
    require(parameter != nullptr, "expected filter test parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

std::vector<float> renderFilter(int filterType, float drive, float resonance = 0.7f, float cutoff = 1800.0f)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", cutoff);
    setParameter(processor, "resonance", resonance);
    setParameter(processor, "filterContour", 0.0f);
    setParameter(processor, "filterType", static_cast<float>(filterType));
    setParameter(processor, "filterDrive", drive);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> samples;
    samples.reserve(96 * 512);

    for (int block = 0; block < 96; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(0, sample);
            require(std::isfinite(value), "filter output must remain finite");
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
    require(first.size() == second.size(), "filter renders must have equal lengths");
    double result = 0.0;
    for (size_t index = 0; index < first.size(); ++index) {
        const auto difference = first[index] - second[index];
        result += static_cast<double>(difference) * difference;
    }
    return result;
}
}

int main()
{
    const auto lowPass = renderFilter(0, 0.0f);
    const auto highPass = renderFilter(1, 0.0f);
    const auto bandPass = renderFilter(2, 0.0f);

    require(energy(lowPass) > energy(highPass) * 4.0,
        "high-pass mode must reject a 440 Hz fundamental below its 1800 Hz cutoff");
    require(energy(bandPass) > 0.001,
        "band-pass mode must produce a finite, non-silent filtered signal");
    require(differenceEnergy(lowPass, bandPass) > 0.01,
        "band-pass mode must differ from low-pass mode in rendered audio");

    const auto bandPassLowQ = renderFilter(2, 0.0f, 0.7f, 440.0f);
    const auto bandPassHighQ = renderFilter(2, 0.0f, 18.0f, 440.0f);
    const auto lowQEnergy = energy(bandPassLowQ);
    const auto highQEnergy = energy(bandPassHighQ);
    require(highQEnergy > lowQEnergy * 0.7 && highQEnergy < lowQEnergy * 1.3,
        "band-pass resonance must change bandwidth without introducing Q-dependent output gain");

    const auto clean = renderFilter(0, 0.0f);
    const auto driven = renderFilter(0, 12.0f);
    require(differenceEnergy(clean, driven) > 0.01,
        "filter drive must change the rendered waveform");

    std::cout << "PASS: low-pass, high-pass, band-pass, and filter-drive audio paths\n";
    return 0;
}
