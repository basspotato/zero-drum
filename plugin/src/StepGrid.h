#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

/// The pattern as a grid: one row per default-kit lane and per pad with a sample, one cell per step. Click an empty
/// cell to add a hit, click a hit to remove it, drag a hit up or down for velocity,
/// right-click a hit to cycle its roll (1, 2, 3, 4 hits per step), Alt-drag for probability,
/// Ctrl-drag (Cmd on a Mac) sideways for timing, Shift-drag for pitch (kick, 808, sample pads). Click a lane's name for its length,
/// Euclidean rhythms, shifting and humanize; M and S mute and solo it. The playhead follows the host, and every edit goes
/// straight to the audio thread and into the saved state (with undo).
class StepGrid final : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid(ZeroDrumProcessor& owner);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr int kHeaderWidth = 104;
    /// Rebuilds the rows after a pad's sample was loaded or cleared.
    void refreshLanes();

private:
    void timerCallback() override;
    void showLaneMenu(juce::Point<float> p);
    enum class DragMode { Velocity, Probability, Timing, Pitch };
    int columns() const noexcept;
    int rows() const noexcept { return static_cast<int>(rows_.size()); }
    const zd::Lane& laneOfRow(int row) const noexcept { return pattern_.lanes[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])]; }
    juce::Rectangle<float> cellBounds(int lane, int step) const;
    bool cellAt(juce::Point<float> p, int& lane, int& step) const;
    /// The mute and solo boxes in a row's header.
    juce::Rectangle<float> muteBox(int row) const;
    juce::Rectangle<float> soloBox(int row) const;
    void shiftLane(int lane, int by);
    /// Long lanes page: over kPageSteps columns the grid shows one page at a time, with tabs on top.
    static constexpr int kPageSteps = 32;
    static constexpr int kRulerHeight = 20;
    int pages() const noexcept { return (columns() + kPageSteps - 1) / kPageSteps; }
    int firstColumn() const noexcept { return pages() > 1 ? page_ * kPageSteps : 0; }
    int visibleColumns() const noexcept { return pages() > 1 ? juce::jmin(kPageSteps, columns() - firstColumn()) : columns(); }
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> pageTab(int index) const; ///< index pages() is the Follow tab

    ZeroDrumProcessor& owner_;
    zd::Pattern pattern_;
    std::vector<int> rows_; ///< the part shown on each row
    int playStep_ = -1;
    int page_ = 0;
    bool follow_ = true; ///< the page follows the playhead while the transport runs
    // Gesture state.
    int downLane_ = -1, downStep_ = -1;
    bool downWasOn_ = false, moved_ = false;
    float downVelocity_ = 0.0f, downProbability_ = 1.0f, downOffset_ = 0.0f;
    int downNote_ = -1;
    DragMode dragMode_ = DragMode::Velocity;
};
