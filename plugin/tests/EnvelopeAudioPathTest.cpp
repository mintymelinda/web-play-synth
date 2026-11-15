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
    require(parameter != nullptr, "expected envelope parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

double renderEnergy(bool ampEnvelopeEnabled, float ampAttack,
                    bool filterEnvelopeEnabled, float filterCutoff,
                    float filterContour)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "ampEnvEnabled", ampEnvelopeEnabled ? 1.0f : 0.0f);
    setParameter(processor, "ampAttack", ampAttack);
    setParameter(processor, "ampDecay", 0.005f);
    setParameter(processor, "ampSustain", 1.0f);
    setParameter(processor, "filterEnvEnabled", filterEnvelopeEnabled ? 1.0f : 0.0f);
    setParameter(processor, "filterAttack", 0.005f);
    setParameter(processor, "filterDecay", 0.005f);
    setParameter(processor, "filterSustain", 1.0f);
    setParameter(processor, "filterCutoff", filterCutoff);
    setParameter(processor, "filterContour", filterContour);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "master", 1.0f);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 48; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        processor.processBlock(output, midi);
    }

    double energy = 0.0;
    for (int channel = 0; channel < output.getNumChannels(); ++channel) {
        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(channel, sample);
            require(std::isfinite(value), "envelope-routed audio must remain finite");
            energy += static_cast<double>(value) * value;
        }
    }

    processor.releaseResources();
    return energy;
}
}

int main()
{
    const auto ampBypassed = renderEnergy(false, 3.5f, false, 18000.0f, 0.0f);
    const auto ampAttackActive = renderEnergy(true, 3.5f, false, 18000.0f, 0.0f);
    require(ampBypassed > 0.01,
        "bypassing the amp envelope must leave the oscillator audio audible");
    require(ampAttackActive < ampBypassed * 0.02,
        "enabling a long amp attack must attenuate the initial rendered audio");

    const auto filterEnvelopeBypassed = renderEnergy(false, 0.005f, false, 300.0f, 1.0f);
    const auto filterEnvelopeActive = renderEnergy(false, 0.005f, true, 300.0f, 1.0f);
    require(filterEnvelopeActive > filterEnvelopeBypassed * 1.5,
        "filter envelope route must audibly open the filter beyond its base cutoff");

    std::cout << "PASS: amp ADSR and filter ADSR audio routing\n";
    return 0;
}
