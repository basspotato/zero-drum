#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

/// Phase 0 editor: the generic parameter editor. Audition parts with MIDI notes 36/38/42.
/// The real GPU-drawn pattern grid replaces this in Phase 1.
class ZeroDrumEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ZeroDrumEditor(ZeroDrumProcessor&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
