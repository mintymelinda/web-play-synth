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


    // Portamento time is the time to reach the destination, not merely one time constant.
    const auto estimatePitch = [](const std::vector<float>& samples) {
        std::vector<double> crossings;
        for (size_t index=1; index<samples.size(); ++index) {
            const auto previous=samples[index-1];
            const auto current=samples[index];
            if (previous<=0.0f && current>0.0f) {
                const auto fraction=static_cast<double>(-previous)/(static_cast<double>(current)-previous);
                crossings.push_back(static_cast<double>(index-1)+fraction);
            }
        }
        if (crossings.size()<2) return 0.0;
        return static_cast<double>(crossings.size()-1)*48000.0/(crossings.back()-crossings.front());
    };
    WebPlaySynthAudioProcessor glideProcessor;
    glideProcessor.prepareToPlay(48000.0,512);
    const auto setGlideParameter=[&glideProcessor](const char* id,float value) {
        auto* parameter=glideProcessor.parameters.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    setGlideParameter("osc1Wave",0.0f);
    setGlideParameter("osc1Level",1.0f);
    setGlideParameter("osc2Level",0.0f);
    setGlideParameter("cutoff",18000.0f);
    setGlideParameter("resonance",0.1f);
    setGlideParameter("filterEnvEnabled",0.0f);
    setGlideParameter("ampAttack",0.005f);
    setGlideParameter("ampDecay",0.005f);
    setGlideParameter("ampSustain",1.0f);
    setGlideParameter("ampRelease",0.01f);
    setGlideParameter("glideEnabled",1.0f);
    setGlideParameter("glideTime",0.1f);
    juce::AudioBuffer<float> glideAudio(2,512);
    juce::MidiBuffer glideMidi;
    glideMidi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
    glideProcessor.processBlock(glideAudio,glideMidi);
    glideMidi.clear();
    glideMidi.addEvent(juce::MidiMessage::noteOff(1,60),0);
    for(int block=0;block<20;++block) glideProcessor.processBlock(glideAudio,glideMidi);
    glideMidi.clear();
    glideMidi.addEvent(juce::MidiMessage::noteOn(1,72,0.8f),0);
    glideProcessor.processBlock(glideAudio,glideMidi);
    std::vector<float> middleGlide;
    std::vector<float> settledGlide;
    for(int block=1;block<=12;++block) {
        glideMidi.clear();
        glideProcessor.processBlock(glideAudio,glideMidi);
        if(block>=1 && block<=4)
            for(int sample=0;sample<glideAudio.getNumSamples();++sample)
                middleGlide.push_back(glideAudio.getSample(0,sample));
        if(block>=9)
            for(int sample=0;sample<glideAudio.getNumSamples();++sample)
                settledGlide.push_back(glideAudio.getSample(0,sample));
    }
    const auto middleFrequency=estimatePitch(middleGlide);
    const auto settledFrequency=estimatePitch(settledGlide);
    require(middleFrequency>300.0 && middleFrequency<510.0,
        "native glide must produce intermediate pitches during its configured transition");
    require(settledFrequency>510.0 && settledFrequency<536.0,
        "native glide must reach the target note within its configured duration");
    glideProcessor.releaseResources();


    // The LFO has real amplitude and stereo-pan destinations in addition to pitch/filter.
    const auto renderLfoRoutes = [](float ampDepth,float panDepth) {
        WebPlaySynthAudioProcessor lfoProcessor;
        lfoProcessor.prepareToPlay(48000.0,512);
        auto setPlain=[&lfoProcessor](const char* id,float value) {
            auto* parameter=lfoProcessor.parameters.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setPlain("osc1Wave",0.0f);
        setPlain("osc1Level",1.0f);
        setPlain("osc2Level",0.0f);
        setPlain("cutoff",18000.0f);
        setPlain("resonance",0.1f);
        setPlain("ampEnvEnabled",0.0f);
        setPlain("filterEnvEnabled",0.0f);
        setPlain("lfoWave",0.0f);
        setPlain("lfoRate",5.0f);
        setPlain("lfoAmpDepth",ampDepth);
        setPlain("lfoPanDepth",panDepth);
        juce::AudioBuffer<float> output(2,512);
        juce::MidiBuffer routeMidi;
        double highAmpEnergy=0.0,lowAmpEnergy=0.0;
        double rightEarlyEnergy=0.0,leftEarlyEnergy=0.0;
        double rightLateEnergy=0.0,leftLateEnergy=0.0;
        int ampSamples=0,panSamples=0;
        for(int block=0;block<18;++block) {
            routeMidi.clear();
            if(block==0) routeMidi.addEvent(juce::MidiMessage::noteOn(1,69,0.8f),0);
            lfoProcessor.processBlock(output,routeMidi);
            for(int sample=0;sample<output.getNumSamples();++sample) {
                const auto left=output.getSample(0,sample);
                const auto right=output.getSample(1,sample);
                if(block>=2 && block<=3) {
                    highAmpEnergy+=static_cast<double>(left)*left+static_cast<double>(right)*right;
                    ++ampSamples;
                }
                if(block>=13 && block<=14) {
                    lowAmpEnergy+=static_cast<double>(left)*left+static_cast<double>(right)*right;
                }
                if(block>=4 && block<=5) {
                    leftEarlyEnergy+=static_cast<double>(left)*left;
                    rightEarlyEnergy+=static_cast<double>(right)*right;
                    ++panSamples;
                }
                if(block>=13 && block<=14) {
                    leftLateEnergy+=static_cast<double>(left)*left;
                    rightLateEnergy+=static_cast<double>(right)*right;
                }
            }
        }
        lfoProcessor.releaseResources();
        return std::array<double,6> {
            std::sqrt(highAmpEnergy/(2.0*ampSamples)),
            std::sqrt(lowAmpEnergy/(2.0*ampSamples)),
            std::sqrt(leftEarlyEnergy/panSamples),
            std::sqrt(rightEarlyEnergy/panSamples),
            std::sqrt(leftLateEnergy/panSamples),
            std::sqrt(rightLateEnergy/panSamples)
        };
    };
    const auto tremolo=renderLfoRoutes(1.0f,0.0f);
    require(tremolo[0]>tremolo[1]*4.0,
        "LFO amplitude depth must create audible tremolo in the voice output");
    const auto autoPan=renderLfoRoutes(0.0f,1.0f);
    require(autoPan[3]>autoPan[2]*2.0 && autoPan[4]>autoPan[5]*2.0,
        "LFO pan depth must move the voice from right to left across the stereo field");

    // Factory patches exercise the newly implemented native audio paths.
    require(processor.getNumPrograms() == 8, "native instrument must expose eight factory programs");
    require(processor.getProgramName(0) == "Init", "first factory program must be Init");
    processor.setCurrentProgram(3);
    require(processor.getCurrentProgram() == 3, "program selection must update the current index");
    require(processor.parameters.getRawParameterValue("cutoff")->load() < 700.0f,
        "Deep Bass must apply its low filter cutoff");
    processor.setCurrentProgram(4);
    require(static_cast<int>(std::round(processor.parameters.getRawParameterValue("filterType")->load())) == 2,
        "Glass Pluck must select the implemented band-pass filter");
    require(processor.parameters.getRawParameterValue("delayMix")->load() > 0.15f
        && processor.parameters.getRawParameterValue("reverbMix")->load() > 0.2f,
        "Glass Pluck must enable the delay and reverb audio paths");
    processor.setCurrentProgram(7);
    require(static_cast<int>(std::round(processor.parameters.getRawParameterValue("unisonVoices")->load())) == 2,
        "Slow Atmosphere must select four-layer unison");
    require(processor.parameters.getRawParameterValue("lfoPanDepth")->load() > 0.4f
        && processor.parameters.getRawParameterValue("reverbMix")->load() > 0.6f,
        "Slow Atmosphere must use LFO pan and reverb destinations");
    juce::MemoryBlock presetState;
    processor.getStateInformation(presetState);
    WebPlaySynthAudioProcessor presetRestored;
    presetRestored.setStateInformation(presetState.getData(), static_cast<int>(presetState.getSize()));
    require(presetRestored.getCurrentProgram() == 7, "current factory program must survive state restore");
    require(presetRestored.parameters.getRawParameterValue("reverbMix")->load() > 0.6f,
        "new audio-path preset parameters must survive state restore");
    processor.setCurrentProgram(0);
    require(processor.parameters.getRawParameterValue("cutoff")->load() > 1700.0f,
        "Init program must restore its default filter cutoff");
    require(processor.parameters.getRawParameterValue("delayMix")->load() == 0.0f
        && processor.parameters.getRawParameterValue("filterDrive")->load() == 0.0f
        && processor.parameters.getRawParameterValue("lfoPanDepth")->load() == 0.0f,
        "Init must reset optional native audio paths to their neutral state");


    // Hosts should receive the current effect tail, not an unconditional eight seconds.
    WebPlaySynthAudioProcessor tailProcessor;
    require(tailProcessor.getTailLengthSeconds() == 0.0,
        "disabled delay and reverb must report no effect tail");
    const auto setTailParameter = [&tailProcessor](const char* id,float value) {
        auto* parameter=tailProcessor.parameters.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    setTailParameter("delayMix",1.0f);
    setTailParameter("delayTime",0.2f);
    setTailParameter("delayFeedback",0.0f);
    require(std::abs(tailProcessor.getTailLengthSeconds()-0.2) < 0.001,
        "single-repeat delay must report one delay interval");
    setTailParameter("delayFeedback",0.5f);
    require(std::abs(tailProcessor.getTailLengthSeconds()-2.0) < 0.001,
        "delay tail must account for repeats down to -60 dB");
    setTailParameter("reverbMix",0.4f);
    require(std::abs(tailProcessor.getTailLengthSeconds()-10.0) < 0.001,
        "combined delay and reverb tails must be additive");


    // Browser hard sync replaces VCO 2 with a ratio-shaped periodic waveform.
    const auto renderSyncWave = [](bool hardSync,float oscillator2Wave) {
        WebPlaySynthAudioProcessor syncProcessor;
        syncProcessor.prepareToPlay(48000.0,512);
        auto setPlain=[&syncProcessor](const char* id,float value) {
            auto* parameter=syncProcessor.parameters.getParameter(id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        setPlain("osc1Level",0.0f);
        setPlain("osc2Level",1.0f);
        setPlain("osc1Wave",0.0f);
        setPlain("osc2Wave",oscillator2Wave);
        setPlain("osc2Tune",7.0f);
        setPlain("hardSync",hardSync?1.0f:0.0f);
        setPlain("crossMod",0.0f);
        setPlain("cutoff",18000.0f);
        setPlain("resonance",0.1f);
        setPlain("ampEnvEnabled",0.0f);
        setPlain("filterEnvEnabled",0.0f);
        juce::AudioBuffer<float> output(2,512);
        juce::MidiBuffer syncMidi;
        for(int block=0;block<96;++block) {
            syncMidi.clear();
            if(block==0) syncMidi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
            syncProcessor.processBlock(output,syncMidi);
        }
        std::array<float,512> samples {};
        for(int sample=0;sample<output.getNumSamples();++sample)
            samples[static_cast<size_t>(sample)]=output.getSample(0,sample);
        syncProcessor.releaseResources();
        return samples;
    };
    const auto syncedSineSelection=renderSyncWave(true,0.0f);
    const auto syncedSquareSelection=renderSyncWave(true,3.0f);
    double syncedWaveformDifference=0.0;
    for(size_t sample=0;sample<syncedSineSelection.size();++sample) {
        const auto delta=syncedSineSelection[sample]-syncedSquareSelection[sample];
        syncedWaveformDifference+=delta*delta;
    }
    require(syncedWaveformDifference<0.000001,
        "hard sync must use the ratio-shaped sync wave regardless of VCO 2 waveform selection");
    const auto freeSine=renderSyncWave(false,0.0f);
    const auto freeSquare=renderSyncWave(false,3.0f);
    double freeWaveformDifference=0.0;
    for(size_t sample=0;sample<freeSine.size();++sample) {
        const auto delta=freeSine[sample]-freeSquare[sample];
        freeWaveformDifference+=delta*delta;
    }
    require(freeWaveformDifference>0.01,
        "VCO 2 waveform selection must remain active when hard sync is disabled");

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
