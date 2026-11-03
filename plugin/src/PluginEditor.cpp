#include "PluginEditor.h"

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), generic_(owner)
{
    addAndMakeVisible(generic_);
    setResizable(true, true);
    setSize(560, 640);
}

void ZeroDrumEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15161a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("ZERO DRUM", getLocalBounds().removeFromTop(36).reduced(12, 0), juce::Justification::centredLeft);
}

void ZeroDrumEditor::resized()
{
    generic_.setBounds(getLocalBounds().withTrimmedTop(36));
}
