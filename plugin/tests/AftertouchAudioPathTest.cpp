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
    require(parameter != nullptr, "expected aftertouch parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

std::vector<float> render(bool channelPressure, bool polyPressure)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "aftertouchDepth", 1.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output(2, 256);
    std::vector<float> samples;
    samples.reserve(10 * 256);

    for (int block = 0; block < 12; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        if (block == 1 && channelPressure)
            midi.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);
        if (block == 1 && polyPressure)
            midi.addEvent(juce::MidiMessage::aftertouchChange(1, 69, 127), 0);

        processor.processBlock(output, midi);
        if (block >= 2) {
            for (int sample = 0; sample < output.getNumSamples(); ++sample) {
                const auto value = output.getSample(0, sample);
                require(std::isfinite(value), "aftertouch output must remain finite");
                samples.push_back(value);
            }
        }
    }

    processor.releaseResources();
    return samples;
}

double differenceEnergy(const std::vector<float>& baseline, const std::vector<float>& expressed)
{
    require(baseline.size() == expressed.size(), "aftertouch renders must have equal lengths");
    double result = 0.0;
    for (size_t index = 0; index < baseline.size(); ++index) {
        const auto difference = baseline[index] - expressed[index];
        result += static_cast<double>(difference) * difference;
    }
    return result;
}
}

int main()
{
    const auto baseline = render(false, false);
    const auto channelExpressed = render(true, false);
    const auto polyExpressed = render(false, true);

    require(differenceEnergy(baseline, channelExpressed) > 0.1,
        "channel pressure must alter active voice output when aftertouch depth is enabled");
    require(differenceEnergy(baseline, polyExpressed) > 0.1,
        "polyphonic key pressure must alter its matching voice output");

    std::cout << "PASS: channel pressure and polyphonic aftertouch audio paths\n";
    return 0;
}
