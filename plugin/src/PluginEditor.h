#pragma once

#include "PadGrid.h"
#include "PluginProcessor.h"
#include "StepGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// Phase 1 editor: a generator bar (style, Generate, Mutate, Fill, Build, Export, Drag MIDI)
/// over three tabs: Beat (the step grid), Pads (4x4 pads with the selected part's controls)
/// and Sound (every parameter).
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

/// A plain container that lays out its children through a callback.
class LayoutComponent final : public juce::Component
{
public:
    std::function<void()> onResize;
    void resized() override
    {
        if (onResize)
            onResize();
    }
};

/// The selected pad's main controls: strip, velocity to tone and reverb send.
class PartPanel final : public juce::Component
{
public:
    explicit PartPanel(ZeroDrumProcessor& owner);
    void showPart(int part);
    /// Load / Clear for sample pads; the editor wires these to the pad grid.
    std::function<void(int)> onLoad, onClear;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    static constexpr int kKnobs = 6;
    static constexpr const char* kSuffixes[kKnobs] { "Attack", "Sustain", "Cutoff", "StripDrive", "VelTone", "RevSend" };
    static constexpr const char* kLabels[kKnobs] { "Attack", "Sustain", "Cutoff", "Drive", "Vel > Tone", "Reverb" };
    static constexpr const char* kPadLabels[kKnobs] { "Pitch", "Decay", "Start", "Level", "Pan", "Reverb" };
    ZeroDrumProcessor& owner_;
    int part_ = 0;
    std::array<Knob, kKnobs> knobs_;
    juce::ToggleButton crush_ { "Crush" };
    juce::TextButton load_ { "Load sample..." }, clear_ { "Clear" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> crushAttachment_;
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
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" }, fill_ { "Fill" }, build_ { "Build" };
    juce::TextButton export_ { "Export..." };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label dragLabel_ { {}, "Drag MIDI:" };
    std::vector<std::unique_ptr<MidiDragChip>> chips_;
    juce::Label summary_;
    juce::TooltipWindow tooltips_ { this };
    juce::GenericAudioProcessorEditor generic_;
    StepGrid steps_;
    LayoutComponent padsPage_;
    PadGrid pads_;
    PartPanel partPanel_;
    juce::Label padHint_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
