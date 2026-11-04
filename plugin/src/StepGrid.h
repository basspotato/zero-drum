#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

/// The pattern as a grid: one row per default-kit lane, one cell per step. Click an empty
/// cell to add a hit, click a hit to remove it, drag a hit up or down for velocity,
/// right-click a hit to cycle its roll (1, 2, 3, 4 hits per step). The playhead follows the
/// host, and every edit goes straight to the audio thread and into the saved state.
class StepGrid final : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid(ZeroDrumProcessor& owner);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr int kLanes = ZeroDrumProcessor::kNumStripParts;
    static constexpr int kHeaderWidth = 64;

private:
    void timerCallback() override;
    int columns() const noexcept;
    juce::Rectangle<float> cellBounds(int lane, int step) const;
    bool cellAt(juce::Point<float> p, int& lane, int& step) const;

    ZeroDrumProcessor& owner_;
    zd::Pattern pattern_;
    int playStep_ = -1;
    // Gesture state.
    int downLane_ = -1, downStep_ = -1;
    bool downWasOn_ = false, moved_ = false;
    float downVelocity_ = 0.0f;
};
