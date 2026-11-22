#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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
    require(parameter != nullptr, "expected PWM test parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

std::vector<float> renderSquare(float pulseWidth, float pwmDepth)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    setParameter(processor, "osc1Wave", 3.0f);
    setParameter(processor, "osc1Level", 1.0f);
    setParameter(processor, "osc1PulseWidth", pulseWidth);
    setParameter(processor, "osc2Level", 0.0f);
    setParameter(processor, "master", 1.0f);
    setParameter(processor, "cutoff", 18000.0f);
    setParameter(processor, "resonance", 0.1f);
    setParameter(processor, "ampEnvEnabled", 0.0f);
    setParameter(processor, "filterEnvEnabled", 0.0f);
    setParameter(processor, "lfoWave", 0.0f);
    setParameter(processor, "lfoRate", 4.5f);
    setParameter(processor, "lfoPulseDepth", pwmDepth);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> samples;
    samples.reserve(128 * 512);

    for (int block = 0; block < 128; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        processor.processBlock(output, midi);

        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(0, sample);
            require(std::isfinite(value), "PWM output must remain finite");
            samples.push_back(value);
        }
    }

    processor.releaseResources();
    return samples;
}

double differenceEnergy(const std::vector<float>& first, const std::vector<float>& second)
{
    require(first.size() == second.size(), "rendered PWM buffers must have equal lengths");
    double result = 0.0;
    for (size_t index = 0; index < first.size(); ++index) {
        const auto delta = first[index] - second[index];
        result += static_cast<double>(delta) * delta;
    }
    return result;
}
}

int main()
{
    const auto halfWidth = renderSquare(0.5f, 0.0f);
    const auto narrowPulse = renderSquare(0.25f, 0.0f);
    require(differenceEnergy(halfWidth, narrowPulse) > 0.01,
        "static pulse width must change the rendered square-wave shape");

    const auto staticPulse = renderSquare(0.5f, 0.0f);
    const auto modulatedPulse = renderSquare(0.5f, 0.2f);
    require(differenceEnergy(staticPulse, modulatedPulse) > 0.01,
        "LFO pulse-width depth must modulate the rendered square-wave shape");

    std::cout << "PASS: static pulse width and LFO PWM affect rendered audio\n";
    return 0;
}
