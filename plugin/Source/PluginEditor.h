#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

class WebPlaySynthAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit WebPlaySynthAudioProcessorEditor(WebPlaySynthAudioProcessor&);
    ~WebPlaySynthAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using State = WebPlaySynthAudioProcessor::ParameterState;
    using SliderAttachment = State::SliderAttachment;
    using ComboAttachment = State::ComboBoxAttachment;
    using ButtonAttachment = State::ButtonAttachment;

    struct Slot {
        juce::Component* control = nullptr;
        juce::Label* label = nullptr;
    };

    void addGroup(const juce::String& title, std::vector<Slot>& slots);
    void addKnob(const juce::String& id, const juce::String& label, std::vector<Slot>& slots);
    void addChoice(const juce::String& id, const juce::String& label, const juce::StringArray& choices, std::vector<Slot>& slots);
    void addToggle(const juce::String& id, const juce::String& label, std::vector<Slot>& slots);
    void layoutGroup(int groupIndex, juce::Rectangle<int> bounds, int columns);
    void timerCallback() override;

    WebPlaySynthAudioProcessor& synthProcessor;
    State& state;
    std::vector<std::unique_ptr<juce::GroupComponent>> groups;
    std::vector<std::vector<Slot>> groupSlots;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::ToggleButton>> buttons;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::GroupComponent> midiCcGroup;
    std::unique_ptr<juce::ComboBox> midiCcTarget;
    std::unique_ptr<juce::TextButton> midiCcLearnButton, midiCcClearButton;
    std::unique_ptr<juce::Label> midiCcStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebPlaySynthAudioProcessorEditor)
};
