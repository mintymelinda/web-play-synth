#pragma once
#include <JuceHeader.h>
#include "Engine/PolySynthEngine.h"
#include "Dsp/Delay.h"
#include "Dsp/Chorus.h"
#include "Dsp/Reverb.h"
#include <array>
#include <atomic>
#include <vector>

class WebPlaySynthAudioProcessor final : public juce::AudioProcessor {
public:
    WebPlaySynthAudioProcessor();
    ~WebPlaySynthAudioProcessor() override = default;
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    float getPitchInputRms() const noexcept { return pitchInputRms.load(std::memory_order_relaxed); }
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 8; }
    int getCurrentProgram() override { return currentProgram.load(std::memory_order_relaxed); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    bool savePatchFile(const juce::File& file);
    bool loadPatchFile(const juce::File& file);
    using ParameterState = juce::AudioProcessorValueTreeState;
    ParameterState parameters;

    // MIDI CC assignments target host-automatable parameters. A single CC maps
    // to at most one parameter and each parameter has at most one assigned CC.
    void setMidiCcLearnTarget(const juce::String& parameterId);
    void clearMidiCcMapping(const juce::String& parameterId);
    juce::String getMidiCcMappingForParameter(const juce::String& parameterId) const;
    bool isMidiCcLearning() const noexcept { return midiCcLearnTarget.load() >= 0; }

private:
    friend struct WebPlaySynthAudioProcessorTestAccess;
    static const juce::StringArray& midiCcParameterIds();
    std::array<std::atomic<int>, 128> midiCcToParameter {};
    std::atomic<int> midiCcLearnTarget { -1 };
    std::atomic<float> pitchInputRms { 0.0f };
    std::atomic<int> currentProgram { 0 };
    using Parameters = webplaysynth::engine::SynthParameters;
    static ParameterState::ParameterLayout createParameterLayout();
    Parameters readParameters() const;
    void processEffects(juce::AudioBuffer<float>& output, const Parameters& p);
    void handleMidiMessage(const juce::MidiMessage& message, const Parameters& p);
    webplaysynth::engine::PolySynthEngine engine;
    webplaysynth::dsp::StereoReverb reverb;
    webplaysynth::dsp::StereoChorus chorus;
    webplaysynth::dsp::StereoDelay delay;
    double currentSampleRate=44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebPlaySynthAudioProcessor)
};
