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
    require(parameter != nullptr, "expected synth parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

std::vector<float> renderHardSync(int unisonMode)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Level", 0.0f);
    setParameter(processor, "osc2Level", 1.0f);
    setParameter(processor, "osc2Tune", 12.0f);
    setParameter(processor, "hardSync", 1.0f);
    setParameter(processor, "unisonVoices", static_cast<float>(unisonMode));
    setParameter(processor, "unisonDetune", 25.0f);
    setParameter(processor, "stereoSpread", 0.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> samples;
    samples.reserve(144 * 512);

    for (int block = 0; block < 160; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        if (block < 16)
            continue;

        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(0, sample);
            require(std::isfinite(value), "hard-sync unison output must remain finite");
            samples.push_back(value);
        }
    }

    processor.releaseResources();
    return samples;
}
}

int main()
{
    const auto singleVoice = renderHardSync(0);
    const auto fourVoices = renderHardSync(2);
    require(singleVoice.size() == fourVoices.size(), "rendered comparison buffers must have equal lengths");

    double normalizedDifference = 0.0;
    double referenceEnergy = 0.0;
    for (size_t sample = 0; sample < singleVoice.size(); ++sample) {
        // Four centered layers each contribute half gain. If their phases all
        // advance identically, the result is exactly twice the single-layer
        // render. Detuned hard-sync layers must diverge from that reference.
        const auto difference = fourVoices[sample] - 2.0f * singleVoice[sample];
        normalizedDifference += static_cast<double>(difference) * difference;
        referenceEnergy += static_cast<double>(fourVoices[sample]) * fourVoices[sample];
    }

    require(referenceEnergy > 0.1, "hard-sync render must contain audible signal");
    require(normalizedDifference > 0.01,
        "hard-sync unison layers must use their detuned master frequencies");

    std::cout << "PASS: hard-sync unison detuning and finite rendered audio\n";
    return 0;
}
