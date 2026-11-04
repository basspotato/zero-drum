#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

/// The bus at a glance: reverb, glue compressor (with its gain-reduction meter), sidechain and
/// the master stage, as labelled knobs in four groups.
class MixPage final : public juce::Component, private juce::Timer
{
public:
    explicit MixPage(ZeroDrumProcessor& owner);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    struct Group
    {
        juce::String title;
        juce::Colour colour;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::unique_ptr<Toggle> toggle;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    Group& addGroup(const juce::String& title, juce::Colour colour);
    void addKnob(Group& g, const char* id, const char* label);
    void addToggle(Group& g, const char* id, const char* label);
    juce::Rectangle<int> meterBounds() const;

    ZeroDrumProcessor& owner_;
    std::vector<std::unique_ptr<Group>> groups_;
    float meterDb_ = 0.0f;
};
