#include "PluginProcessor.h"
#include "Dsp/Oscillator.h"
#include "Dsp/Envelope.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <vector>

struct WebPlaySynthAudioProcessorTestAccess {
    static void startVoice(WebPlaySynthAudioProcessor& processor)
    {
        processor.prepareToPlay(48000.0, 128);
        const auto parameters = processor.readParameters();
        processor.voices[0].start(69, 0.8f, 1, 0.0f, 0.0f, parameters, 0.0);
    }

    static void renderVoiceSample(WebPlaySynthAudioProcessor& processor, bool noteOff,
        float& left, float& right, float& oscillatorLeft, float& oscillatorRight)
    {
        const auto parameters = processor.readParameters();
        auto& voice = processor.voices[0];
        if (noteOff)
            voice.stop();
        voice.render(parameters, 48000.0, left, right, &oscillatorLeft, &oscillatorRight);
    }

    static void printVoiceState(const WebPlaySynthAudioProcessor& processor)
    {
        const auto& voice = processor.voices[0];
        std::cerr << "voice-state: currentCutoff=" << voice.currentCutoff
                  << ", currentResonance=" << voice.currentResonance
                  << ", appliedResonance=" << voice.appliedResonance
                  << ", currentFrequency=" << voice.currentFrequency
                  << ", lfoPhase=" << voice.lfoPhase
                  << ", oscillatorPhase=" << voice.phase1[0] << '\n';
    }
};

namespace {
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

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
    require(parameter != nullptr, "expected DSP parity parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
};

template <typename Configure>
StereoRender render(Configure configure, int blockCount, int noteOffSample = -1)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    configure(processor);

    StereoRender result;
    result.left.reserve(static_cast<size_t>(blockCount * blockSize));
    result.right.reserve(static_cast<size_t>(blockCount * blockSize));

    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < blockCount; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);

        const auto blockStart = block * blockSize;
        if (noteOffSample >= blockStart && noteOffSample < blockStart + blockSize)
            midi.addEvent(juce::MidiMessage::noteOff(1, 69), noteOffSample - blockStart);

        processor.processBlock(output, midi);
        for (int sample = 0; sample < blockSize; ++sample) {
            const auto left = output.getSample(0, sample);
            const auto right = output.getSample(1, sample);
            require(std::isfinite(left) && std::isfinite(right),
                "rendered parity samples must remain finite");
            result.left.push_back(left);
            result.right.push_back(right);
        }
    }

    processor.releaseResources();
    return result;
}

void configureCommon(WebPlaySynthAudioProcessor& processor)
{
    setParameter(processor, "osc1Wave", 0.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "filterContour", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "lfoWave", 0.0f);
    setParameter(processor, "lfoRate", 4.5f);
    setParameter(processor, "lfoPitchDepth", 0.0f);
    setParameter(processor, "lfoFilterDepth", 0.0f);
    setParameter(processor, "lfoAmpDepth", 0.0f);
    setParameter(processor, "lfoPanDepth", 0.0f);
    setParameter(processor, "lfoPulseDepth", 0.0f);
}


void compareAdsrSequence(const char* name, const std::function<void(juce::ADSR&, webplaysynth::dsp::Adsr&)>& sequence)
{
    juce::ADSR juceEnvelope;
    webplaysynth::dsp::Adsr portableEnvelope;
    juceEnvelope.setSampleRate(sampleRate);
    portableEnvelope.setSampleRate(sampleRate);

    sequence(juceEnvelope, portableEnvelope);
    require(juceEnvelope.isActive() == portableEnvelope.isActive(), name);
}

void advanceAndCompare(juce::ADSR& reference, webplaysynth::dsp::Adsr& candidate, int count, const char* name)
{
    for (int i = 0; i < count; ++i) {
        const auto expected = reference.getNextSample();
        const auto actual = candidate.getNextSample();
        require(expected == actual, name);
        require(reference.isActive() == candidate.isActive(), "ADSR active state must match JUCE");
    }
}

