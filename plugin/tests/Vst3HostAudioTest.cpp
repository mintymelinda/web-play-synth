#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool condition, const juce::String& message)
{
    if (! condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

float outputPeak(const juce::AudioBuffer<float>& buffer, int outputChannels)
{
    float peak = 0.0f;
    for (int channel = 0; channel < outputChannels; ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            peak = juce::jmax(peak, std::abs(buffer.getSample(channel, sample)));
    return peak;
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    require(argc == 2, "usage: WebPlaySynthVst3HostAudioTest <path-to-vst3-bundle>");

    const juce::File pluginBundle(argv[1]);
    require(pluginBundle.isDirectory() && pluginBundle.hasFileExtension(".vst3"),
        "the supplied path must be the built VST3 bundle");

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    juce::VST3PluginFormat vst3Format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3Format.findAllTypesForFile(descriptions, pluginBundle.getFullPathName());
    require(! descriptions.isEmpty(), "JUCE host must discover the built VST3");

    const juce::PluginDescription* instrument = nullptr;
    for (const auto* description : descriptions)
        if (description->isInstrument) {
            instrument = description;
            break;
        }
    require(instrument != nullptr, "the discovered VST3 must identify itself as an instrument");

    juce::String error;
    auto instance = formats.createPluginInstance(*instrument, 48000.0, 512, error);
    require(instance != nullptr, "JUCE host must instantiate the VST3: " + error);
    require(instance->acceptsMidi(), "the instantiated VST3 must accept MIDI");
    require(instance->getTotalNumOutputChannels() >= 1,
        "the instantiated VST3 must expose at least one audio output");

    const auto outputChannels = instance->getTotalNumOutputChannels();
    const auto bufferChannels = juce::jmax(outputChannels, instance->getTotalNumInputChannels());
    juce::AudioBuffer<float> audio(bufferChannels, 512);
    juce::MidiBuffer midi;

    instance->setNonRealtime(true);
    instance->prepareToPlay(48000.0, 512);

    float heldPeak = 0.0f;
    for (int block = 0; block < 48; ++block) {
        audio.clear();
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

        instance->processBlock(audio, midi);
        heldPeak = juce::jmax(heldPeak, outputPeak(audio, outputChannels));
    }

    require(std::isfinite(heldPeak) && heldPeak > 0.0001f,
        "a MIDI note sent through the loaded VST3 must produce non-silent host output");

    float releasePeak = 0.0f;
    for (int block = 0; block < 160; ++block) {
        audio.clear();
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

        instance->processBlock(audio, midi);
        if (block >= 150)
            releasePeak = juce::jmax(releasePeak, outputPeak(audio, outputChannels));
    }

    require(std::isfinite(releasePeak) && releasePeak < 0.0001f,
        "the loaded VST3 output must return to silence after note-off");

    instance->releaseResources();
    std::cout << "PASS: loaded VST3 receives MIDI, produces host audio, and releases to silence\n";
    return 0;
}
