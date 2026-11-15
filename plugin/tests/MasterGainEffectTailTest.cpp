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
    require(parameter != nullptr, "expected master/effect test parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float renderBlock(WebPlaySynthAudioProcessor& processor, juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> output(2, 512);
    processor.processBlock(output, midi);
    float peak = 0.0f;
    for (int channel = 0; channel < output.getNumChannels(); ++channel) {
        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(channel, sample);
            require(std::isfinite(value), "effect output must remain finite");
            peak = juce::jmax(peak, std::abs(value));
        }
    }
    return peak;
}
}

int main()
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "master", 1.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "delayTime", 0.05f);
    setParameter(processor, "delayFeedback", 0.6f);
    setParameter(processor, "delayMix", 1.0f);
    setParameter(processor, "chorusMix", 0.0f);
    setParameter(processor, "reverbMix", 0.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
    const auto audibleDelay = renderBlock(processor, midi);
    require(audibleDelay > 0.001f,
        "wet-only delay must produce audible output before the master is muted");

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    renderBlock(processor, midi);

    setParameter(processor, "master", 0.0f);
    midi.clear();
    float mutedTailPeak = 0.0f;
    for (int block = 0; block < 24; ++block)
        mutedTailPeak = juce::jmax(mutedTailPeak, renderBlock(processor, midi));

    require(mutedTailPeak < 0.000001f,
        "master level at zero must mute stored delay/effect tails, not only new voice input");

    processor.releaseResources();
    std::cout << "PASS: master output gain controls stored effect tails\n";
    return 0;
}
