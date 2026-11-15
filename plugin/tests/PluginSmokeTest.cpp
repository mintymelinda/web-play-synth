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


    // The native filter exposes three functioning topologies, not cosmetic modes.
    const auto renderFilterTone = [](int filterMode, float filterDrive) {
        WebPlaySynthAudioProcessor filterProcessor;
        filterProcessor.prepareToPlay(48000.0, 512);
        auto setPlain = [&filterProcessor](const char* id, float value) {
            auto* parameter = filterProcessor.parameters.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setPlain("osc1Wave", 0.0f);
        setPlain("osc1Level", 1.0f);
        setPlain("osc2Level", 0.0f);
        setPlain("cutoff", 1800.0f);
        setPlain("resonance", 0.8f);
        setPlain("filterType", static_cast<float>(filterMode));
        setPlain("filterDrive", filterDrive);
        setPlain("ampEnvEnabled", 0.0f);
        setPlain("filterEnvEnabled", 0.0f);
        juce::AudioBuffer<float> tone(2, 512);
        juce::MidiBuffer toneMidi;
        for (int block = 0; block < 96; ++block) {
            toneMidi.clear();
            if (block == 0)
                toneMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
            filterProcessor.processBlock(tone, toneMidi);
        }
        std::array<float, 512> samples {};
        for (int sample = 0; sample < tone.getNumSamples(); ++sample)
            samples[static_cast<size_t>(sample)] = tone.getSample(0, sample);
        filterProcessor.releaseResources();
        return samples;
    };
    const auto lowPassTone = renderFilterTone(0, 0.0f);
    const auto highPassTone = renderFilterTone(1, 0.0f);
    float lowPassEnergy = 0.0f;
    float highPassEnergy = 0.0f;
    for (size_t sample = 0; sample < lowPassTone.size(); ++sample) {
        lowPassEnergy += lowPassTone[sample] * lowPassTone[sample];
        highPassEnergy += highPassTone[sample] * highPassTone[sample];
    }
    require(lowPassEnergy > highPassEnergy * 4.0f,
        "low-pass and high-pass filter modes must produce different frequency responses");

    const auto cleanTone = renderFilterTone(0, 0.0f);
    const auto drivenTone = renderFilterTone(0, 18.0f);
    float driveDifference = 0.0f;
    for (size_t sample = 0; sample < cleanTone.size(); ++sample) {
        const auto delta = drivenTone[sample] - cleanTone[sample];
        driveDifference += delta * delta;
    }
    require(driveDifference > 0.001f,
        "filter drive must alter the actual pre-filter audio signal");


    // Internal unison layers detune the oscillator pairs and create a real stereo field.
    const auto renderUnison = [](float voiceMode, float detune, float spread) {
        WebPlaySynthAudioProcessor unisonProcessor;
        unisonProcessor.prepareToPlay(48000.0, 512);
        auto setPlain = [&unisonProcessor](const char* id, float value) {
            auto* parameter = unisonProcessor.parameters.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setPlain("osc1Wave", 0.0f);
        setPlain("osc1Level", 1.0f);
        setPlain("osc2Level", 0.0f);
        setPlain("unisonVoices", voiceMode);
        setPlain("unisonDetune", detune);
        setPlain("stereoSpread", spread);
        setPlain("ampEnvEnabled", 0.0f);
        setPlain("filterEnvEnabled", 0.0f);
        juce::AudioBuffer<float> tone(2, 512);
        juce::MidiBuffer toneMidi;
        for (int block = 0; block < 96; ++block) {
            toneMidi.clear();
            if (block == 0)
                toneMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
            unisonProcessor.processBlock(tone, toneMidi);
        }
        std::array<float, 512> leftSamples {}, rightSamples {};
        for (int sample = 0; sample < tone.getNumSamples(); ++sample) {
            leftSamples[static_cast<size_t>(sample)] = tone.getSample(0, sample);
            rightSamples[static_cast<size_t>(sample)] = tone.getSample(1, sample);
        }
        unisonProcessor.releaseResources();
        return std::pair { leftSamples, rightSamples };
    };
    const auto singleVoice = renderUnison(0.0f, 0.0f, 0.0f);
    const auto stackedVoices = renderUnison(2.0f, 20.0f, 0.0f);
    float unisonDifference = 0.0f;
    float monoStereoDifference = 0.0f;
    for (size_t sample = 0; sample < singleVoice.first.size(); ++sample) {
        const auto difference = stackedVoices.first[sample] - singleVoice.first[sample];
        unisonDifference += difference * difference;
        const auto channelDifference = singleVoice.first[sample] - singleVoice.second[sample];
        monoStereoDifference += channelDifference * channelDifference;
    }
    require(unisonDifference > 0.001f, "detuned unison voices must change the rendered oscillator signal");
    require(monoStereoDifference < 0.000001f, "one unison voice with zero spread must retain centered mono imaging");

    const auto spreadVoices = renderUnison(2.0f, 20.0f, 1.0f);
    float stereoImageDifference = 0.0f;
    for (size_t sample = 0; sample < spreadVoices.first.size(); ++sample) {
        const auto difference = spreadVoices.first[sample] - spreadVoices.second[sample];
        stereoImageDifference += difference * difference;
    }
    require(stereoImageDifference > 0.001f, "stereo spread must pan detuned unison layers across the output channels");


    // Delay and reverb must continue producing audio after the dry voice releases.
    const auto renderEffectTail = [](float delayMix, float reverbMix) {
        WebPlaySynthAudioProcessor effectsProcessor;
        effectsProcessor.prepareToPlay(48000.0, 512);
        auto setPlain = [&effectsProcessor](const char* id, float value) {
            auto* parameter = effectsProcessor.parameters.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setPlain("delayTime", 0.2f);
        setPlain("delayFeedback", 0.65f);
        setPlain("delayMix", delayMix);
        setPlain("reverbMix", reverbMix);
        setPlain("reverbRoom", 0.8f);
        setPlain("reverbDamping", 0.2f);
        juce::AudioBuffer<float> effected(2, 512);
        juce::MidiBuffer effectMidi;
        float tailPeak=0.0f;
        for (int block=0; block<160; ++block) {
            effectMidi.clear();
            if (block==0)
                effectMidi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
            if (block==96)
                effectMidi.addEvent(juce::MidiMessage::noteOff(1,60),0);
            effectsProcessor.processBlock(effected,effectMidi);
            if (block>=140)
                for (int channel=0; channel<effected.getNumChannels(); ++channel)
                    for (int sample=0; sample<effected.getNumSamples(); ++sample)
                        tailPeak=juce::jmax(tailPeak,std::abs(effected.getSample(channel,sample)));
        }
        effectsProcessor.releaseResources();
        return tailPeak;
    };
    const auto dryTail = renderEffectTail(0.0f, 0.0f);
    const auto delayTail = renderEffectTail(1.0f, 0.0f);
    const auto reverbTail = renderEffectTail(0.0f, 0.8f);
    require(dryTail < 0.0001f, "dry signal must release to silence when effects are disabled");
    require(delayTail > 0.0001f, "delay feedback must create an audible tail after note-off");
    require(reverbTail > 0.0001f, "reverb must create an audible tail after note-off");

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
