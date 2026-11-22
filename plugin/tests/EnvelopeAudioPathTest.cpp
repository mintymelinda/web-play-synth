#include "PluginProcessor.h"

#include <algorithm>

#include <cmath>
#include <cstdlib>
#include <iostream>

struct WebPlaySynthAudioProcessorTestAccess
{
    static int activeVoiceCount(const WebPlaySynthAudioProcessor& processor)
    {
        return static_cast<int>(std::count_if(processor.voices.begin(), processor.voices.end(),
            [](const auto& voice) { return voice.active; }));
    }

    static float ampEnvelopeValue(const WebPlaySynthAudioProcessor& processor)
    {
        return processor.voices[0].ampEnvelope.getNextSample();
    }

    static float filterEnvelopeValue(const WebPlaySynthAudioProcessor& processor)
    {
        return processor.voices[0].filterEnvelope.getNextSample();
    }
};

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
    setParameter(processor, "cutoff", filterCutoff);
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
    const auto ampAttackActive = renderEnergy(true, 4.0f, false, 18000.0f, 0.0f);
    require(ampBypassed > 0.01,
        "bypassing the amp envelope must leave the oscillator audio audible");
    require(ampAttackActive < ampBypassed * 0.02,
        "enabling a long amp attack must attenuate the initial rendered audio");

    const auto filterEnvelopeBypassed = renderEnergy(false, 0.005f, false, 100.0f, 1.0f);
    const auto filterEnvelopeActive = renderEnergy(false, 0.005f, true, 100.0f, 1.0f);
    require(filterEnvelopeActive > filterEnvelopeBypassed * 1.5,
        "filter envelope route must audibly open the filter beyond its base cutoff");

    // Bypassed routes must not freeze envelope state. In particular, the
    // amp ADSR still owns voice lifetime even when its audio contribution is bypassed.
    {
        WebPlaySynthAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        setParameter(processor, "ampEnvEnabled", 0.0f);
        setParameter(processor, "filterEnvEnabled", 0.0f);
        setParameter(processor, "ampAttack", 0.005f);
        setParameter(processor, "ampDecay", 0.005f);
        setParameter(processor, "ampSustain", 1.0f);
        setParameter(processor, "ampRelease", 0.005f);
        setParameter(processor, "filterAttack", 0.005f);
        setParameter(processor, "filterDecay", 0.005f);
        setParameter(processor, "filterSustain", 1.0f);
        setParameter(processor, "filterRelease", 0.005f);

        juce::AudioBuffer<float> output(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        processor.processBlock(output, midi);
        require(WebPlaySynthAudioProcessorTestAccess::activeVoiceCount(processor) == 1,
            "note-on must allocate one voice with both envelope routes bypassed");

        for (int block = 1; block < 48; ++block) {
            midi.clear();
            processor.processBlock(output, midi);
        }
        require(WebPlaySynthAudioProcessorTestAccess::ampEnvelopeValue(processor) > 0.99f,
            "bypassed amp ADSR must continue through attack and decay to sustain");
        require(WebPlaySynthAudioProcessorTestAccess::filterEnvelopeValue(processor) > 0.99f,
            "bypassed filter ADSR must continue through attack and decay to sustain");

        // Re-enable the routes while the note is held: neither envelope should
        // restart from its attack phase after having been bypassed.
        setParameter(processor, "ampEnvEnabled", 1.0f);
        setParameter(processor, "filterEnvEnabled", 1.0f);
        midi.clear();
        processor.processBlock(output, midi);
        require(WebPlaySynthAudioProcessorTestAccess::ampEnvelopeValue(processor) > 0.99f,
            "re-enabling amp envelope routing must use its progressed state");
        require(WebPlaySynthAudioProcessorTestAccess::filterEnvelopeValue(processor) > 0.99f,
            "re-enabling filter envelope routing must use its progressed state");

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        processor.processBlock(output, midi);
        require(WebPlaySynthAudioProcessorTestAccess::activeVoiceCount(processor) == 0,
            "note-off must retire the voice after the amp release even when routing was bypassed");

        processor.releaseResources();
    }

    std::cout << "PASS: amp ADSR and filter ADSR audio routing\n";
    return 0;
}
