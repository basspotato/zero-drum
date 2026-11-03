#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// Phase 1 editor: a generator bar (style, Generate, Mutate, Fill, Export, Drag MIDI) over the generic
/// parameter editor. Audition parts with MIDI notes 36/38/42, the 808 on MIDI channel 2.
/// The GPU-drawn pattern grid replaces the generic editor later in Phase 1.
/// A chip you drag out of the plugin to drop a MIDI clip on a DAW track. `lane` -1 = whole beat.
class MidiDragChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragChip(ZeroDrumProcessor& owner, int lane, juce::String label);
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging_ = false; repaint(); }

private:
    ZeroDrumProcessor& owner_;
    int lane_;
    juce::String label_;
    bool dragging_ = false;
};

class ZeroDrumEditor final : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit ZeroDrumEditor(ZeroDrumProcessor&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void chooseExportFolder();

    ZeroDrumProcessor& owner_;
    juce::ComboBox style_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment_;
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" }, fill_ { "Fill" };
    juce::TextButton export_ { "Export..." };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label dragLabel_ { {}, "Drag MIDI:" };
    std::vector<std::unique_ptr<MidiDragChip>> chips_;
    juce::Label summary_;
    juce::TooltipWindow tooltips_ { this };
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
