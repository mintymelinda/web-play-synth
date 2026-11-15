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

struct RenderResult {
    double energy = 0.0;
    float peak = 0.0f;
};

RenderResult renderOscillatorRoutes(bool oscillator1, bool oscillator2)
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    const auto setParameter = [&processor](const char* id, float value) {
        auto* parameter = processor.parameters.getParameter(id);
        require(parameter != nullptr, "expected audio-route parameter to exist");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };

    setParameter("vco1Enabled", oscillator1 ? 1.0f : 0.0f);
    setParameter("vco2Enabled", oscillator2 ? 1.0f : 0.0f);
    setParameter("ampEnvEnabled", 0.0f);
    setParameter("filterEnvEnabled", 0.0f);
    setParameter("filterContour", 0.0f);
    setParameter("cutoff", 18000.0f);
    setParameter("resonance", 0.1f);
    setParameter("master", 1.0f);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 48; ++block) {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        processor.processBlock(output, midi);
    }

    RenderResult result;
    for (int channel = 0; channel < output.getNumChannels(); ++channel) {
        for (int sample = 0; sample < output.getNumSamples(); ++sample) {
            const auto value = output.getSample(channel, sample);
            require(std::isfinite(value), "oscillator route output must remain finite");
            result.energy += static_cast<double>(value) * value;
            result.peak = juce::jmax(result.peak, std::abs(value));
        }
    }

    processor.releaseResources();
    return result;
}
}

int main()
{
    const auto oscillator1Only = renderOscillatorRoutes(true, false);
    const auto oscillator2Only = renderOscillatorRoutes(false, true);
    const auto bothDisabled = renderOscillatorRoutes(false, false);

    require(oscillator1Only.energy > 0.01,
        "VCO 1 route must independently deliver oscillator audio to the filter");
    require(oscillator2Only.energy > 0.01,
        "VCO 2 route must independently deliver oscillator audio to the filter");
    require(bothDisabled.peak < 0.000001f,
        "disabling both oscillator routes must produce silence");

    std::cout << "PASS: independent VCO audio routes and complete oscillator mute\n";
    return 0;
}
