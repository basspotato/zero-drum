#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

/// The pattern as a grid: one row per default-kit lane and per pad with a sample, one cell per step. Click an empty
/// cell to add a hit, click a hit to remove it, drag a hit up or down for velocity,
/// right-click a hit to cycle its roll (1, 2, 3, 4 hits per step), Alt-drag for probability,
/// Ctrl-drag (Cmd on a Mac) sideways for timing. Click a lane's name for its length,
/// Euclidean rhythms and humanize. The playhead follows the host, and every edit goes
/// straight to the audio thread and into the saved state (with undo).
class StepGrid final : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid(ZeroDrumProcessor& owner);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr int kHeaderWidth = 72;
    /// Rebuilds the rows after a pad's sample was loaded or cleared.
    void refreshLanes();

private:
    void timerCallback() override;
    void showLaneMenu(juce::Point<float> p);
    enum class DragMode { Velocity, Probability, Timing };
    int columns() const noexcept;
    int rows() const noexcept { return static_cast<int>(rows_.size()); }
    const zd::Lane& laneOfRow(int row) const noexcept { return pattern_.lanes[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])]; }
    juce::Rectangle<float> cellBounds(int lane, int step) const;
    bool cellAt(juce::Point<float> p, int& lane, int& step) const;

    ZeroDrumProcessor& owner_;
    zd::Pattern pattern_;
    std::vector<int> rows_; ///< the part shown on each row
    int playStep_ = -1;
    // Gesture state.
    int downLane_ = -1, downStep_ = -1;
    bool downWasOn_ = false, moved_ = false;
    float downVelocity_ = 0.0f, downProbability_ = 1.0f, downOffset_ = 0.0f;
    DragMode dragMode_ = DragMode::Velocity;
};
