#include "PluginProcessor.h"

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

void setParameter(WebPlaySynthAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, "expected LFO destination parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct RenderMetrics {
    double energy = 0.0;
    double stereoDifferenceEnergy = 0.0;
    double leftRightEnergyDifference = 0.0;
};

RenderMetrics render(float ampDepth, float panDepth)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "lfoWave", 0.0f);
    setParameter(processor, "lfoRate", 4.5f);
    setParameter(processor, "lfoAmpDepth", ampDepth);
    setParameter(processor, "lfoPanDepth", panDepth);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    RenderMetrics result;

    for (int block = 0; block < 128; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto left = output.getSample(0, sample);
            const auto right = output.getSample(1, sample);
            require(std::isfinite(left) && std::isfinite(right),
                "LFO amplitude and pan output must remain finite");
            result.energy += static_cast<double>(left) * left
                + static_cast<double>(right) * right;
            const auto difference = left - right;
            result.stereoDifferenceEnergy += static_cast<double>(difference) * difference;
            result.leftRightEnergyDifference += static_cast<double>(left) * left
                - static_cast<double>(right) * right;
        }
    }

    processor.releaseResources();
    return result;
}
}

int main()
{
    const auto unmodulated = render(0.0f, 0.0f);
    const auto amplitudeModulated = render(0.85f, 0.0f);
    require(std::abs(amplitudeModulated.energy - unmodulated.energy) > 0.01,
        "LFO amplitude depth must change rendered output energy");

    const auto centered = render(0.0f, 0.0f);
    const auto panned = render(0.0f, 1.0f);
    require(centered.stereoDifferenceEnergy < 0.000001,
        "a centered voice without pan modulation must remain centered");
    require(panned.stereoDifferenceEnergy > 0.01,
        "LFO pan depth must create time-varying stereo separation");

    std::cout << "PASS: LFO amplitude and stereo-pan destinations affect rendered audio\n";
    return 0;
}
