#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

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
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    using ParameterState = juce::AudioProcessorValueTreeState;
    ParameterState parameters;

    // MIDI CC assignments target host-automatable parameters. A single CC maps
    // to at most one parameter and each parameter has at most one assigned CC.
    void setMidiCcLearnTarget(const juce::String& parameterId);
    void clearMidiCcMapping(const juce::String& parameterId);
    juce::String getMidiCcMappingForParameter(const juce::String& parameterId) const;
    bool isMidiCcLearning() const noexcept { return midiCcLearnTarget.load() >= 0; }

private:
    static const juce::StringArray& midiCcParameterIds();
    std::array<std::atomic<int>, 128> midiCcToParameter {};
    std::atomic<int> midiCcLearnTarget { -1 };
    std::atomic<float> pitchInputRms { 0.0f };
    struct Parameters {
        int osc1Wave=2, osc2Wave=3, lfoWave=0;
        float osc1Octave=0, osc1Tune=0, osc1Level=.72f, osc2Octave=0, osc2Tune=7, osc2Level=.38f;
        bool hardSync=false, glideEnabled=false, vco1Enabled=true, vco2Enabled=true, filterEnvEnabled=true, ampEnvEnabled=true;
        float crossMod=0, cutoff=1800, resonance=.8f, filterContour=.55f;
        float filterAttack=.025f, filterDecay=.42f, filterSustain=.32f, filterRelease=.38f;
        float ampAttack=.015f, ampDecay=.25f, ampSustain=.78f, ampRelease=.32f;
        float lfoRate=4.5f, lfoPitchDepth=0, lfoFilterDepth=0, master=.72f, glideTime=.18f;
    };
    struct Voice {
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::ADSR ampEnvelope, filterEnvelope;
        bool active=false, keyDown=false;
        int note=-1, age=0;
        float velocity=0;
        double phase1=0, phase2=0, lfoPhase=0, currentFrequency=440, targetFrequency=440;
        void prepare(double sampleRate, int maximumBlockSize);
        void start(int midiNote, float noteVelocity, const Parameters& p, double previousFrequency);
        void stop();
        float render(const Parameters& p, double sampleRate);
    };
    static ParameterState::ParameterLayout createParameterLayout();
    Parameters readParameters() const;
    void renderRange(juce::AudioBuffer<float>& buffer, int start, int end, const Parameters& p);
    void handleMidiMessage(const juce::MidiMessage& message, const Parameters& p);
    Voice* findVoiceToStart();
    void startNote(int note, float velocity, const Parameters& p);
    void stopNote(int note);
    static float oscillatorSample(double phase, double increment, int waveform);
    static float polyBlep(double phase, double increment);
    static float waveformSample(double phase, int waveform);
    std::array<Voice,16> voices;
    double currentSampleRate=44100, lastPlayedFrequency=0;
    int voiceAge=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebPlaySynthAudioProcessor)
};
