#include "MixPage.h"

#include "Theme.h"

#include <cmath>

namespace {
constexpr int kKnobSize = 78;
constexpr int kGroupHeader = 30;
constexpr float kMeterRangeDb = 12.0f;
} // namespace

MixPage::MixPage(ZeroDrumProcessor& owner) : owner_(owner)
{
    auto& reverb = addGroup("Reverb", juce::Colour(0xff3a8bff));
    addKnob(reverb, "revDecay", "Decay");
    addKnob(reverb, "revSize", "Size");
    addKnob(reverb, "revPreDelay", "Pre-delay");
    addKnob(reverb, "revDamping", "Damping");
    addKnob(reverb, "revHighPass", "High-pass");
    addKnob(reverb, "revLevel", "Level");

    auto& glue = addGroup("Glue", juce::Colour(0xff2fd1c1));
    addToggle(glue, "glueOn", "On");
    addKnob(glue, "glueThreshold", "Threshold");
    addKnob(glue, "glueRatio", "Ratio");
    addKnob(glue, "glueAttack", "Attack");
    addKnob(glue, "glueRelease", "Release");
    addKnob(glue, "glueMakeup", "Makeup");
    addKnob(glue, "glueMix", "Mix");

    auto& side = addGroup("Sidechain", juce::Colour(0xffff3d5a));
    addKnob(side, "subDuck", "808 duck");
    addKnob(side, "percDuck", "Perc duck");
    addKnob(side, "duckRelease", "Release");

    auto& master = addGroup("Master", juce::Colour(0xffff7a2f));
    addToggle(master, "clipOn", "Clipper");
    addKnob(master, "clipDrive", "Drive");
    addKnob(master, "clipCeiling", "Ceiling");
    addKnob(master, "masterLevel", "Level");

    startTimerHz(30);
}

MixPage::Group& MixPage::addGroup(const juce::String& title, juce::Colour colour)
{
    auto g = std::make_unique<Group>();
    g->title = title;
    g->colour = colour;
    groups_.push_back(std::move(g));
    return *groups_.back();
}

void MixPage::addKnob(Group& g, const char* id, const char* label)
{
    auto k = std::make_unique<Knob>();
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour(juce::Slider::rotarySliderFillColourId, g.colour);
    k->slider.setColour(juce::Slider::thumbColourId, g.colour.brighter(0.4f));
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner_.state(), id, k->slider);
    k->slider.textFromValueFunction = [](double v) {
        return std::abs(v) >= 100.0 ? juce::String(juce::roundToInt(v)) : juce::String(v, 2);
    };
    k->slider.updateText();
    k->caption.setText(label, juce::dontSendNotification);
    k->caption.setJustificationType(juce::Justification::centred);
    k->caption.setColour(juce::Label::textColourId, zdui::kDim);
    k->caption.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(k->slider);
    addAndMakeVisible(k->caption);
    g.knobs.push_back(std::move(k));
}

void MixPage::addToggle(Group& g, const char* id, const char* label)
{
    g.toggle = std::make_unique<Toggle>();
    g.toggle->button.setButtonText(label);
    g.toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), id, g.toggle->button);
    addAndMakeVisible(g.toggle->button);
}

void MixPage::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int perRow = juce::jmax(1, (area.getWidth() - 16) / kKnobSize);
    for (auto& g : groups_)
    {
        const int extra = g->title == "Glue" ? 1 : 0; // room for the meter
        const int cells = static_cast<int>(g->knobs.size()) + extra;
        const int rows = (cells + perRow - 1) / perRow;
        g->bounds = area.removeFromTop(kGroupHeader + rows * (kKnobSize + 18) + 8);
        area.removeFromTop(8);
        auto inner = g->bounds.reduced(8, 0);
        auto header = inner.removeFromTop(kGroupHeader);
        if (g->toggle)
            g->toggle->button.setBounds(header.removeFromRight(100).reduced(0, 4));
        for (std::size_t i = 0; i < g->knobs.size(); ++i)
        {
            const int col = static_cast<int>(i) % perRow, row = static_cast<int>(i) / perRow;
            auto r = juce::Rectangle<int>(inner.getX() + col * kKnobSize, inner.getY() + row * (kKnobSize + 18), kKnobSize, kKnobSize + 18);
            g->knobs[i]->caption.setBounds(r.removeFromTop(16));
            g->knobs[i]->slider.setBounds(r.reduced(6, 0));
        }
    }
}

juce::Rectangle<int> MixPage::meterBounds() const
{
    for (const auto& g : groups_)
        if (g->title == "Glue")
        {
            const auto inner = g->bounds.reduced(8, 0).withTrimmedTop(kGroupHeader);
            const int perRow = juce::jmax(1, (getLocalBounds().reduced(10).getWidth() - 16) / kKnobSize);
            const int i = static_cast<int>(g->knobs.size());
            const int col = i % perRow, row = i / perRow;
            return { inner.getX() + col * kKnobSize + 22, inner.getY() + row * (kKnobSize + 18) + 16, 34, kKnobSize - 6 };
        }
    return {};
}

void MixPage::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    for (const auto& grp : groups_)
    {
        g.setColour(zdui::kPanel);
        g.fillRoundedRectangle(grp->bounds.toFloat(), 6.0f);
        g.setColour(grp->colour);
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawText(grp->title, grp->bounds.reduced(12, 0).removeFromTop(kGroupHeader), juce::Justification::centredLeft);
    }
    // Glue gain reduction: fills from the top, 0 to 12 dB.
    const auto m = meterBounds().toFloat();
    if (!m.isEmpty())
    {
        g.setColour(zdui::kCellOff);
        g.fillRoundedRectangle(m, 3.0f);
        const float frac = juce::jlimit(0.0f, 1.0f, meterDb_ / kMeterRangeDb);
        g.setColour(juce::Colour(0xff2fd1c1));
        g.fillRoundedRectangle(m.withHeight(m.getHeight() * frac), 3.0f);
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("GR " + juce::String(meterDb_, 1), m.withY(m.getBottom() + 2).withHeight(14).expanded(14, 0).toNearestInt(),
                   juce::Justification::centred);
    }
}

void MixPage::timerCallback()
{
    // Fast attack, slow fall, so the meter is readable.
    const float now = owner_.glueReductionDb();
    const float next = now > meterDb_ ? now : meterDb_ + (now - meterDb_) * 0.15f;
    if (std::abs(next - meterDb_) > 0.01f)
    {
        meterDb_ = next;
        repaint(meterBounds().expanded(16, 18));
    }
}
