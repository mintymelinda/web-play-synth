#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    processor.processBlock(audio, midi);

    float peak = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            peak = juce::jmax(peak, std::abs(audio.getSample(channel, sample)));

    require(std::isfinite(peak) && peak > 0.0001f, "MIDI note-on must produce finite, non-silent audio");

    juce::MemoryBlock savedState;
    processor.getStateInformation(savedState);
    const auto originalCutoff = processor.parameters.getRawParameterValue("cutoff")->load();
    processor.parameters.getParameter("cutoff")->setValueNotifyingHost(0.1f);
    processor.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
    const auto restoredCutoff = processor.parameters.getRawParameterValue("cutoff")->load();
    require(std::abs(originalCutoff - restoredCutoff) < 0.01f, "plugin state must restore parameter values");

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    processor.processBlock(audio, midi);

    midi.clear();
    float tailPeak = 0.0f;
    const auto releaseBlocks = (7 * 48000) / audio.getNumSamples();
    for (int block = 0; block < releaseBlocks; ++block) {
        processor.processBlock(audio, midi);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                tailPeak = juce::jmax(tailPeak, std::abs(audio.getSample(channel, sample)));
    }

    require(std::isfinite(tailPeak) && tailPeak < 0.0001f, "released voice must eventually return to silence");
    processor.releaseResources();

    std::cout << "PASS: MIDI rendering, finite audio, patch state round-trip, and release-to-silence\n";
    return 0;
}
