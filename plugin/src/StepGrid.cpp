#include "StepGrid.h"

#include "PartName.h"
#include "Theme.h"

#include <algorithm>
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

juce::Rectangle<float> StepGrid::gridArea() const
{
    auto area = getLocalBounds().toFloat().reduced(6.0f);
    if (pages() > 1)
        area.removeFromTop(24.0f); // page tabs
    return area;
}

juce::Rectangle<float> StepGrid::pageTab(int index) const
{
    const auto top = getLocalBounds().toFloat().reduced(6.0f);
    return { static_cast<float>(kHeaderWidth) + 6.0f + index * 84.0f, top.getY(), 80.0f, 20.0f };
}

juce::Rectangle<float> StepGrid::cellBounds(int lane, int step) const
{
    const auto area = gridArea().withTrimmedLeft(static_cast<float>(kHeaderWidth));
    const float w = area.getWidth() / static_cast<float>(visibleColumns());
    const float h = area.getHeight() / static_cast<float>(std::max(1, rows()));
    return { area.getX() + (step - firstColumn()) * w, area.getY() + lane * h, w, h };
}

bool StepGrid::cellAt(juce::Point<float> p, int& lane, int& step) const
{
    const auto area = gridArea().withTrimmedLeft(static_cast<float>(kHeaderWidth));
    if (!area.contains(p) || rows() == 0)
        return false;
    const int row = juce::jlimit(0, rows() - 1, static_cast<int>((p.y - area.getY()) / (area.getHeight() / static_cast<float>(rows()))));
    lane = rows_[static_cast<std::size_t>(row)];
    step = firstColumn()
           + juce::jlimit(0, visibleColumns() - 1, static_cast<int>((p.x - area.getX()) / (area.getWidth() / visibleColumns())));
    return step < pattern_.lanes[static_cast<std::size_t>(lane)].length;
}

juce::Rectangle<float> StepGrid::muteBox(int row) const
{
    const auto r = cellBounds(row, 0);
    return { static_cast<float>(kHeaderWidth) - 36.0f, r.getCentreY() - 9.0f, 18.0f, 18.0f };
}

juce::Rectangle<float> StepGrid::soloBox(int row) const
{
    return muteBox(row).translated(20.0f, 0.0f);
}

void StepGrid::shiftLane(int lane, int by)
{
    // Rotates the lane's steps inside its length: later (right) for by > 0, earlier for by < 0.
    owner_.pushUndo();
    owner_.editPattern([lane, by](zd::Pattern& pat) {
        auto& l = pat.lanes[static_cast<std::size_t>(lane)];
        const int n = l.length;
        if (n <= 1)
            return;
        const int k = ((by % n) + n) % n;
        std::rotate(l.steps.begin(), l.steps.begin() + (n - k), l.steps.begin() + n);
    });
    pattern_ = owner_.currentPattern();
    repaint();
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    if (pages() > 1)
    {
        // Page tabs, then Follow (the page tracks the playhead while playing).
        g.setFont(juce::FontOptions(12.0f));
        for (int i = 0; i <= pages(); ++i)
        {
            const bool follow = i == pages();
            const bool on = follow ? follow_ : i == page_;
            const auto tab = pageTab(i);
            g.setColour(on ? zdui::kAccent : zdui::kCellOff);
            g.fillRoundedRectangle(tab, 4.0f);
            g.setColour(on ? juce::Colours::black : zdui::kText);
            const juce::String text = follow ? juce::String("Follow")
                                             : juce::String(i * kPageSteps + 1) + "-" + juce::String(juce::jmin(columns(), (i + 1) * kPageSteps));
            g.drawText(text, tab, juce::Justification::centred);
        }
    }
    for (int l = 0; l < rows(); ++l)
    {
        const int part = rows_[static_cast<std::size_t>(l)];
        const auto& lane = laneOfRow(l);
        const auto colour = zdui::laneColour(part);
        const auto row = cellBounds(l, 0);
        g.setColour(colour);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        const bool muted = ((owner_.audibleMask() >> part) & 1u) == 0;
        g.setColour(muted ? colour.withAlpha(0.4f) : colour);
        g.drawFittedText(owner_.padName(part), juce::Rectangle<float>(6.0f, row.getY(), kHeaderWidth - 44.0f, row.getHeight()).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.7f);
        auto box = [&](juce::Rectangle<float> b, const char* text, bool on, juce::Colour onColour) {
            g.setColour(on ? onColour : zdui::kCellOff);
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour(on ? juce::Colours::black : zdui::kDim);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(text, b, juce::Justification::centred);
        };
        box(muteBox(l), "M", owner_.laneMuted(part), juce::Colour(0xffe0b040));
        box(soloBox(l), "S", owner_.laneSoloed(part), juce::Colour(0xff40c0e0));

        for (int s = firstColumn(); s < firstColumn() + visibleColumns(); ++s)
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
                g.setColour(colour.withAlpha((0.35f + 0.65f * v * step.probability) * (muted ? 0.35f : 1.0f)));
                g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight() * (1.0f - v)), 3.0f);
                g.setColour(colour);
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
                if (std::abs(step.microOffset) > 0.005f)
                {
                    // Timing nudge: a tick at the bottom, left = early, right = late.
                    const float x = r.getCentreX() + step.microOffset * r.getWidth();
                    g.setColour(zdui::kText);
                    g.fillRect(juce::Rectangle<float>(x - 1.0f, r.getBottom() - 6.0f, 2.0f, 5.0f));
                }
                if (step.probability < 0.995f && r.getWidth() > 22.0f)
                {
                    g.setColour(zdui::kText.withAlpha(0.85f));
                    g.setFont(juce::FontOptions(9.0f));
                    g.drawText(juce::String(juce::roundToInt(step.probability * 100.0f)) + "%", r.withHeight(12.0f).toNearestInt(),
                               juce::Justification::centred);
                }
                if (step.note >= 0 && r.getWidth() > 18.0f && r.getHeight() > 30.0f)
                {
                    g.setColour(zdui::kText);
                    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                    g.drawText(juce::MidiMessage::getMidiNoteName(step.note, true, true, 3), r.withTrimmedTop(r.getHeight() - 16.0f).toNearestInt(),
                               juce::Justification::centred);
                }
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
    for (int s = firstColumn() + 16; s < firstColumn() + visibleColumns(); s += 16)
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
            if (a.length != b.length || a.straight != b.straight)
                changed = true;
            for (int s = 0; s < a.length && !changed; ++s)
            {
                const auto& x = a.steps[static_cast<std::size_t>(s)];
                const auto& y = b.steps[static_cast<std::size_t>(s)];
                changed = x.on != y.on || std::abs(x.velocity - y.velocity) > 1.0e-6f || x.ratchet != y.ratchet || x.note != y.note
                       || std::abs(x.probability - y.probability) > 1.0e-6f;
            }
        }
        if (changed)
            pattern_ = latest;
    }
    // In a chain, the playhead shows only while the slot being edited is the one playing.
    double local = 0.0;
    const bool here = owner_.isTransportRunning() && owner_.playingSlot(&local) == owner_.editSlot();
    const int step = here ? static_cast<int>(std::floor(local / std::max(1.0e-3, pattern_.stepBeats))) : -1;
    if (step != playStep_)
    {
        playStep_ = step;
        changed = true;
    }
    int page = juce::jlimit(0, juce::jmax(0, pages() - 1), page_);
    if (follow_ && step >= 0 && pages() > 1 && downLane_ < 0)
        page = (step % columns()) / kPageSteps;
    if (page != page_)
    {
        page_ = page;
        changed = true;
    }
    if (changed)
        repaint();
}

