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
    require(parameter != nullptr, "expected synth parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

double renderBlockRmsVariation(int unisonMode)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Level", 0.0f);
    setParameter(processor, "osc2Level", 1.0f);
    setParameter(processor, "osc2Tune", 12.0f);
    setParameter(processor, "hardSync", 1.0f);
    setParameter(processor, "unisonVoices", static_cast<float>(unisonMode));
    setParameter(processor, "unisonDetune", 25.0f);
    setParameter(processor, "stereoSpread", 0.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    double sum = 0.0;
    double sumSquares = 0.0;
    int count = 0;

    for (int block = 0; block < 160; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        if (block < 16)
            continue;

        double blockEnergy = 0.0;
        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(0, sample);
            require(std::isfinite(value), "hard-sync unison output must remain finite");
            blockEnergy += static_cast<double>(value) * value;
        }
        const auto blockRms = std::sqrt(blockEnergy / output.getNumSamples());
        sum += blockRms;
        sumSquares += blockRms * blockRms;
        ++count;
    }

    processor.releaseResources();
    const auto mean = sum / count;
    return std::sqrt(std::max(0.0, sumSquares / count - mean * mean));
}
}

int main()
{
    const auto singleVoiceVariation = renderBlockRmsVariation(0);
    const auto fourVoiceVariation = renderBlockRmsVariation(2);

    require(singleVoiceVariation < 0.0005,
        "single hard-sync voice should have a stable block-level RMS");
    require(fourVoiceVariation > singleVoiceVariation + 0.0005,
        "hard-sync unison layers must use their detuned master frequencies and create beating");

    std::cout << "PASS: hard-sync unison detuning and finite rendered audio\n";
    return 0;
}
