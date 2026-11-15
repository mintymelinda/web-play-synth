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

    // MIDI CC learn maps a controller to a host parameter and applies the
    // controller value through JUCE's normalized parameter interface.
    processor.setMidiCcLearnTarget("cutoff");
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 0);
    processor.processBlock(audio, midi);
    const auto ccCutoff = processor.parameters.getRawParameterValue("cutoff")->load();
    require(ccCutoff > 17000.0f, "learned MIDI CC must update its assigned parameter");

    // A new assignment of the same CC replaces the old target.
    processor.setMidiCcLearnTarget("resonance");
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 64), 0);
    processor.processBlock(audio, midi);
    require(processor.getMidiCcMappingForParameter("cutoff").isEmpty(), "reassigning a CC must clear its previous parameter");
    require(processor.getMidiCcMappingForParameter("resonance") == "74", "reassigned CC must target the newly learned parameter");

    // CC mappings are serialized alongside APVTS parameter state.
    juce::MemoryBlock mappedState;
    processor.getStateInformation(mappedState);
    WebPlaySynthAudioProcessor restoredProcessor;
    restoredProcessor.setStateInformation(mappedState.getData(), static_cast<int>(mappedState.getSize()));
    require(restoredProcessor.getMidiCcMappingForParameter("resonance") == "74", "MIDI CC assignments must survive state restore");

    // A parameter can have only one CC; learning another CC for the same
    // parameter replaces its previous assignment.
    restoredProcessor.setMidiCcLearnTarget("resonance");
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 71, 0), 0);
    restoredProcessor.processBlock(audio, midi);
    require(restoredProcessor.getMidiCcMappingForParameter("resonance") == "71", "a parameter must retain only its latest learned CC");
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 71, 0), 0);
    restoredProcessor.processBlock(audio, midi);
    require(std::abs(restoredProcessor.parameters.getRawParameterValue("resonance")->load() - 0.1f) < 0.02f,
        "restored MIDI CC must continue controlling its parameter");

    restoredProcessor.clearMidiCcMapping("resonance");
    require(restoredProcessor.getMidiCcMappingForParameter("resonance").isEmpty(), "clearing a mapping must remove the assignment");

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    processor.processBlock(audio, midi);

    midi.clear();
    float tailPeak = 0.0f;
    const auto releaseBlocks = (7 * 48000) / audio.getNumSamples();
    for (int block = 0; block < releaseBlocks; ++block) {
        processor.processBlock(audio, midi);
        if (block == releaseBlocks - 1) {
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                    tailPeak = juce::jmax(tailPeak, std::abs(audio.getSample(channel, sample)));
        }
    }

    require(std::isfinite(tailPeak) && tailPeak < 0.0001f, "released voice must eventually return to silence");

    // The pitch input bus is optional and disabled by default, preserving the
    // existing instrument's MIDI-only layout until a host explicitly routes it.
    WebPlaySynthAudioProcessor inputProcessor;
    require(!inputProcessor.getBus(true, 0)->isEnabled(), "pitch input bus must be disabled by default");
    const auto defaultLayout = inputProcessor.getBusesLayout();
    require(inputProcessor.checkBusesLayoutSupported(defaultLayout), "default disabled-input stereo-output layout must remain supported");
    auto monoOutputLayout = defaultLayout;
    monoOutputLayout.outputBuses.set(0, juce::AudioChannelSet::mono());
    require(inputProcessor.checkBusesLayoutSupported(monoOutputLayout), "disabled-input mono-output layout must remain supported");
    auto inputLayout = inputProcessor.getBusesLayout();
    inputLayout.inputBuses.set(0, juce::AudioChannelSet::mono());
    require(inputProcessor.setBusesLayout(inputLayout), "processor must accept an enabled mono pitch input bus");
    inputProcessor.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> inputAndOutput(2, 512);
    for (int sample = 0; sample < inputAndOutput.getNumSamples(); ++sample)
        inputAndOutput.setSample(0, sample, 0.5f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * sample / 48000.0f));
    juce::MidiBuffer noMidi;
    inputProcessor.processBlock(inputAndOutput, noMidi);

    require(inputProcessor.getPitchInputRms() > 0.3f, "known mono input signal must reach the pitch input bus");
    float inputTestOutputPeak = 0.0f;
    for (int channel = 0; channel < inputAndOutput.getNumChannels(); ++channel)
        for (int sample = 0; sample < inputAndOutput.getNumSamples(); ++sample)
            inputTestOutputPeak = juce::jmax(inputTestOutputPeak, std::abs(inputAndOutput.getSample(channel, sample)));
    require(inputTestOutputPeak < 0.000001f, "pitch input must not pass through to the synth output");

    // Enabling the audio input must not disable the instrument's MIDI path.
    for (int sample = 0; sample < inputAndOutput.getNumSamples(); ++sample)
        inputAndOutput.setSample(0, sample, 0.5f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * sample / 48000.0f));
    juce::MidiBuffer inputBusMidi;
    inputBusMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    inputProcessor.processBlock(inputAndOutput, inputBusMidi);
    float midiWithInputPeak = 0.0f;
    for (int channel = 0; channel < inputAndOutput.getNumChannels(); ++channel)
        for (int sample = 0; sample < inputAndOutput.getNumSamples(); ++sample)
            midiWithInputPeak = juce::jmax(midiWithInputPeak, std::abs(inputAndOutput.getSample(channel, sample)));
    require(inputProcessor.getPitchInputRms() > 0.3f, "pitch input must remain readable while MIDI is active");
    require(midiWithInputPeak > 0.0001f, "MIDI must continue producing synth audio with pitch input enabled");
    inputProcessor.releaseResources();

    processor.releaseResources();

    std::cout << "PASS: MIDI rendering, finite audio, patch state round-trip, and release-to-silence\n";
    return 0;
}
