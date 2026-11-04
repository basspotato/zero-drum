#include "MixPage.h"

#include "LookAndFeel.h"
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

    auto& midi = addGroup("MIDI out", juce::Colour(0xffb36bff));
    addToggle(midi, "midiOut", "On");
    addKnob(midi, "midiNoteLength", "Note length");
    addKnob(midi, "midiDrumChannel", "Drum channel");

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
    zdui::styleKnob(k->slider, k->caption, label, g.colour);
    k->attachment = zdui::attachKnob(owner_.state(), id, k->slider);
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
    // Three rows: reverb, glue (with its meter), then sidechain, master and MIDI side by side.
    // Each group is as wide as its knobs, on the 8 px grid.
    auto area = getLocalBounds().reduced(8, 12);
    const std::vector<std::vector<int>> rows { { 0 }, { 1 }, { 2, 3, 4 } };
    const int rowH = kGroupHeader + kKnobSize + 18 + 12;
    for (const auto& rowGroups : rows)
    {
        auto row = area.removeFromTop(rowH);
        area.removeFromTop(8);
        for (int gi : rowGroups)
        {
            if (gi >= static_cast<int>(groups_.size()))
                continue;
            auto& g = groups_[static_cast<std::size_t>(gi)];
            const int cells = static_cast<int>(g->knobs.size()) + (g->title == "Glue" ? 1 : 0);
            const int width = rowGroups.size() == 1 ? row.getWidth() : juce::jmax(cells * kKnobSize + 32, 200);
            g->bounds = row.removeFromLeft(juce::jmin(width, row.getWidth()));
            row.removeFromLeft(8);
            auto inner = g->bounds.reduced(16, 0);
            auto header = inner.removeFromTop(kGroupHeader);
            if (g->toggle)
                g->toggle->button.setBounds(header.removeFromRight(84).reduced(0, 3));
            for (std::size_t i = 0; i < g->knobs.size(); ++i)
            {
                auto r = juce::Rectangle<int>(inner.getX() + static_cast<int>(i) * kKnobSize, inner.getY(), kKnobSize, kKnobSize + 18);
                g->knobs[i]->caption.setBounds(r.removeFromTop(14));
                g->knobs[i]->slider.setBounds(r.reduced(6, 0));
            }
        }
    }
}

juce::Rectangle<int> MixPage::meterBounds() const
{
    for (const auto& g : groups_)
        if (g->title == "Glue" && !g->knobs.empty())
        {
            const auto last = g->knobs.back()->slider.getBounds();
            return { last.getRight() + 26, last.getY() + 4, 24, last.getHeight() - 26 };
        }
    return {};
}

void MixPage::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    for (const auto& grp : groups_)
    {
        g.setColour(zdui::kPanel);
        g.fillRoundedRectangle(grp->bounds.toFloat(), 8.0f);
        auto head = grp->bounds.reduced(16, 0).removeFromTop(kGroupHeader);
        g.setColour(grp->colour);
        g.fillEllipse(head.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f));
        head.removeFromLeft(8);
        g.setColour(zdui::kText);
        g.setFont(juce::FontOptions(zdui::kLabel + 1.0f, juce::Font::bold));
        g.drawText(grp->title.toUpperCase(), head, juce::Justification::centredLeft);
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
