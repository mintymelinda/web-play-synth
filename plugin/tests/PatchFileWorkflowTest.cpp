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

float parameterValue(WebPlaySynthAudioProcessor& processor, const char* id)
{
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, "expected patch parameter to exist");
    return parameter->getValue();
}

void setParameter(WebPlaySynthAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter(id);
    require(parameter != nullptr, "expected patch parameter to exist");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

int main()
{
    WebPlaySynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);

    setParameter(processor, "cutoff", 6400.0f);
    setParameter(processor, "lfoAmpDepth", 0.37f);
    setParameter(processor, "delayMix", 0.42f);
    processor.setMidiCcLearnTarget("cutoff");

    juce::AudioBuffer<float> output(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 96), 0);
    processor.processBlock(output, midi);

    require(processor.getMidiCcMappingForParameter("cutoff") == "74",
        "MIDI CC learn assignment must be present before saving");
    const auto savedCutoff = parameterValue(processor, "cutoff");
    const auto savedAmpDepth = parameterValue(processor, "lfoAmpDepth");
    const auto savedDelayMix = parameterValue(processor, "delayMix");

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("WebPlaySynthPatchFileWorkflowTest");
    directory.createDirectory();
    const auto patch = directory.getChildFile("round-trip.wpsynth");
    const auto invalid = directory.getChildFile("invalid.wpsynth");
    invalid.replaceWithText("not a Web Play Synth patch");

    require(processor.savePatchFile(patch), "valid patch state must save to disk");
    require(patch.existsAsFile() && patch.getSize() > 0,
        "saved patch file must exist and contain state");

    setParameter(processor, "cutoff", 900.0f);
    setParameter(processor, "lfoAmpDepth", 0.0f);
    setParameter(processor, "delayMix", 0.0f);
    processor.clearMidiCcMapping("cutoff");

    require(processor.loadPatchFile(patch), "saved patch must load successfully");
    require(std::abs(parameterValue(processor, "cutoff") - savedCutoff) < 0.001f,
        "loading a patch must restore filter cutoff");
    require(std::abs(parameterValue(processor, "lfoAmpDepth") - savedAmpDepth) < 0.001f,
        "loading a patch must restore modulation parameters");
    require(std::abs(parameterValue(processor, "delayMix") - savedDelayMix) < 0.001f,
        "loading a patch must restore effect parameters");
    require(processor.getMidiCcMappingForParameter("cutoff") == "74",
        "loading a patch must restore MIDI CC assignments");

    const auto restoredCutoff = parameterValue(processor, "cutoff");
    require(!processor.loadPatchFile(invalid),
        "invalid patch files must be rejected");
    require(std::abs(parameterValue(processor, "cutoff") - restoredCutoff) < 0.001f,
        "rejecting an invalid patch must leave the current sound unchanged");
    require(!processor.loadPatchFile(directory.getChildFile("missing.wpsynth")),
        "missing patch files must be rejected");

    patch.deleteFile();
    invalid.deleteFile();
    directory.deleteRecursively();
    processor.releaseResources();

    std::cout << "PASS: native patch save/load, parameter restore, MIDI CC restore, and invalid-file rejection\n";
    return 0;
}
