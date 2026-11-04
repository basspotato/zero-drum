#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

/// Every sound parameter, one part at a time: a list of parts on the left (the five kit
/// voices, the loaded pads and the session settings), and on the right that part's controls
/// in titled cards: its voice, its channel strip (or its sample settings). Built from the
/// parameter layout itself, so a new parameter shows up here without extra code.
class SoundPage final : public juce::Component
{
public:
    explicit SoundPage(ZeroDrumProcessor& owner);
    ~SoundPage() override;

    /// Shows a part (0..15) or the session settings (kSession).
    void showPart(int part);
    int shownPart() const noexcept { return part_; }
    /// Rebuilds the part list (after a pad gets or loses its sample, or a rename).
    void refreshList();
    /// Called when the user picks a part in the list.
    std::function<void(int)> onSelect;

    static constexpr int kSession = -1;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    struct Control;
    struct Section;
    class Content;

    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> listRow(int index) const;
    void rebuild();
    juce::String shortName(const juce::String& paramName) const;

    ZeroDrumProcessor& owner_;
    int part_ = 0;
    std::vector<int> listParts_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
};
