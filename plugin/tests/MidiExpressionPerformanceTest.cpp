#include "PluginProcessor.h"

#include <algorithm>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

struct WebPlaySynthAudioProcessorTestAccess
{
    static int activeVoiceCount(const WebPlaySynthAudioProcessor& processor)
    {
        return static_cast<int>(std::count_if(processor.voices.begin(), processor.voices.end(),
            [](const auto& voice) { return voice.active; }));
    }

    static bool firstVoiceSustainHeld(const WebPlaySynthAudioProcessor& processor)
    {
        return processor.voices[0].sustainHeld;
    }

    static bool firstVoiceAmpEnvelopeActive(const WebPlaySynthAudioProcessor& processor)
    {
        return processor.voices[0].ampEnvelope.isActive();
    }

    static bool sustainPedalDown(const WebPlaySynthAudioProcessor& processor, int channel)
    {
        return processor.sustainPedalDown[static_cast<size_t>(channel - 1)];
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

std::vector<float> renderPitchBend(bool bend)
{
    WebPlaySynthAudioProcessor processor;
    configure(processor, false);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output(2, 256);
    std::vector<float> rendered;
    rendered.reserve(10 * 256);

    for (int block = 0; block < 12; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        if (bend && block == 1)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
        processor.processBlock(output, midi);
        if (block >= 2) {
            for (int sample = 0; sample < output.getNumSamples(); ++sample) {
                const auto value = output.getSample(0, sample);
                require(std::isfinite(value), "pitch-bent oscillator output must remain finite");
                rendered.push_back(value);
            }
        }
    }

    processor.releaseResources();
    return rendered;
}
}

int main()
{
    const auto centeredPitch = renderPitchBend(false);
    const auto bentPitch = renderPitchBend(true);
    require(centeredPitch.size() == bentPitch.size(),
        "pitch-bend renders must have equal lengths");
    double pitchDifference = 0.0;
    for (size_t sample = 0; sample < centeredPitch.size(); ++sample) {
        const auto difference = centeredPitch[sample] - bentPitch[sample];
        pitchDifference += static_cast<double>(difference) * difference;
    }
    require(pitchDifference > 0.1,
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
    require(!WebPlaySynthAudioProcessorTestAccess::sustainPedalDown(processor, 1),
        "sustain pedal state must clear on CC64 release");
    require(!WebPlaySynthAudioProcessorTestAccess::firstVoiceSustainHeld(processor),
        "released voice must leave sustain-held state on CC64 release");
    require(WebPlaySynthAudioProcessorTestAccess::firstVoiceAmpEnvelopeActive(processor),
        "amp ADSR must enter its release stage after CC64 release");
    midi.clear();
    float releasedPeak = 0.0f;
    for (int block = 0; block < 24; ++block)
        releasedPeak = juce::jmax(releasedPeak, renderPeak(processor, midi));
    if (releasedPeak >= 0.001f)
        std::cerr << "sustain release peak " << releasedPeak
                  << ", active voices " << WebPlaySynthAudioProcessorTestAccess::activeVoiceCount(processor)
                  << ", amp ADSR active " << WebPlaySynthAudioProcessorTestAccess::firstVoiceAmpEnvelopeActive(processor)
                  << '\\n';
    require(releasedPeak < 0.001f,
        "releasing sustain pedal must release held notes through the amp envelope");
    require(WebPlaySynthAudioProcessorTestAccess::activeVoiceCount(processor) == 0,
        "released sustain-held voice must be retired after its amp release");

    processor.releaseResources();
    std::cout << "PASS: MIDI pitch bend and sustain pedal audio paths\n";
    return 0;
}