void StepGrid::mouseDown(const juce::MouseEvent& e)
{
    if (pages() > 1)
        for (int i = 0; i <= pages(); ++i)
            if (pageTab(i).contains(e.position))
            {
                if (i == pages())
                    follow_ = !follow_;
                else
                {
                    page_ = i;
                    follow_ = false;
                }
                repaint();
                return;
            }
    if (e.position.x < static_cast<float>(kHeaderWidth) + 6.0f && gridArea().contains(e.position))
    {
        for (int r = 0; r < rows(); ++r)
        {
            const int part = rows_[static_cast<std::size_t>(r)];
            if (muteBox(r).contains(e.position))
            {
                owner_.setLaneMute(part, !owner_.laneMuted(part));
                repaint();
                return;
            }
            if (soloBox(r).contains(e.position))
            {
                owner_.setLaneSolo(part, !owner_.laneSoloed(part));
                repaint();
                return;
            }
        }
        showLaneMenu(e.position);
        return;
    }
    int lane = 0, step = 0;
    if (!cellAt(e.position, lane, step))
        return;
    auto& s = pattern_.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    if (e.mods.isPopupMenu())
    {
        if (!s.on)
            return;
        owner_.pushUndo();
        const int ratchet = s.ratchet % zd::kMaxRatchet + 1;
        s.ratchet = ratchet;
        owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].ratchet = ratchet; });
        repaint();
        return;
    }
    owner_.pushUndo();
    downLane_ = lane;
    downStep_ = step;
    downWasOn_ = s.on;
    moved_ = false;
    // Alt drags probability, Ctrl (Cmd on a Mac) drags timing; a plain drag sets velocity.
    dragMode_ = e.mods.isAltDown()       ? DragMode::Probability
                : e.mods.isCommandDown() ? DragMode::Timing
                : e.mods.isShiftDown() && owner_.defaultNote(lane) >= 0 ? DragMode::Pitch
                                                                       : DragMode::Velocity;
    if (!s.on)
    {
        s = zd::Step { true, 0.9f, 1.0f, 0.0f, -1, 1 };
        const zd::Step added = s;
        owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = added; });
        owner_.auditionPart(lane, added.velocity);
    }
    downVelocity_ = s.velocity;
    downProbability_ = s.probability;
    downOffset_ = s.microOffset;
    downNote_ = s.note >= 0 ? s.note : owner_.defaultNote(lane);
    repaint();
}

void StepGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (downLane_ < 0)
        return;
    const bool horizontal = dragMode_ == DragMode::Timing;
    if (std::abs(horizontal ? e.getDistanceFromDragStartX() : e.getDistanceFromDragStartY()) < 3)
        return;
    moved_ = true;
    const int lane = downLane_, step = downStep_;
    auto& st = pattern_.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    switch (dragMode_)
    {
        case DragMode::Velocity:
            st.velocity = juce::jlimit(0.05f, 1.0f, downVelocity_ - static_cast<float>(e.getDistanceFromDragStartY()) / 120.0f);
            break;
        case DragMode::Probability:
            st.probability = juce::jlimit(0.0f, 1.0f, downProbability_ - static_cast<float>(e.getDistanceFromDragStartY()) / 120.0f);
            break;
        case DragMode::Pitch:
            // 8 pixels per semitone, up to two octaves either way.
            st.note = juce::jlimit(0, 127, downNote_ - e.getDistanceFromDragStartY() / 8);
            break;
        case DragMode::Timing:
        {
            const float cell = juce::jmax(4.0f, cellBounds(0, 0).getWidth());
            st.microOffset = juce::jlimit(-0.5f, 0.5f, downOffset_ + static_cast<float>(e.getDistanceFromDragStartX()) / (2.0f * cell));
            break;
        }
    }
    const zd::Step edited = st;
    owner_.editPattern([=](zd::Pattern& p) { p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)] = edited; });
    repaint();
}

void StepGrid::showLaneMenu(juce::Point<float> p)
{
    const auto area = gridArea();
    if (rows() == 0 || !area.contains(p))
        return;
    const int row = juce::jlimit(0, rows() - 1, static_cast<int>((p.y - area.getY()) / (area.getHeight() / static_cast<float>(rows()))));
    const int lane = rows_[static_cast<std::size_t>(row)];
    const int length = pattern_.lanes[static_cast<std::size_t>(lane)].length;

    juce::PopupMenu lengths;
    for (int n : { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 32, 48, 64 })
        lengths.addItem(juce::String(n) + " steps", true, n == length, [this, lane, n] {
            owner_.setLaneLength(lane, n);
            pattern_ = owner_.currentPattern();
            repaint();
        });
    juce::PopupMenu euclid;
    for (int k = 1; k <= juce::jmin(length, 16); ++k)
        euclid.addItem(juce::String(k) + " of " + juce::String(length), [this, lane, k] {
            owner_.euclidLane(lane, k);
            pattern_ = owner_.currentPattern();
            repaint();
        });

    juce::PopupMenu menu;
    menu.addSectionHeader(owner_.padName(lane));
    menu.addSubMenu("Length", lengths);
    menu.addSubMenu("Euclidean", euclid);
    if (lane < ZeroDrumProcessor::kNumStripParts)
        menu.addItem("Generate this lane", [this, lane] {
            owner_.generateLane(lane);
            pattern_ = owner_.currentPattern();
            repaint();
        });
    menu.addItem("Double", length > 0 && 2 * length <= zd::kMaxStepsPerLane, false, [this, lane] {
        owner_.doubleLane(lane);
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.addItem("Reverse", [this, lane] {
        owner_.reverseLane(lane);
        pattern_ = owner_.currentPattern();
        repaint();
    });
    juce::PopupMenu velocity;
    velocity.addItem("Ramp up (soft to loud)", [this, lane] { owner_.rampLane(lane, true); pattern_ = owner_.currentPattern(); repaint(); });
    velocity.addItem("Ramp down (loud to soft)", [this, lane] { owner_.rampLane(lane, false); pattern_ = owner_.currentPattern(); repaint(); });
    menu.addSubMenu("Velocity", velocity);
    menu.addItem("Straight (no swing)", true, pattern_.lanes[static_cast<std::size_t>(lane)].straight, [this, lane] {
        owner_.pushUndo();
        owner_.editPattern([lane](zd::Pattern& pat) {
            auto& l = pat.lanes[static_cast<std::size_t>(lane)];
            l.straight = !l.straight;
        });
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.addItem("Shift right (later)", [this, lane] { shiftLane(lane, 1); });
    menu.addItem("Shift left (earlier)", [this, lane] { shiftLane(lane, -1); });
    menu.addItem("Humanize this lane", [this, lane] {
        owner_.humanizeBeat(0.7f, lane);
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.addItem("Humanize the beat", [this] {
        owner_.humanizeBeat(0.7f);
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.addSeparator();
    menu.addItem("Copy lane", [this, lane] { owner_.copyLane(lane); });
    menu.addItem("Paste lane", owner_.hasLaneCopy(), false, [this, lane] {
        owner_.pasteLane(lane);
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.addSeparator();
    menu.addItem("Rename...", [this, lane] { zdui::askPartName(owner_, lane, [this] { repaint(); }); });
    menu.addItem("Reset sound", [this, lane] { owner_.resetPartSound(lane); });
    menu.addItem("Clear lane", [this, lane] {
        owner_.pushUndo();
        owner_.editPattern([lane](zd::Pattern& pat) {
            auto& l = pat.lanes[static_cast<std::size_t>(lane)];
            for (auto& st : l.steps)
                st = zd::Step {};
        });
        pattern_ = owner_.currentPattern();
        repaint();
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
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
