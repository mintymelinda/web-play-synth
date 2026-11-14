#include "PluginEditor.h"

namespace {
const auto panel = juce::Colour(0xff171c25);
const auto border = juce::Colour(0xff303b4b);
const auto cyan = juce::Colour(0xff63d9e8);
const auto violet = juce::Colour(0xffb49aff);
const auto text = juce::Colour(0xffe7edf5);
const auto muted = juce::Colour(0xff8794a6);
}

WebPlaySynthAudioProcessorEditor::WebPlaySynthAudioProcessorEditor(WebPlaySynthAudioProcessor& p)
    : AudioProcessorEditor(p), state(p.parameters)
{
    setResizable(true, true);
    setResizeLimits(760, 700, 1440, 1200);

    groupSlots.resize(8);
    addGroup("VCO 01  /  PRIMARY OSCILLATOR", groupSlots[0]);
    addChoice("osc1Wave", "WAVEFORM", { "Sine", "Triangle", "Saw", "Square" }, groupSlots[0]);
    addKnob("osc1Octave", "OCTAVE", groupSlots[0]);
    addKnob("osc1Tune", "TUNE", groupSlots[0]);
    addKnob("osc1Level", "LEVEL", groupSlots[0]);

    addGroup("VCO 02  /  MODULATOR OSCILLATOR", groupSlots[1]);
    addChoice("osc2Wave", "WAVEFORM", { "Sine", "Triangle", "Saw", "Square" }, groupSlots[1]);
    addKnob("osc2Octave", "OCTAVE", groupSlots[1]);
    addKnob("osc2Tune", "TUNE", groupSlots[1]);
    addKnob("osc2Level", "LEVEL", groupSlots[1]);
    addKnob("crossMod", "FM AMOUNT", groupSlots[1]);
    addToggle("hardSync", "HARD SYNC", groupSlots[1]);

    addGroup("VCF  /  RESONANT LOW-PASS FILTER", groupSlots[2]);
    addKnob("cutoff", "CUTOFF", groupSlots[2]);
    addKnob("resonance", "RESONANCE", groupSlots[2]);
    addKnob("filterContour", "CONTOUR", groupSlots[2]);

    addGroup("VCA  /  AMPLIFIER ENVELOPE", groupSlots[3]);
    addKnob("ampAttack", "ATTACK", groupSlots[3]);
    addKnob("ampDecay", "DECAY", groupSlots[3]);
    addKnob("ampSustain", "SUSTAIN", groupSlots[3]);
    addKnob("ampRelease", "RELEASE", groupSlots[3]);

    addGroup("VCF  /  FILTER ENVELOPE", groupSlots[4]);
    addKnob("filterAttack", "ATTACK", groupSlots[4]);
    addKnob("filterDecay", "DECAY", groupSlots[4]);
    addKnob("filterSustain", "SUSTAIN", groupSlots[4]);
    addKnob("filterRelease", "RELEASE", groupSlots[4]);

    addGroup("LFO  /  MODULATION SOURCE", groupSlots[5]);
    addChoice("lfoWave", "WAVEFORM", { "Sine", "Triangle", "Saw", "Square" }, groupSlots[5]);
    addKnob("lfoRate", "RATE", groupSlots[5]);
    addKnob("lfoPitchDepth", "PITCH DEPTH", groupSlots[5]);
    addKnob("lfoFilterDepth", "FILTER DEPTH", groupSlots[5]);

    addGroup("OUTPUT  /  PERFORMANCE", groupSlots[6]);
    addKnob("master", "MASTER", groupSlots[6]);
    addToggle("glideEnabled", "GLOBAL GLIDE", groupSlots[6]);
    addKnob("glideTime", "GLIDE TIME", groupSlots[6]);

    addGroup("PATCH ROUTING  /  SIGNAL SWITCHES", groupSlots[7]);
    addToggle("vco1Enabled", "VCO 1 AUDIO", groupSlots[7]);
    addToggle("vco2Enabled", "VCO 2 AUDIO", groupSlots[7]);
    addToggle("filterEnvEnabled", "FILTER ENV → VCF", groupSlots[7]);
    addToggle("ampEnvEnabled", "AMP ENV → VCA", groupSlots[7]);

    // Build all child components before sizing: setSize() invokes resized().
    setSize(980, 880);
}

void WebPlaySynthAudioProcessorEditor::addGroup(const juce::String& title, std::vector<Slot>&)
{
    auto group = std::make_unique<juce::GroupComponent>();
    group->setText(title);
    group->setColour(juce::GroupComponent::outlineColourId, border);
    group->setColour(juce::GroupComponent::textColourId, cyan);
    addAndMakeVisible(*group);
    groups.push_back(std::move(group));
}

void WebPlaySynthAudioProcessorEditor::addKnob(const juce::String& id, const juce::String& name, std::vector<Slot>& slots)
{
    auto label = std::make_unique<juce::Label>();
    label->setText(name, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, muted);
    label->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    addAndMakeVisible(*label);

    auto slider = std::make_unique<juce::Slider>();
    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 17);
    slider->setColour(juce::Slider::rotarySliderFillColourId, cyan);
    slider->setColour(juce::Slider::rotarySliderOutlineColourId, border);
    slider->setColour(juce::Slider::thumbColourId, text);
    slider->setColour(juce::Slider::textBoxTextColourId, text);
    slider->setColour(juce::Slider::textBoxBackgroundColourId, panel);
    slider->setColour(juce::Slider::textBoxOutlineColourId, border);
    addAndMakeVisible(*slider);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(state, id, *slider));
    slots.push_back({ slider.get(), label.get() });
    sliders.push_back(std::move(slider));
    labels.push_back(std::move(label));
}