void testPortableAdsrMatchesJuceStateMachine()
{
    compareAdsrSequence("standard ADSR sequence must match JUCE", [](juce::ADSR& reference, webplaysynth::dsp::Adsr& candidate) {
        const juce::ADSR::Parameters parameters { 0.007f, 0.012f, 0.35f, 0.02f };
        const webplaysynth::dsp::Adsr::Parameters portableParameters { 0.007f, 0.012f, 0.35f, 0.02f };
        reference.setParameters(parameters);
        candidate.setParameters(portableParameters);
        reference.noteOn();
        candidate.noteOn();
        advanceAndCompare(reference, candidate, 400, "attack/decay/sustain samples must match JUCE");
        reference.noteOff();
        candidate.noteOff();
        advanceAndCompare(reference, candidate, 1200, "release samples must match JUCE");
    });

    compareAdsrSequence("zero-time stages must match JUCE", [](juce::ADSR& reference, webplaysynth::dsp::Adsr& candidate) {
        const juce::ADSR::Parameters parameters { 0.0f, 0.0f, 0.4f, 0.0f };
        const webplaysynth::dsp::Adsr::Parameters portableParameters { 0.0f, 0.0f, 0.4f, 0.0f };
        reference.setParameters(parameters);
        candidate.setParameters(portableParameters);
        reference.noteOn();
        candidate.noteOn();
        advanceAndCompare(reference, candidate, 8, "zero-time stage samples must match JUCE");
        reference.noteOff();
        candidate.noteOff();
        advanceAndCompare(reference, candidate, 8, "zero-time release must match JUCE");
    });

    compareAdsrSequence("retrigger during release must match JUCE", [](juce::ADSR& reference, webplaysynth::dsp::Adsr& candidate) {
        const juce::ADSR::Parameters parameters { 0.01f, 0.02f, 0.5f, 0.2f };
        const webplaysynth::dsp::Adsr::Parameters portableParameters { 0.01f, 0.02f, 0.5f, 0.2f };
        reference.setParameters(parameters);
        candidate.setParameters(portableParameters);
        reference.noteOn();
        candidate.noteOn();
        advanceAndCompare(reference, candidate, 180, "pre-release samples must match JUCE");
        reference.noteOff();
        candidate.noteOff();
        advanceAndCompare(reference, candidate, 24, "release prefix must match JUCE");
        reference.noteOn();
        candidate.noteOn();
        advanceAndCompare(reference, candidate, 240, "retriggered attack must preserve JUCE envelope value");
    });

    compareAdsrSequence("parameter changes during release must match JUCE", [](juce::ADSR& reference, webplaysynth::dsp::Adsr& candidate) {
        const juce::ADSR::Parameters initial { 0.01f, 0.02f, 0.5f, 0.2f };
        const webplaysynth::dsp::Adsr::Parameters portableInitial { 0.01f, 0.02f, 0.5f, 0.2f };
        reference.setParameters(initial);
        candidate.setParameters(portableInitial);
        reference.noteOn();
        candidate.noteOn();
        advanceAndCompare(reference, candidate, 220, "pre-release parameter-change samples must match JUCE");
        reference.noteOff();
        candidate.noteOff();
        advanceAndCompare(reference, candidate, 12, "release prefix before parameter change must match JUCE");
        const juce::ADSR::Parameters changed { 0.03f, 0.04f, 0.25f, 0.15f };
        const webplaysynth::dsp::Adsr::Parameters portableChanged { 0.03f, 0.04f, 0.25f, 0.15f };
        reference.setParameters(changed);
        candidate.setParameters(portableChanged);
        advanceAndCompare(reference, candidate, 400, "release after parameter change must match JUCE");
    });
}

void testAmpEnvelopeMatchesJuceAdsr()
{
    constexpr float attack = 0.007f;
    constexpr float decay = 0.012f;
    constexpr float sustain = 0.35f;
    constexpr float release = 0.02f;
    constexpr int noteOffSample = 240;
    constexpr int blockCount = 6;

    const auto configure = [](bool enabled) {
        return [enabled](WebPlaySynthAudioProcessor& processor) {
            configureCommon(processor);
            setParameter(processor, "ampEnvEnabled", enabled ? 1.0f : 0.0f);
            setParameter(processor, "ampAttack", attack);
            setParameter(processor, "ampDecay", decay);
            setParameter(processor, "ampSustain", sustain);
            setParameter(processor, "ampRelease", release);
        };
    };

    const auto dry = render(configure(false), blockCount, noteOffSample);
    const auto enveloped = render(configure(true), blockCount, noteOffSample);

    juce::ADSR reference;
    reference.setSampleRate(sampleRate);
    reference.setParameters({ attack, decay, sustain, release });
    reference.noteOn();

    for (int sample = 0; sample < blockCount * blockSize; ++sample) {
        if (sample == noteOffSample)
            reference.noteOff();

        const auto envelope = reference.getNextSample();
        const auto expectedLeft = dry.left[static_cast<size_t>(sample)] * envelope;
        const auto expectedRight = dry.right[static_cast<size_t>(sample)] * envelope;
        require(std::abs(enveloped.left[static_cast<size_t>(sample)] - expectedLeft) < 2.0e-6f,
            "amp-envelope rendered left channel must match JUCE ADSR sample-for-sample");
        require(std::abs(enveloped.right[static_cast<size_t>(sample)] - expectedRight) < 2.0e-6f,
            "amp-envelope rendered right channel must match JUCE ADSR sample-for-sample");
    }
}

