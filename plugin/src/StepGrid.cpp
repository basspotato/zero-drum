#include "StepGrid.h"

#include "Theme.h"

#include <cmath>

StepGrid::StepGrid(ZeroDrumProcessor& owner) : owner_(owner), pattern_(owner.currentPattern())
{
    refreshLanes();
    startTimerHz(30);
}

void StepGrid::refreshLanes()
{
    rows_.clear();
    int length = 16;
    for (int p = 0; p < ZeroDrumProcessor::kNumStripParts; ++p)
    {
        rows_.push_back(p);
        length = std::max(length, pattern_.lanes[static_cast<std::size_t>(p)].length);
    }
    for (int p = ZeroDrumProcessor::kFirstSamplePad; p < zd::kMaxParts; ++p)
        if (owner_.padHasSample(p))
        {
            rows_.push_back(p);
            // A pad that never had a lane gets an empty one the length of the beat.
            if (pattern_.lanes[static_cast<std::size_t>(p)].length <= 0)
            {
                const int len = std::min(length, zd::kMaxStepsPerLane);
                pattern_.lanes[static_cast<std::size_t>(p)].length = len;
                owner_.editPattern([p, len](zd::Pattern& pat) {
                    auto& lane = pat.lanes[static_cast<std::size_t>(p)];
                    if (lane.length <= 0)
                        lane.length = len;
                });
            }
        }
    repaint();
}

int StepGrid::columns() const noexcept
{
    int n = 16;
    for (int r = 0; r < rows(); ++r)
        n = std::max(n, laneOfRow(r).length);
    return std::min(n, zd::kMaxStepsPerLane);
}

juce::Rectangle<float> StepGrid::cellBounds(int lane, int step) const
{
    const auto area = getLocalBounds().toFloat().reduced(6.0f).withTrimmedLeft(static_cast<float>(kHeaderWidth));
    const float w = area.getWidth() / static_cast<float>(columns());
    const float h = area.getHeight() / static_cast<float>(std::max(1, rows()));
    return { area.getX() + step * w, area.getY() + lane * h, w, h };
}

bool StepGrid::cellAt(juce::Point<float> p, int& lane, int& step) const
{
    const auto area = getLocalBounds().toFloat().reduced(6.0f).withTrimmedLeft(static_cast<float>(kHeaderWidth));
    if (!area.contains(p))
        return false;
    const int row = juce::jlimit(0, rows() - 1, static_cast<int>((p.y - area.getY()) / (area.getHeight() / static_cast<float>(rows()))));
    lane = rows_[static_cast<std::size_t>(row)];
    step = juce::jlimit(0, columns() - 1, static_cast<int>((p.x - area.getX()) / (area.getWidth() / columns())));
    return step < pattern_.lanes[static_cast<std::size_t>(lane)].length;
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    const int cols = columns();
    for (int l = 0; l < rows(); ++l)
    {
        const int part = rows_[static_cast<std::size_t>(l)];
        const auto& lane = laneOfRow(l);
        const auto colour = zdui::laneColour(part);
        const auto row = cellBounds(l, 0);
        g.setColour(colour);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawFittedText(owner_.padName(part), juce::Rectangle<float>(6.0f, row.getY(), kHeaderWidth - 8.0f, row.getHeight()).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);

        for (int s = 0; s < cols; ++s)
        {
            const auto r = cellBounds(l, s).reduced(s % 4 == 3 ? 1.0f : 1.5f, 3.0f).withTrimmedRight(s % 4 == 3 ? 2.0f : 0.0f);
            if (s >= lane.length)
            {
                g.setColour(zdui::kBackground.brighter(0.03f));
                g.fillRoundedRectangle(r, 3.0f);
                continue;
            }
            const auto& step = lane.steps[static_cast<std::size_t>(s)];
            const bool beat = (s / 4) % 2 == 0;
            g.setColour(beat ? zdui::kCellOff : zdui::kCellOff.darker(0.25f));
            g.fillRoundedRectangle(r, 3.0f);
            if (step.on)
            {
                // Velocity fills the cell from the bottom; rolls show as ticks.
                const float v = juce::jlimit(0.05f, 1.0f, step.velocity);
                g.setColour(colour.withAlpha(0.35f + 0.65f * v * step.probability));
                g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight() * (1.0f - v)), 3.0f);
                g.setColour(colour);
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
                for (int k = 1; k < step.ratchet; ++k)
                {
                    const float x = r.getX() + r.getWidth() * k / step.ratchet;
                    g.setColour(zdui::kBackground.withAlpha(0.8f));
                    g.drawLine(x, r.getY() + 2.0f, x, r.getBottom() - 2.0f, 1.5f);
                }
            }
            if (playStep_ >= 0 && lane.length > 0 && s == playStep_ % lane.length)
            {
                g.setColour(zdui::kText.withAlpha(0.18f));
                g.fillRoundedRectangle(r, 3.0f);
            }
        }
    }
    // Bar lines every 16 steps.
    g.setColour(zdui::kDim.withAlpha(0.5f));
    for (int s = 16; s < cols; s += 16)
    {
        const auto r = cellBounds(0, s);
        g.drawLine(r.getX(), r.getY(), r.getX(), cellBounds(rows() - 1, s).getBottom(), 1.0f);
    }
}

