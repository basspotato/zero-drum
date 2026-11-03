#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// Phase 1 editor: a generator bar (style, Generate, Mutate, Fill) over the generic
/// parameter editor. Audition parts with MIDI notes 36/38/42, the 808 on MIDI channel 2.
/// The GPU-drawn pattern grid replaces the generic editor later in Phase 1.
class ZeroDrumEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ZeroDrumEditor(ZeroDrumProcessor&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    ZeroDrumProcessor& owner_;
    juce::ComboBox style_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment_;
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" }, fill_ { "Fill" };
    juce::Label summary_;
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