void testFilterEnvelopeRenderedOutputMatchesJuceAdsr()
{
    constexpr float attack = 0.007f;
    constexpr float decay = 0.012f;
    constexpr float sustain = 0.35f;
    constexpr float release = 0.02f;
    constexpr float baseCutoff = 1200.0f;
    constexpr float contour = 0.45f;
    constexpr int noteOffSample = 240;
    constexpr int sampleCount = 6 * blockSize;

    WebPlaySynthAudioProcessor processor;
    configureCommon(processor);
    setParameter(processor, "filterEnvEnabled", 1.0f);
    setParameter(processor, "filterContour", contour);
    setParameter(processor, "cutoff", baseCutoff);
    setParameter(processor, "filterAttack", attack);
    setParameter(processor, "filterDecay", decay);
    setParameter(processor, "filterSustain", sustain);
    setParameter(processor, "filterRelease", release);
    WebPlaySynthAudioProcessorTestAccess::startVoice(processor);

    juce::ADSR referenceEnvelope;
    referenceEnvelope.setSampleRate(sampleRate);
    referenceEnvelope.setParameters({ attack, decay, sustain, release });
    referenceEnvelope.noteOn();

    juce::dsp::StateVariableTPTFilter<float> leftFilter;
    juce::dsp::StateVariableTPTFilter<float> rightFilter;
    const juce::dsp::ProcessSpec spec {
        sampleRate, static_cast<juce::uint32>(blockSize), 1
    };
    leftFilter.prepare(spec);
    rightFilter.prepare(spec);
    leftFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    rightFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    leftFilter.reset();
    rightFilter.reset();
    leftFilter.setResonance(0.1f);
    rightFilter.setResonance(0.1f);

    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto noteOff = sample == noteOffSample;
        if (noteOff)
            referenceEnvelope.noteOff();

        float actualLeft = 0.0f;
        float actualRight = 0.0f;
        float oscillatorLeft = 0.0f;
        float oscillatorRight = 0.0f;
        WebPlaySynthAudioProcessorTestAccess::renderVoiceSample(processor, noteOff,
            actualLeft, actualRight, oscillatorLeft, oscillatorRight);

        const auto envelope = referenceEnvelope.getNextSample();
        const auto cutoff = juce::jlimit(30.0f, 19000.0f,
            baseCutoff * static_cast<float>(std::pow(2.0, contour * envelope * 3.0)));
        leftFilter.setCutoffFrequency(cutoff);
        rightFilter.setCutoffFrequency(cutoff);

        const auto expectedLeft = leftFilter.processSample(0, oscillatorLeft) * 0.8f;
        const auto expectedRight = rightFilter.processSample(0, oscillatorRight) * 0.8f;
        require(std::abs(actualLeft - expectedLeft) < 3.0e-6f,
            "filter-envelope rendered left channel must match JUCE ADSR and TPT filter using the actual oscillator signal");
        require(std::abs(actualRight - expectedRight) < 3.0e-6f,
            "filter-envelope rendered right channel must match JUCE ADSR and TPT filter using the actual oscillator signal");
    }
    processor.releaseResources();
}

