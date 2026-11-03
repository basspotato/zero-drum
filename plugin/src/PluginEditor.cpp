#include "PluginEditor.h"

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), owner_(owner), generic_(owner)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(owner.state().getParameter("genStyle")))
        style_.addItemList(param->choices, 1);
    styleAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.state(), "genStyle", style_);

    generate_.onClick = [this] { owner_.generateBeat(); };
    mutate_.onClick = [this] { owner_.mutateBeat(); };
    fill_.onClick = [this] { owner_.addFillToBeat(); };

    summary_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    summary_.setFont(juce::FontOptions(13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &style_, &generate_, &mutate_, &fill_, &summary_, &generic_ })
        addAndMakeVisible(*c);

    setResizable(true, true);
    setSize(640, 720);
    timerCallback();
    startTimerHz(4);
}

void ZeroDrumEditor::timerCallback()
{
    summary_.setText(owner_.describeBeat(), juce::dontSendNotification);
}

void ZeroDrumEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15161a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("ZERO DRUM", getLocalBounds().removeFromTop(40).reduced(12, 0), juce::Justification::centredLeft);
}

void ZeroDrumEditor::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(40).reduced(8, 6);
    top.removeFromLeft(120); // title
    style_.setBounds(top.removeFromLeft(150));
    top.removeFromLeft(8);
    generate_.setBounds(top.removeFromLeft(90));
    top.removeFromLeft(4);
    mutate_.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(4);
    fill_.setBounds(top.removeFromLeft(50));
    summary_.setBounds(area.removeFromTop(24).reduced(12, 0));
    generic_.setBounds(area);
}