void StepGrid::timerCallback()
{
    bool changed = false;
    if (downLane_ < 0)
    {
        // Pick up changes from Generate, Mutate, Fill, Build and loaded state.
        const auto latest = owner_.currentPattern();
        for (int l = 0; l < zd::kMaxParts && !changed; ++l)
        {
            const auto& a = latest.lanes[static_cast<std::size_t>(l)];
            const auto& b = pattern_.lanes[static_cast<std::size_t>(l)];
            if (a.length != b.length)
                changed = true;
            for (int s = 0; s < a.length && !changed; ++s)
            {
                const auto& x = a.steps[static_cast<std::size_t>(s)];
                const auto& y = b.steps[static_cast<std::size_t>(s)];
                changed = x.on != y.on || std::abs(x.velocity - y.velocity) > 1.0e-6f || x.ratchet != y.ratchet
                       || std::abs(x.probability - y.probability) > 1.0e-6f;
            }
        }
        if (changed)
            pattern_ = latest;
    }
    const int step = owner_.isTransportRunning()
                         ? static_cast<int>(std::floor(owner_.playheadBeats() / std::max(1.0e-3, pattern_.stepBeats)))
                         : -1;
    if (step != playStep_)
    {
        playStep_ = step;
        changed = true;
    }
    if (changed)
        repaint();
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    int lane = 0, step = 0;
    if (!cellAt(e.position, lane, step))
        return;
    auto& s = pattern_.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    if (e.mods.isPopupMenu())
    {
        if (!s.on)
            return;
        const int ratchet = s.ratchet % zd::kMaxRatchet + 1;
        s.ratchet = ratchet;
        owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].ratchet = ratchet; });
        repaint();
        return;
    }
    downLane_ = lane;
    downStep_ = step;
    downWasOn_ = s.on;
    moved_ = false;
    if (!s.on)
    {
        s = zd::Step { true, 0.9f, 1.0f, 0.0f, -1, 1 };
        const zd::Step added = s;
        owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = added; });
        owner_.auditionPart(lane, added.velocity);
    }
    downVelocity_ = s.velocity;
    repaint();
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (downLane_ < 0 || std::abs(e.getDistanceFromDragStartY()) < 3)
        return;
    moved_ = true;
    const float v = juce::jlimit(0.05f, 1.0f, downVelocity_ - e.getDistanceFromDragStartY() / 120.0f);
    const int lane = downLane_, step = downStep_;
    pattern_.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].velocity = v;
    owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].velocity = v; });
    repaint();
}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    if (downLane_ >= 0 && downWasOn_ && !moved_)
    {
        const int lane = downLane_, step = downStep_;
        pattern_.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].on = false;
        owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].on = false; });
        repaint();
    }
    downLane_ = downStep_ = -1;
}
