#pragma once

#include "MixPage.h"
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
    /// lane -1 drags the whole beat as MIDI, kAudioLoop the rendered loop as a WAV.
    static constexpr int kAudioLoop = -2;
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

/// A pattern slot button: click to edit the slot, right-click for its menu.
class SlotButton final : public juce::TextButton
{
public:
    std::function<void()> onMenu;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        juce::TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu())
            juce::TextButton::mouseUp(e);
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
    juce::ToggleButton reverse_ { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment_;
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
    void showKitMenu();
    bool keyPressed(const juce::KeyPress& key) override;
    void chooseKitFile(bool save);

    ZeroDrumProcessor& owner_;
    juce::ComboBox style_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment_;
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" }, fill_ { "Fill" }, build_ { "Build" };
    juce::TextButton export_ { "Export..." };
    juce::TextButton kit_ { "Kit" };
    juce::TextButton undo_ { "Undo" }, redo_ { "Redo" };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label dragLabel_ { {}, "Drag out:" };
    std::vector<std::unique_ptr<MidiDragChip>> chips_;
    juce::Label summary_;
    std::array<SlotButton, zd::kMaxChain> slots_;
    juce::ComboBox chain_;
    juce::TextButton autoFill_ { "Auto fill" };
    void refreshSlots();
    void showSlotMenu(int slot);
    void selectSlot(int slot);
    void loadKitFile(const juce::File& file);
    static juce::File kitsFolder();
    juce::ComboBox repeatRate_;
    juce::Label repeatLabel_ { {}, "Repeat" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> repeatAttachment_;
    juce::TooltipWindow tooltips_ { this };
    juce::GenericAudioProcessorEditor generic_;
    MixPage mix_;
    StepGrid steps_;
    LayoutComponent padsPage_;
    PadGrid pads_;
    PartPanel partPanel_;
    juce::Label padHint_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
