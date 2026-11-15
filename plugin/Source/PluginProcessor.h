#pragma once
#include <JuceHeader.h>
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
    static const juce::StringArray& midiCcParameterIds();
    std::array<std::atomic<int>, 128> midiCcToParameter {};
    std::atomic<int> midiCcLearnTarget { -1 };
    std::atomic<float> pitchInputRms { 0.0f };
    std::atomic<int> currentProgram { 0 };
    struct Parameters {
        int osc1Wave=2, osc2Wave=3, lfoWave=0, filterType=0, unisonVoices=0;
        float osc1Octave=0, osc1Tune=0, osc1Level=.72f, osc1PulseWidth=.5f, osc2Octave=0, osc2Tune=7, osc2Level=.38f, osc2PulseWidth=.5f;
        bool hardSync=false, glideEnabled=false, vco1Enabled=true, vco2Enabled=true, filterEnvEnabled=true, ampEnvEnabled=true;
        float crossMod=0, cutoff=1800, resonance=.8f, filterContour=.55f, filterDrive=0;
        float filterAttack=.025f, filterDecay=.42f, filterSustain=.32f, filterRelease=.38f;
        float ampAttack=.015f, ampDecay=.25f, ampSustain=.78f, ampRelease=.32f;
        float lfoRate=4.5f, lfoPitchDepth=0, lfoFilterDepth=0, lfoAmpDepth=0, lfoPanDepth=0, lfoPulseDepth=0, master=.72f, glideTime=.18f;
        float unisonDetune=0, stereoSpread=0;
        float chorusRate=.8f, chorusDepth=.5f, chorusMix=0;
        float delayTime=.32f, delayFeedback=.35f, delayMix=0;
        float reverbMix=0, reverbRoom=.55f, reverbDamping=.4f;
        double glideCoefficient=1.0;
        double osc1FrequencyRatio=1.0, osc2FrequencyRatio=1.0, hardSyncRatio=1.0;
        float filterDriveGain=1.0f, unisonLayerGain=1.0f;
        std::array<double,4> unisonDetuneRatios {1.0,1.0,1.0,1.0};
        std::array<float,4> unisonPanPositions {};
        std::array<float,4> unisonLeftGains {1.0f,1.0f,1.0f,1.0f};
        std::array<float,4> unisonRightGains {1.0f,1.0f,1.0f,1.0f};
    };
    struct Voice {
        std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filters;
        juce::ADSR ampEnvelope, filterEnvelope;
        bool active=false, keyDown=false;
        int note=-1, age=0;
        float velocity=0;
        std::array<double, 4> phase1 {}, phase2 {};
        double lfoPhase=0, currentFrequency=440, targetFrequency=440;
        int currentFilterType=-1;
        float currentResonance=-1.0f;
        void prepare(double sampleRate, int maximumBlockSize);
        void start(int midiNote, float noteVelocity, const Parameters& p, double previousFrequency);
        void stop();
        void render(const Parameters& p, double sampleRate, float& left, float& right);
    };
    static ParameterState::ParameterLayout createParameterLayout();
    Parameters readParameters() const;
    void renderRange(juce::AudioBuffer<float>& buffer, int start, int end, const Parameters& p);
    void processEffects(juce::AudioBuffer<float>& output, const Parameters& p);
    void handleMidiMessage(const juce::MidiMessage& message, const Parameters& p);
    Voice* findVoiceToStart();
    void startNote(int note, float velocity, const Parameters& p);
    void stopNote(int note);
    static float oscillatorSample(double phase, double increment, int waveform, float pulseWidth = 0.5f);
    static float polyBlep(double phase, double increment);
    static float waveformSample(double phase, int waveform);
    std::array<Voice,16> voices;
    juce::Reverb reverb;
    std::vector<float> chorusBufferLeft, chorusBufferRight;
    int chorusWritePosition=0;
    double chorusPhase=0;
    std::vector<float> delayBufferLeft, delayBufferRight;
    int delayWritePosition=0;
    double currentSampleRate=44100, lastPlayedFrequency=0;
    int voiceAge=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebPlaySynthAudioProcessor)
};
