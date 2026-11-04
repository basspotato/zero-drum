#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace zdui {

/// Zero Drum's look: flat, dark, one accent, colour only where it carries meaning.
///
/// Built on an 8 px grid with three type sizes (kHeading, kLabel, kCaption). Buttons are flat
/// rounded rectangles that light up when on; toggles are small switches; knobs are a 270 degree
/// arc that fills from the left, or from the centre for bipolar values such as pan.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float start, float end, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool separator, bool active, bool highlighted, bool ticked,
                           bool hasSubMenu, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

/// Readable values with units, from the parameter's own label: "120 ms", "1.2 kHz", "-6.0 dB",
/// "80 %" for 0..1 amounts, "L 30" / "C" / "R 30" for pan.
juce::String formatValue(const juce::RangedAudioParameter& param, double value);

/// Attaches a knob to a parameter and gives it the readable text above (call instead of
/// constructing a SliderAttachment directly).
std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachKnob(juce::AudioProcessorValueTreeState& state,
                                                                                const juce::String& id, juce::Slider& slider);

/// A knob with its caption above and its value below, the one control style used everywhere.
void styleKnob(juce::Slider& slider, juce::Label& caption, const juce::String& text, juce::Colour colour);

} // namespace zdui
