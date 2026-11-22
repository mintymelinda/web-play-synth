#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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
    require(parameter != nullptr, "stress test parameter must exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void renderStressCase(double sampleRate)
{
    WebPlaySynthAudioProcessor processor;
    constexpr int blockSize = 256;
    processor.prepareToPlay(sampleRate, blockSize);

    setParameter(processor, "osc1Level", 0.85f);
    setParameter(processor, "osc2Level", 0.75f);
    setParameter(processor, "osc1Wave", 2.0f);
    setParameter(processor, "osc2Wave", 3.0f);
    setParameter(processor, "osc2Tune", 7.0f);
    setParameter(processor, "osc2PulseWidth", 0.25f);
    setParameter(processor, "hardSync", 1.0f);
    setParameter(processor, "crossMod", 1.0f);
    setParameter(processor, "unisonVoices", 2.0f);
    setParameter(processor, "unisonDetune", 50.0f);
    setParameter(processor, "stereoSpread", 1.0f);
    setParameter(processor, "cutoff", 6500.0f);
    setParameter(processor, "resonance", 18.0f);
    setParameter(processor, "filterDrive", 24.0f);
    setParameter(processor, "filterType", 2.0f);
    setParameter(processor, "lfoRate", 12.0f);
    setParameter(processor, "lfoPitchDepth", 1.0f);
    setParameter(processor, "lfoFilterDepth", 1.0f);
    setParameter(processor, "lfoAmpDepth", 1.0f);
    setParameter(processor, "lfoPanDepth", 1.0f);
    setParameter(processor, "lfoPulseDepth", 0.45f);
    setParameter(processor, "chorusRate", 5.0f);
    setParameter(processor, "chorusDepth", 1.0f);
    setParameter(processor, "chorusMix", 1.0f);
    setParameter(processor, "delayTime", 0.08f);
    setParameter(processor, "delayFeedback", 0.85f);
    setParameter(processor, "delayMix", 1.0f);
    setParameter(processor, "reverbRoom", 1.0f);
    setParameter(processor, "reverbDamping", 0.0f);
    setParameter(processor, "reverbMix", 1.0f);
    setParameter(processor, "ampAttack", 0.001f);
    setParameter(processor, "ampDecay", 0.02f);
    setParameter(processor, "ampSustain", 1.0f);
    setParameter(processor, "ampRelease", 0.08f);
    setParameter(processor, "filterAttack", 0.001f);
    setParameter(processor, "filterDecay", 0.02f);
    setParameter(processor, "filterSustain", 1.0f);
    setParameter(processor, "filterRelease", 0.08f);

    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    double energy = 0.0;
    float peak = 0.0f;
    constexpr int blocks = 320;
    for (int block = 0; block < blocks; ++block) {
        midi.clear();
        if (block == 0) {
            for (int note = 0; note < 16; ++note)
                midi.addEvent(juce::MidiMessage::noteOn(1, 36 + note * 3, 0.72f), 0);
        }
        if (block == 160) {
            for (int note = 0; note < 16; ++note)
                midi.addEvent(juce::MidiMessage::noteOff(1, 36 + note * 3), 0);
        }
        if (block % 16 == 0) {
            setParameter(processor, "cutoff", (block % 32 == 0) ? 80.0f : 18000.0f);
            setParameter(processor, "filterType", static_cast<float>((block / 16) % 3));
            setParameter(processor, "lfoRate", (block % 32 == 0) ? 0.1f : 12.0f);
            setParameter(processor, "delayFeedback", (block % 32 == 0) ? 0.0f : 0.85f);
        }

        processor.processBlock(audio, midi);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
            for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
                const auto value = audio.getSample(channel, sample);
                require(std::isfinite(value), "full-load output must remain finite during parameter changes and release");
                peak = juce::jmax(peak, std::abs(value));
                energy += static_cast<double>(value) * value;
            }
        }
    }

    require(energy > 0.01, "stress render must contain non-silent audio");
    if (peak >= 64.0f)
        std::cerr << "stress sample rate " << sampleRate << " Hz produced peak " << peak << '\\n';
    require(peak < 64.0f, "stress render must remain bounded under maximum configured processing");
    processor.releaseResources();
}
}

int main()
{
    renderStressCase(44100.0);
    renderStressCase(48000.0);
    renderStressCase(96000.0);
    std::cout << "PASS: 16-note, four-layer DSP stress matrix at 44.1/48/96 kHz\n";
    return 0;
}