void assertLfoDestinationMatchesRenderedReference(
    int oscillatorWave,
    float pitchDepth,
    float filterDepth,
    float ampDepth,
    float panDepth,
    float pulseDepth,
    float baseCutoff,
    const char* scenario)
{
    constexpr float rate = 4.5f;
    constexpr float velocity = 0.8f;
    constexpr int blockCount = 128;

    const auto actual = render([=](WebPlaySynthAudioProcessor& processor) {
        configureCommon(processor);
        setParameter(processor, "osc1Wave", static_cast<float>(oscillatorWave));
        setParameter(processor, "osc1Level", 1.0f);
        setParameter(processor, "osc1PulseWidth", 0.5f);
        setParameter(processor, "cutoff", baseCutoff);
        setParameter(processor, "lfoRate", rate);
        setParameter(processor, "lfoPitchDepth", pitchDepth);
        setParameter(processor, "lfoFilterDepth", filterDepth);
        setParameter(processor, "lfoAmpDepth", ampDepth);
        setParameter(processor, "lfoPanDepth", panDepth);
        setParameter(processor, "lfoPulseDepth", pulseDepth);
    }, blockCount);

    juce::dsp::StateVariableTPTFilter<float> leftFilter;
    juce::dsp::StateVariableTPTFilter<float> rightFilter;
    const juce::dsp::ProcessSpec spec {
        sampleRate, static_cast<juce::uint32>(blockSize), 1
    };
    leftFilter.prepare(spec);
    rightFilter.prepare(spec);
    leftFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    rightFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    leftFilter.reset();
    rightFilter.reset();
    leftFilter.setResonance(0.1f);
    rightFilter.setResonance(0.1f);

    double lfoPhase = 0.0;
    double oscillatorPhase = 0.0;
    for (int sample = 0; sample < blockCount * blockSize; ++sample) {
        const auto lfo = webplaysynth::dsp::waveformSample(lfoPhase, 0);
        const auto modulatedFrequency = juce::jlimit(
            10.0, 20000.0, 440.0 + static_cast<double>(lfo * pitchDepth));
        const auto oscillatorIncrement = modulatedFrequency / sampleRate;
        const auto pulseWidth = juce::jlimit(0.05f, 0.95f, 0.5f + lfo * pulseDepth);
        const auto oscillator = webplaysynth::dsp::oscillatorSample(
            oscillatorPhase, oscillatorIncrement, oscillatorWave, pulseWidth);

        float inputLeft = oscillator;
        float inputRight = oscillator;
        if (panDepth > 0.0f) {
            const auto pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(lfo * panDepth));
            const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const auto panScale = juce::MathConstants<float>::sqrt2;
            inputLeft *= std::cos(angle) * panScale;
            inputRight *= std::sin(angle) * panScale;
        }

        const auto cutoff = juce::jlimit(30.0f, 19000.0f, baseCutoff + lfo * filterDepth);
        leftFilter.setCutoffFrequency(cutoff);
        rightFilter.setCutoffFrequency(cutoff);
        const auto ampGain = (1.0f + lfo * ampDepth) / (1.0f + ampDepth);
        const auto expectedLeft = leftFilter.processSample(0, inputLeft) * ampGain * velocity;
        const auto expectedRight = rightFilter.processSample(0, inputRight) * ampGain * velocity;

        const auto leftError = std::abs(actual.left[static_cast<size_t>(sample)] - expectedLeft);
        const auto rightError = std::abs(actual.right[static_cast<size_t>(sample)] - expectedRight);
        if (leftError >= 4.0e-6f || rightError >= 4.0e-6f) {
            std::cerr << "LFO parity mismatch: " << scenario
                      << ", sample=" << sample
                      << ", actualL=" << actual.left[static_cast<size_t>(sample)]
                      << ", expectedL=" << expectedLeft
                      << ", actualR=" << actual.right[static_cast<size_t>(sample)]
                      << ", expectedR=" << expectedRight
                      << ", leftError=" << leftError
                      << ", rightError=" << rightError
                      << ", lfo=" << lfo
                      << ", oscillator=" << oscillator
                      << ", cutoff=" << cutoff
                      << ", ampGain=" << ampGain << '\n';
            WebPlaySynthAudioProcessor diagnosticProcessor;
            diagnosticProcessor.prepareToPlay(sampleRate, blockSize);
            configureCommon(diagnosticProcessor);
            setParameter(diagnosticProcessor, "osc1Wave", static_cast<float>(oscillatorWave));
            setParameter(diagnosticProcessor, "osc1Level", 1.0f);
            setParameter(diagnosticProcessor, "osc1PulseWidth", 0.5f);
            setParameter(diagnosticProcessor, "cutoff", baseCutoff);
            setParameter(diagnosticProcessor, "lfoRate", rate);
            setParameter(diagnosticProcessor, "lfoPitchDepth", pitchDepth);
            setParameter(diagnosticProcessor, "lfoFilterDepth", filterDepth);
            setParameter(diagnosticProcessor, "lfoAmpDepth", ampDepth);
            setParameter(diagnosticProcessor, "lfoPanDepth", panDepth);
            setParameter(diagnosticProcessor, "lfoPulseDepth", pulseDepth);
            WebPlaySynthAudioProcessorTestAccess::startVoice(diagnosticProcessor);
            float directLeft = 0.0f, directRight = 0.0f;
            float directOscillatorLeft = 0.0f, directOscillatorRight = 0.0f;
            for (int directSample = 0; directSample <= sample; ++directSample)
                WebPlaySynthAudioProcessorTestAccess::renderVoiceSample(diagnosticProcessor, false,
                    directLeft, directRight, directOscillatorLeft, directOscillatorRight);
            std::cerr << "direct-voice: left=" << directLeft << ", right=" << directRight
                      << ", oscillatorLeft=" << directOscillatorLeft
                      << ", oscillatorRight=" << directOscillatorRight << '\n';
            WebPlaySynthAudioProcessorTestAccess::printVoiceState(diagnosticProcessor);
        }
        require(leftError < 4.0e-6f,
            "LFO destination rendered left channel must match the explicit reference path");
        require(rightError < 4.0e-6f,
            "LFO destination rendered right channel must match the explicit reference path");

        oscillatorPhase += oscillatorIncrement;
        if (oscillatorPhase >= 1.0)
            oscillatorPhase -= std::floor(oscillatorPhase);

        lfoPhase += static_cast<double>(rate) / sampleRate;
        if (lfoPhase >= 1.0)
            lfoPhase -= std::floor(lfoPhase);
    }
}

