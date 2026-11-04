#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

/// The generator's dials in one slim bar above the step grid: density, complexity, groove,
/// the 808 root and swing, plus whether Generate also loads the style's kit. Each dial is a
/// small knob with its name and value beside it, so the grid keeps nearly all the height.
class GenStrip final : public juce::Component
{
public:
    explicit GenStrip(ZeroDrumProcessor& owner);

    static constexpr int kHeight = 48;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Dial
    {
        juce::Slider knob;
        juce::Label name, value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    static constexpr int kDials = 5;
    std::array<Dial, kDials> dials_;
    juce::ToggleButton kit_ { "Style kit" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kitAttachment_;
};
