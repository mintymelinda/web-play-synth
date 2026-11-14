#include "PluginEditor.h"
WebPlaySynthAudioProcessorEditor::WebPlaySynthAudioProcessorEditor(WebPlaySynthAudioProcessor& processor)
    : AudioProcessorEditor(&processor) { setSize(620,420); }
void WebPlaySynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10151d));
    g.setColour(juce::Colour(0xff62d8d0)); g.setFont(juce::FontOptions(22.0f,juce::Font::bold));
    g.drawText("WEB PLAY SYNTH",24,18,getWidth()-48,32,juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffaab7c6)); g.setFont(juce::FontOptions(12.0f));
    g.drawText("NATIVE INSTRUMENT  ·  VST3",26,50,getWidth()-52,20,juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff263342)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(12),8,1);
    g.setColour(juce::Colour(0xffaab7c6));
    g.drawFittedText("The native DSP engine and host automation are online. Use your DAW's parameter panel to edit controls. A dedicated instrument interface matching the browser synth is the next milestone.",
        32,100,getWidth()-64,100,juce::Justification::centredLeft,4);
    g.setColour(juce::Colour(0xff62d8d0));
    g.drawText("16-VOICE POLYPHONY   ·   MIDI INPUT   ·   PRESET STATE",32,getHeight()-54,getWidth()-64,24,juce::Justification::centredLeft);
}
void WebPlaySynthAudioProcessorEditor::resized() {}
