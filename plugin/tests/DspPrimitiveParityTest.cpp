#include "PluginProcessor.h"
#include "Dsp/Oscillator.h"
#include "Dsp/Envelope.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <vector>

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
    testLfoAmplitudeMatchesRenderedReference();
    std::cout << "PASS: JUCE ADSR and LFO amplitude rendered-output parity\n";
    return 0;
}
