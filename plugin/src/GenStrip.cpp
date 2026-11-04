#include "GenStrip.h"

#include "LookAndFeel.h"
#include "Theme.h"

namespace {

struct DialSpec
{
    const char* id;
    const char* name;
    const char* tip;
};

constexpr DialSpec kSpecs[] {
    { "genDensity", "Density", "How many hits Generate places" },
    { "genComplexity", "Complexity", "Syncopation, ghost notes and rolls" },
    { "genGroove", "Groove", "How much the style's feel (timing, accents) comes through" },
    { "genRoot", "Root", "The 808's key; the kick follows it" },
    { "swing", "Swing", "Delays every other 16th (50 % is straight)" },
};

} // namespace

GenStrip::GenStrip(ZeroDrumProcessor& owner)
{
    for (int i = 0; i < kDials; ++i)
    {
        auto& d = dials_[static_cast<std::size_t>(i)];
        const auto& spec = kSpecs[i];
        d.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        d.knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        d.knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        d.knob.setColour(juce::Slider::rotarySliderFillColourId, zdui::kAccent);
        d.knob.setTooltip(spec.tip);
        d.knob.setWantsKeyboardFocus(false);
        d.attachment = zdui::attachKnob(owner.state(), spec.id, d.knob);
        d.name.setText(spec.name, juce::dontSendNotification);
        d.name.setFont(juce::FontOptions(zdui::kCaption, juce::Font::bold));
        d.name.setColour(juce::Label::textColourId, zdui::kDim);
        d.value.setFont(juce::FontOptions(zdui::kLabel, juce::Font::bold));
        d.value.setColour(juce::Label::textColourId, zdui::kText);
        for (auto* label : { &d.name, &d.value })
        {
            label->setBorderSize({});
            label->setInterceptsMouseClicks(false, false);
        }
        auto* knob = &d.knob;
        auto* value = &d.value;
        d.knob.onValueChange = [knob, value] { value->setText(knob->getTextFromValue(knob->getValue()), juce::dontSendNotification); };
        d.knob.onValueChange();
        addAndMakeVisible(d.knob);
        addAndMakeVisible(d.name);
        addAndMakeVisible(d.value);
    }
    kit_.setTooltip("Generate also loads the style's tuned kit (locked parts keep their sound)");
    kit_.setColour(juce::ToggleButton::tickColourId, zdui::kAccent);
    kit_.setWantsKeyboardFocus(false);
    kitAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.state(), "genKit", kit_);
    addAndMakeVisible(kit_);
}

void GenStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.0f, 4.0f);
    g.setColour(zdui::kPanel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(zdui::kDim);
    g.setFont(juce::FontOptions(zdui::kCaption, juce::Font::bold));
    g.drawText("GENERATOR", r.withTrimmedLeft(14.0f).withWidth(80.0f), juce::Justification::centredLeft);
}

void GenStrip::resized()
{
    auto r = getLocalBounds().reduced(0, 4);
    r.removeFromLeft(96);
    kit_.setBounds(r.removeFromRight(110).reduced(8, 6));
    const int cell = juce::jmin(132, r.getWidth() / kDials);
    for (auto& d : dials_)
    {
        auto c = r.removeFromLeft(cell);
        d.knob.setBounds(c.removeFromLeft(36).withSizeKeepingCentre(34, 34));
        c.removeFromLeft(6);
        auto text = c.withSizeKeepingCentre(c.getWidth(), 30);
        d.name.setBounds(text.removeFromTop(13));
        d.value.setBounds(text);
    }
}
