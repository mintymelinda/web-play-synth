#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class WebPlaySynthAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit WebPlaySynthAudioProcessorEditor(WebPlaySynthAudioProcessor& processor);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebPlaySynthAudioProcessorEditor)
};