void testLfoDestinationsMatchRenderedReference()
{
    assertLfoDestinationMatchesRenderedReference(0, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f, 18000.0f, "pitch");
    assertLfoDestinationMatchesRenderedReference(0, 0.0f, 3000.0f, 0.0f, 0.0f, 0.0f, 1200.0f, "filter");
    assertLfoDestinationMatchesRenderedReference(0, 0.0f, 0.0f, 0.65f, 0.0f, 0.0f, 18000.0f, "amplitude");
    assertLfoDestinationMatchesRenderedReference(0, 0.0f, 0.0f, 0.0f, 0.85f, 0.0f, 18000.0f, "pan");
    assertLfoDestinationMatchesRenderedReference(3, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 18000.0f, "pulse-width");
}

void testLfoAmplitudeMatchesRenderedReference()
{
    constexpr float rate = 4.5f;
    constexpr float depth = 0.65f;
    constexpr int blockCount = 8;

    const auto dry = render([](WebPlaySynthAudioProcessor& processor) {
        configureCommon(processor);
    }, blockCount);

    const auto modulated = render([](WebPlaySynthAudioProcessor& processor) {
        configureCommon(processor);
        setParameter(processor, "lfoAmpDepth", depth);
    }, blockCount);

    double phase = 0.0;
    const auto phaseIncrement = static_cast<double>(rate) / sampleRate;
    for (int sample = 0; sample < blockCount * blockSize; ++sample) {
        const auto lfo = webplaysynth::dsp::waveformSample(phase, 0);
        const auto gain = (1.0f + lfo * depth) / (1.0f + depth);
        const auto expectedLeft = dry.left[static_cast<size_t>(sample)] * gain;
        const auto expectedRight = dry.right[static_cast<size_t>(sample)] * gain;
        require(std::abs(modulated.left[static_cast<size_t>(sample)] - expectedLeft) < 2.0e-6f,
            "LFO amplitude rendered left channel must match its phase/gain reference");
        require(std::abs(modulated.right[static_cast<size_t>(sample)] - expectedRight) < 2.0e-6f,
            "LFO amplitude rendered right channel must match its phase/gain reference");

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= std::floor(phase);
    }
}
}

int main()
{
    testPortableAdsrMatchesJuceStateMachine();
    testAmpEnvelopeMatchesJuceAdsr();
    testFilterEnvelopeRenderedOutputMatchesJuceAdsr();
    testLfoAmplitudeMatchesRenderedReference();
    testLfoDestinationsMatchRenderedReference();
    std::cout << "PASS: JUCE ADSR and LFO amplitude rendered-output parity\n";
    return 0;
}