void WebPlaySynthAudioProcessorEditor::addChoice(const juce::String& id, const juce::String& name, const juce::StringArray& choices, std::vector<Slot>& slots)
{
    auto label = std::make_unique<juce::Label>();
    label->setText(name, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, muted);
    label->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    addAndMakeVisible(*label);

    auto combo = std::make_unique<juce::ComboBox>();
    for (int i = 0; i < choices.size(); ++i)
        combo->addItem(choices[i], i + 1);
    combo->setColour(juce::ComboBox::backgroundColourId, panel);
    combo->setColour(juce::ComboBox::textColourId, text);
    combo->setColour(juce::ComboBox::outlineColourId, border);
    combo->setColour(juce::ComboBox::arrowColourId, cyan);
    addAndMakeVisible(*combo);
    comboAttachments.push_back(std::make_unique<ComboAttachment>(state, id, *combo));
    slots.push_back({ combo.get(), label.get() });
    combos.push_back(std::move(combo));
    labels.push_back(std::move(label));
}

void WebPlaySynthAudioProcessorEditor::addToggle(const juce::String& id, const juce::String& name, std::vector<Slot>& slots)
{
    auto button = std::make_unique<juce::ToggleButton>(name);
    button->setColour(juce::ToggleButton::textColourId, text);
    button->setColour(juce::ToggleButton::tickColourId, cyan);
    button->setColour(juce::ToggleButton::tickDisabledColourId, muted);
    addAndMakeVisible(*button);
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(state, id, *button));
    slots.push_back({ button.get(), nullptr });
    buttons.push_back(std::move(button));
}

void WebPlaySynthAudioProcessorEditor::layoutGroup(int index, juce::Rectangle<int> bounds, int columns)
{
    auto& group = *groups[static_cast<size_t>(index)];
    group.setBounds(bounds);
    auto inner = bounds.reduced(12, 28);
    const auto rows = static_cast<int>((groupSlots[static_cast<size_t>(index)].size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
    const auto cellWidth = inner.getWidth() / columns;
    const auto cellHeight = inner.getHeight() / juce::jmax(1, rows);

    for (size_t i = 0; i < groupSlots[static_cast<size_t>(index)].size(); ++i) {
        auto& slot = groupSlots[static_cast<size_t>(index)][i];
        const auto col = static_cast<int>(i % static_cast<size_t>(columns));
        const auto row = static_cast<int>(i / static_cast<size_t>(columns));
        auto cell = juce::Rectangle<int>(inner.getX() + col * cellWidth, inner.getY() + row * cellHeight, cellWidth, cellHeight).reduced(3, 1);
        if (slot.label != nullptr) {
            slot.label->setBounds(cell.removeFromTop(17));
            slot.control->setBounds(cell.reduced(1, 0));
        } else {
            slot.control->setBounds(cell.reduced(4, 10));
        }
    }
}

void WebPlaySynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10151d));
    g.setColour(text);
    g.setFont(juce::Font(juce::FontOptions(23.0f, juce::Font::bold)));
    g.drawText("WEB PLAY  SYNTH", 18, 8, 300, 30, juce::Justification::left);
    g.setColour(violet);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("DUAL VCO  /  NATIVE INSTRUMENT", 20, 37, 360, 16, juce::Justification::left);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("HOST AUTOMATABLE  •  16 VOICE POLYPHONY", getWidth() - 310, 18, 290, 20, juce::Justification::right);
}

void WebPlaySynthAudioProcessorEditor::resized()
{
    // Host/editor frameworks may request layout during construction or teardown.
    if (groups.size() != 8 || groupSlots.size() != 8)
        return;

    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(58);
    const auto gap = 10;
    const auto halfWidth = (area.getWidth() - gap) / 2;
    const auto rowHeight = (area.getHeight() - gap * 3) / 4;

    layoutGroup(0, { area.getX(), area.getY(), halfWidth, rowHeight }, 4);
    layoutGroup(1, { area.getX() + halfWidth + gap, area.getY(), halfWidth, rowHeight }, 3);
    auto row2 = area.getY() + rowHeight + gap;
    layoutGroup(2, { area.getX(), row2, halfWidth, rowHeight }, 3);
    layoutGroup(3, { area.getX() + halfWidth + gap, row2, halfWidth, rowHeight }, 4);
    auto row3 = row2 + rowHeight + gap;
    layoutGroup(4, { area.getX(), row3, halfWidth, rowHeight }, 4);
    layoutGroup(5, { area.getX() + halfWidth + gap, row3, halfWidth, rowHeight }, 4);
    auto row4 = row3 + rowHeight + gap;
    layoutGroup(6, { area.getX(), row4, halfWidth, rowHeight }, 3);
    layoutGroup(7, { area.getX() + halfWidth + gap, row4, halfWidth, rowHeight }, 2);
}
