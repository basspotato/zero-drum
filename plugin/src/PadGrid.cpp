#include "PadGrid.h"

#include "Theme.h"

PadGrid::PadGrid(ZeroDrumProcessor& owner) : owner_(owner)
{
    for (int i = 0; i < kPads; ++i)
        lastHits_[static_cast<std::size_t>(i)] = owner_.partHits(i);
    startTimerHz(30);
}

PadGrid::~PadGrid()
{
    if (repeating_)
        owner_.setNoteRepeat(-1, 1.0f);
}

juce::Rectangle<float> PadGrid::padBounds(int part) const
{
    const auto area = getLocalBounds().toFloat().reduced(6.0f);
    const float w = area.getWidth() / 4.0f, h = area.getHeight() / 4.0f;
    const int column = part % 4, rowFromTop = 3 - part / 4;
    return { area.getX() + column * w, area.getY() + rowFromTop * h, w, h };
}

int PadGrid::padAt(juce::Point<float> p) const
{
    for (int i = 0; i < kPads; ++i)
        if (padBounds(i).contains(p))
            return i;
    return -1;
}

float PadGrid::velocityAt(int part, juce::Point<float> p) const
{
    const auto r = padBounds(part).reduced(4.0f);
    // Top of the pad is full velocity, the bottom edge soft.
    return juce::jlimit(0.15f, 1.0f, 1.0f - 0.85f * (p.y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
}

void PadGrid::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    for (int i = 0; i < kPads; ++i)
    {
        const auto r = padBounds(i).reduced(4.0f);
        const auto colour = zdui::laneColour(i);
        const float glow = glow_[static_cast<std::size_t>(i)];
        g.setColour(zdui::kCellOff.interpolatedWith(colour, 0.18f + 0.82f * glow));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(i == selected_ ? zdui::kText : colour.withAlpha(0.55f));
        g.drawRoundedRectangle(r, 8.0f, i == selected_ ? 2.0f : 1.0f);

        const juce::String name = i < ZeroDrumProcessor::kNumStripParts ? juce::String(ZeroDrumProcessor::kStripNames[i]) : "Part " + juce::String(i + 1);
        g.setColour(zdui::kText);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(name, r.reduced(10.0f, 8.0f).toNearestInt(), juce::Justification::topLeft);
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String(i + 1), r.reduced(10.0f, 8.0f).toNearestInt(), juce::Justification::bottomRight);
    }
}

void PadGrid::timerCallback()
{
    bool changed = false;
    for (int i = 0; i < kPads; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const auto hits = owner_.partHits(i);
        if (hits != lastHits_[idx])
        {
            lastHits_[idx] = hits;
            glow_[idx] = 1.0f;
            changed = true;
        }
        else if (glow_[idx] > 0.0f)
        {
            glow_[idx] = juce::jmax(0.0f, glow_[idx] - 0.12f);
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void PadGrid::mouseDown(const juce::MouseEvent& e)
{
    pressed_ = padAt(e.position);
    if (pressed_ < 0)
        return;
    selected_ = pressed_;
    if (onSelect)
        onSelect(selected_);
    const float velocity = velocityAt(pressed_, e.position);
    if (e.mods.isShiftDown())
    {
        repeating_ = true;
        owner_.setNoteRepeat(pressed_, velocity); // repeat plays the first hit itself
    }
    else
        owner_.auditionPart(pressed_, velocity);
    repaint();
}

void PadGrid::mouseDrag(const juce::MouseEvent& e)
{
    // Dragging a pad out of the grid drops its lane into the DAW as MIDI.
    if (pressed_ < 0 || dragging_ || getLocalBounds().contains(e.getPosition()) || pressed_ >= ZeroDrumProcessor::kNumStripParts)
        return;
    if (repeating_)
    {
        owner_.setNoteRepeat(-1, 1.0f);
        repeating_ = false;
    }
    const auto file = owner_.writeMidiClip(pressed_);
    if (!file.existsAsFile())
        return;
    dragging_ = true;
    juce::Component::SafePointer<PadGrid> self(this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this, [self] {
        if (self != nullptr)
            self->dragging_ = false;
    });
}

void PadGrid::mouseUp(const juce::MouseEvent&)
{
    if (repeating_)
        owner_.setNoteRepeat(-1, 1.0f);
    repeating_ = false;
    pressed_ = -1;
}
