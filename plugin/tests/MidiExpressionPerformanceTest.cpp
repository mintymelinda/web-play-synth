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
    require(parameter != nullptr, "expected MIDI performance parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void configure(WebPlaySynthAudioProcessor& processor, bool ampEnvelope)
{
    processor.prepareToPlay(48000.0, 256);
    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "ampEnvEnabled", ampEnvelope ? 1.0f : 0.0f);
    setParameter(processor, "ampAttack", 0.005f);
    setParameter(processor, "ampDecay", 0.005f);
    setParameter(processor, "ampSustain", 1.0f);
    setParameter(processor, "ampRelease", 0.02f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
}

float renderPeak(WebPlaySynthAudioProcessor& processor, juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> output(2, 256);
    processor.processBlock(output, midi);
    float peak = 0.0f;
    for (int channel = 0; channel < output.getNumChannels(); ++channel) {
        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(channel, sample);
            require(std::isfinite(value), "MIDI expression output must remain finite");
            peak = juce::jmax(peak, std::abs(value));
        }
    }
    return peak;
}

double renderPitchBend(bool bend)
{
    WebPlaySynthAudioProcessor processor;
    configure(processor, false);
    juce::MidiBuffer midi;
    double energy = 0.0;
    juce::AudioBuffer<float> output(2, 256);

    for (int block = 0; block < 12; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        if (bend && block == 1)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
        processor.processBlock(output, midi);
        if (block >= 2) {
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
                energy += static_cast<double>(output.getSample(0, sample)) * output.getSample(0, sample);
        }
    }

    processor.releaseResources();
    return energy;
}
}

int main()
{
    const auto centeredPitchEnergy = renderPitchBend(false);
    const auto bentPitchEnergy = renderPitchBend(true);
    require(std::abs(centeredPitchEnergy - bentPitchEnergy) > 0.01,
        "pitch wheel movement must alter the rendered oscillator frequency");

    WebPlaySynthAudioProcessor processor;
    configure(processor, true);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    renderPeak(processor, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    renderPeak(processor, midi);

    midi.clear();
    float heldPeak = 0.0f;
    for (int block = 0; block < 12; ++block)
        heldPeak = juce::jmax(heldPeak, renderPeak(processor, midi));
    require(heldPeak > 0.05f,
        "sustain pedal must hold a released MIDI note while the pedal remains down");

    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    renderPeak(processor, midi);
    midi.clear();
    float releasedPeak = 0.0f;
    for (int block = 0; block < 24; ++block)
        releasedPeak = juce::jmax(releasedPeak, renderPeak(processor, midi));
    require(releasedPeak < 0.001f,
        "releasing sustain pedal must release held notes through the amp envelope");

    processor.releaseResources();
    std::cout << "PASS: MIDI pitch bend and sustain pedal audio paths\n";
    return 0;
}
