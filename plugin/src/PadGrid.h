#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

/// Sixteen pads in a 4x4 grid, one per part, pad 1 bottom-left. Click to play (higher on the
/// pad = louder), hold Shift for tempo-synced 16th note repeat, drag a pad out of the plugin
/// to drop that lane as a MIDI clip. Pads flash on every hit, from the sequencer too.
class PadGrid final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PadGrid(ZeroDrumProcessor& owner);
    ~PadGrid() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Drop an audio file on pads 6..16 to load it.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /// Opens a file chooser for the pad's sample.
    void chooseSample(int part);
    /// Called after a pad's sample changes (the part panel refreshes its title).
    std::function<void(int)> onSampleChanged;

    int selectedPart() const noexcept { return selected_; }
    std::function<void(int)> onSelect;

    static constexpr int kPads = 16;
    /// Part played by the pad in grid cell (column, row from the top).
    static int partAt(int column, int rowFromTop) noexcept { return (3 - rowFromTop) * 4 + column; }

private:
    void timerCallback() override;
    juce::Rectangle<float> padBounds(int part) const;
    int padAt(juce::Point<float> p) const;
    float velocityAt(int part, juce::Point<float> p) const;

    ZeroDrumProcessor& owner_;
    std::array<std::uint32_t, kPads> lastHits_ {};
    std::array<float, kPads> glow_ {};
    int selected_ = 0;
    int pressed_ = -1;
    bool repeating_ = false;
    bool dragging_ = false;
    int dropTarget_ = -1;
    std::unique_ptr<juce::FileChooser> chooser_;
};
