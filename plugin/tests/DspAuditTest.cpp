#include "PluginProcessor.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
std::atomic<bool> countAllocations { false };
std::atomic<unsigned long long> allocationCount { 0 };
}

void* operator new(std::size_t size)
{
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        if (countAllocations.load(std::memory_order_relaxed))
            allocationCount.fetch_add(1, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        if (countAllocations.load(std::memory_order_relaxed))
            allocationCount.fetch_add(1, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

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
    require(parameter != nullptr, "audit parameter must exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct RenderMetrics {
    double activeEnergy = 0.0;
    double tailEnergy = 0.0;
    float peak = 0.0f;
};

RenderMetrics renderEffects(bool chorus, bool delay, bool reverb, bool modulation)
{
    WebPlaySynthAudioProcessor processor;
    constexpr int blockSize = 256;
    processor.prepareToPlay(48000.0, blockSize);

    setParameter(processor, "master", 1.0f);
    setParameter(processor, "ampAttack", 0.005f);
    setParameter(processor, "ampDecay", 0.02f);
    setParameter(processor, "ampSustain", 0.8f);
    setParameter(processor, "ampRelease", 0.08f);
    setParameter(processor, "filterContour", 0.35f);
    setParameter(processor, "cutoff", 5000.0f);
    setParameter(processor, "resonance", 1.4f);
    setParameter(processor, "chorusRate", 1.7f);
    setParameter(processor, "chorusDepth", 0.8f);
    setParameter(processor, "chorusMix", chorus ? 0.55f : 0.0f);
    setParameter(processor, "delayTime", 0.08f);
    setParameter(processor, "delayFeedback", 0.72f);
    setParameter(processor, "delayMix", delay ? 0.6f : 0.0f);
    setParameter(processor, "reverbRoom", 0.82f);
    setParameter(processor, "reverbDamping", 0.22f);
    setParameter(processor, "reverbMix", reverb ? 0.58f : 0.0f);
    setParameter(processor, "lfoRate", 3.7f);
    setParameter(processor, "lfoFilterDepth", modulation ? 1800.0f : 0.0f);
    setParameter(processor, "lfoAmpDepth", modulation ? 0.65f : 0.0f);
    setParameter(processor, "lfoPanDepth", modulation ? 0.9f : 0.0f);
    setParameter(processor, "lfoPulseDepth", modulation ? 0.25f : 0.0f);

    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;
    RenderMetrics metrics;

    for (int block = 0; block < 120; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        if (block == 30)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

        if (block == 55 && reverb) {
            setParameter(processor, "reverbRoom", 0.35f);
            setParameter(processor, "reverbDamping", 0.9f);
        }
        if (block == 65 && delay) {
            setParameter(processor, "delayTime", 0.21f);
            setParameter(processor, "delayFeedback", 0.4f);
        }

        processor.processBlock(output, midi);
        for (int channel = 0; channel < output.getNumChannels(); ++channel) {
            for (int sample = 0; sample < output.getNumSamples(); ++sample) {
                const auto value = output.getSample(channel, sample);
                require(std::isfinite(value), "effect and modulation combinations must remain finite");
                metrics.peak = juce::jmax(metrics.peak, std::abs(value));
                const auto energy = static_cast<double>(value) * value;
                if (block < 30)
                    metrics.activeEnergy += energy;
                if (block >= 55)
                    metrics.tailEnergy += energy;
            }
        }
    }

    processor.releaseResources();
    return metrics;
}

void testEffectAndModulationInteractions()
{
    const auto dry = renderEffects(false, false, false, false);
    const auto chorus = renderEffects(true, false, false, false);
    const auto delay = renderEffects(false, true, false, false);
    const auto reverb = renderEffects(false, false, true, false);
    const auto combined = renderEffects(true, true, true, true);

    require(dry.activeEnergy > 0.01, "dry reference render must contain audio");
    require(std::abs(chorus.activeEnergy - dry.activeEnergy) > 0.001,
        "chorus wet mix must change the active signal");
    require(std::abs(delay.activeEnergy - dry.activeEnergy) > 0.001,
        "delay wet mix must change the active signal");
    require(std::abs(reverb.activeEnergy - dry.activeEnergy) > 0.001,
        "reverb wet mix must change the active signal");
    require(delay.tailEnergy > dry.tailEnergy + 0.001,
        "delay feedback must remain audible after the source voice releases");
    require(reverb.tailEnergy > dry.tailEnergy + 0.001,
        "reverb tail must remain audible after the source voice releases");
    require(std::abs(combined.activeEnergy - dry.activeEnergy) > 0.001,
        "simultaneous effects and LFO destinations must affect the signal");
    require(combined.peak < 64.0f, "combined effects and modulation must remain bounded");

    std::cout << "PASS: chorus/delay/reverb interactions, live parameter changes, "
                 "LFO destinations, and effect tails\n";
}

void testParameterBoundariesAndDenormals()
{
    WebPlaySynthAudioProcessor processor;
    constexpr int blockSize = 256;
    processor.prepareToPlay(96000.0, blockSize);

    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);

    for (auto* parameter : processor.getParameters())
        parameter->setValueNotifyingHost(0.0f);

    processor.processBlock(output, midi);
    midi.clear();

    for (auto* parameter : processor.getParameters())
        parameter->setValueNotifyingHost(1.0f);

    unsigned int subnormalSamples = 0;
    float peak = 0.0f;
    for (int block = 0; block < 1400; ++block) {
        if (block == 180)
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
        processor.processBlock(output, midi);
        midi.clear();

        for (int channel = 0; channel < output.getNumChannels(); ++channel) {
            for (int sample = 0; sample < output.getNumSamples(); ++sample) {
                const auto value = output.getSample(channel, sample);
                require(std::isfinite(value), "minimum/maximum parameter states must remain finite");
                peak = juce::jmax(peak, std::abs(value));
                if (std::fpclassify(value) == FP_SUBNORMAL)
                    ++subnormalSamples;
            }
        }
    }

    require(peak < 64.0f, "maximum parameter values must remain within the established stress bound");
    require(subnormalSamples == 0, "audio callback must flush denormal output during long effect tails");
    processor.releaseResources();

    std::cout << "PASS: all-parameter min/max boundary sweep and denormal-free long-tail render\n";
}

void configureFullLoad(WebPlaySynthAudioProcessor& processor)
{
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "unisonVoices", 2.0f);
    setParameter(processor, "unisonDetune", 50.0f);
    setParameter(processor, "stereoSpread", 1.0f);
    setParameter(processor, "hardSync", 1.0f);
    setParameter(processor, "crossMod", 800.0f);
    setParameter(processor, "filterType", 2.0f);
    setParameter(processor, "filterDrive", 24.0f);
    setParameter(processor, "cutoff", 12000.0f);
    setParameter(processor, "resonance", 18.0f);
    setParameter(processor, "lfoRate", 20.0f);
    setParameter(processor, "lfoPitchDepth", 25.0f);
    setParameter(processor, "lfoFilterDepth", 5000.0f);
    setParameter(processor, "lfoAmpDepth", 1.0f);
    setParameter(processor, "lfoPanDepth", 1.0f);
    setParameter(processor, "lfoPulseDepth", 0.45f);
    setParameter(processor, "chorusRate", 5.0f);
    setParameter(processor, "chorusDepth", 1.0f);
    setParameter(processor, "chorusMix", 1.0f);
    setParameter(processor, "delayTime", 0.08f);
    setParameter(processor, "delayFeedback", 0.85f);
    setParameter(processor, "delayMix", 1.0f);
    setParameter(processor, "reverbRoom", 0.95f);
    setParameter(processor, "reverbDamping", 0.0f);
    setParameter(processor, "reverbMix", 1.0f);
    setParameter(processor, "ampAttack", 0.005f);
    setParameter(processor, "ampDecay", 0.05f);
    setParameter(processor, "ampSustain", 1.0f);
    setParameter(processor, "filterAttack", 0.005f);
    setParameter(processor, "filterDecay", 0.05f);
    setParameter(processor, "filterSustain", 1.0f);
}

void testNoAudioThreadAllocations()
{
    WebPlaySynthAudioProcessor processor;
    constexpr int blockSize = 256;
    processor.prepareToPlay(48000.0, blockSize);
    configureFullLoad(processor);

    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 0; note < 16; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + note * 3, 0.7f), 0);

    processor.processBlock(output, midi);
    midi.clear();

    processor.setMidiCcLearnTarget("master");
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 64), 0);
    processor.processBlock(output, midi);
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 96), 0);
    for (int block = 0; block < 16; ++block)
        processor.processBlock(output, midi);

    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (int block = 0; block < 128; ++block) {
        midi.clear();
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 96), 0);
        processor.processBlock(output, midi);
    }
    countAllocations.store(false, std::memory_order_relaxed);

    const auto allocations = allocationCount.load(std::memory_order_relaxed);
    require(allocations == 0, "steady-state processBlock must not allocate heap memory");
    processor.releaseResources();

    std::cout << "PASS: zero steady-state heap allocations across 128 full-load audio blocks\n";
}

void testPolyphonyAndEffectsPerformance()
{
    WebPlaySynthAudioProcessor processor;
    constexpr int blockSize = 256;
    constexpr double sampleRate = 48000.0;
    processor.prepareToPlay(sampleRate, blockSize);
    configureFullLoad(processor);

    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 0; note < 16; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + note * 3, 0.7f), 0);

    processor.processBlock(output, midi);
    midi.clear();
    for (int block = 0; block < 48; ++block)
        processor.processBlock(output, midi);

    constexpr int measuredBlocks = 256;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < measuredBlocks; ++block)
        processor.processBlock(output, midi);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto audioDuration = measuredBlocks * blockSize / sampleRate;
    const auto realtimeRatio = elapsed / audioDuration;

    require(realtimeRatio < 1.0,
        "16 voices, four-layer unison, modulation, and all effects must render faster than real time");
    processor.releaseResources();

    std::cout << "PASS: full-load DSP rendered at " << realtimeRatio
              << "x real-time (lower is faster)\n";
}
}

int main()
{
    testEffectAndModulationInteractions();
    testParameterBoundariesAndDenormals();
    testNoAudioThreadAllocations();
    testPolyphonyAndEffectsPerformance();
    return 0;
}
