#pragma once

#include "GenStrip.h"
#include "LookAndFeel.h"
#include "MixPage.h"
#include "PadGrid.h"
#include "PluginProcessor.h"
#include "SoundPage.h"
#include "StepGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/// The editor: a header with the generator (style, Generate, Mutate, Fill, Build), undo, kit,
/// export and the output meter; a pattern bar (slots, chain, drag-out chips); four tabs (Beat,
/// Pads, Mix, Sound); and a status line. Laid out on an 8 px grid.
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
    static constexpr int kKnobs = 8;
    static constexpr int kStripKnobs = 6, kPadKnobs = 7;
    static constexpr const char* kSuffixes[kKnobs] { "Attack", "Sustain", "Cutoff", "StripDrive", "VelTone", "RevSend", "", "" };
    static constexpr const char* kLabels[kKnobs] { "Attack", "Sustain", "Cutoff", "Drive", "Vel > Tone", "Reverb", "", "" };
    static constexpr const char* kPadLabels[kKnobs] { "Pitch", "Decay", "Start", "Level", "Pan", "Reverb", "Vary", "" };
    /// Pad knob i uses kPadSuffixes[kPadKnobMap[i]] (-1: unused).
    static constexpr int kPadKnobMap[kKnobs] { 0, 1, 2, 3, 4, 5, 8, -1 };
    ZeroDrumProcessor& owner_;
    int part_ = 0;
    std::array<Knob, kKnobs> knobs_;
    juce::ToggleButton crush_ { "Crush" };
    juce::ToggleButton reverse_ { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment_;
    juce::ComboBox choke_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chokeAttachment_;
    juce::TextButton load_ { "Load sample..." }, clear_ { "Clear" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> crushAttachment_;
};

/// A thin output peak meter: green, amber over -6 dBFS, red at 0 dBFS, with a peak hold.
class OutputMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit OutputMeter(ZeroDrumProcessor& owner) : owner_(owner)
    {
        setTooltip("Output level");
        startTimerHz(30);
    }
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    ZeroDrumProcessor& owner_;
    float level_ = 0.0f, hold_ = 0.0f;
    int holdFrames_ = 0;
};

/// Play state, tempo and position, at the right of the tab bar. In the standalone a click
/// starts and stops the free-running clock (as Space does).
class TransportView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit TransportView(ZeroDrumProcessor& owner);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    ZeroDrumProcessor& owner_;
    bool playing_ = false;
    double bpm_ = 0.0, beats_ = -1.0;
};

class ZeroDrumEditor final : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit ZeroDrumEditor(ZeroDrumProcessor&);
    ~ZeroDrumEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    void chooseExportFolder();
    void showKitMenu();
    bool keyPressed(const juce::KeyPress& key) override;
    void chooseKitFile(bool save);
    /// Scales the whole interface (0.75 to 1.5) and keeps the window's content the same.
    void setZoom(float zoom);
    juce::Rectangle<int> unzoomedBounds() const;

    ZeroDrumProcessor& owner_;
    juce::SharedResourcePointer<zdui::LookAndFeel> look_; ///< declared first, so it outlives every child
    juce::ComboBox style_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment_;
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" }, fill_ { "Fill" }, build_ { "Build" };
    juce::TextButton export_ { "Export..." };
    juce::TextButton kit_ { "Kit" };
    juce::TextButton undo_ { "Undo" }, redo_ { "Redo" };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Label dragLabel_ { {}, "DRAG" };
    juce::Label patternLabel_ { {}, "PATTERN" };
    juce::Label hints_;
    OutputMeter meter_ { owner_ };
    TransportView transport_ { owner_ };
    juce::ComboBox zoomBox_;
    float zoom_ = 1.0f;
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
    juce::ToggleButton fullLevel_ { "Full level" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fullLevelAttachment_;
    juce::TextButton tap_ { "Tap" };
    juce::TooltipWindow tooltips_ { this };
    SoundPage sound_;
    MixPage mix_;
    LayoutComponent beatPage_;
    GenStrip genStrip_;
    StepGrid steps_;
    LayoutComponent padsPage_;
    PadGrid pads_;
    PartPanel partPanel_;
    juce::Label padHint_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumEditor)
};
