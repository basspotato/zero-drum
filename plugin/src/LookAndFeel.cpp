#include "LookAndFeel.h"

#include <cmath>

namespace zdui {

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::TextButton::buttonColourId, kRaised);
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::ComboBox::backgroundColourId, kRaised);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::outlineColourId, kLine);
    setColour(juce::ComboBox::arrowColourId, kDim);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId, kText);
    setColour(juce::PopupMenu::headerTextColourId, kDim);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderOutlineColourId, kCellOff);
    setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    setColour(juce::TooltipWindow::backgroundColourId, kPanel);
    setColour(juce::TooltipWindow::textColourId, kText);
    setColour(juce::TooltipWindow::outlineColourId, kLine);
    setColour(juce::AlertWindow::backgroundColourId, kPanel);
    setColour(juce::AlertWindow::textColourId, kText);
    setColour(juce::AlertWindow::outlineColourId, kLine);
    setColour(juce::TextEditor::backgroundColourId, kRaised);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kLine);
    setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
    setColour(juce::TabbedComponent::backgroundColourId, kBackground);
    setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, kCellOff);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour(juce::TextButton::buttonOnColourId) : background;
    if (!b.isEnabled())
        fill = fill.withMultipliedAlpha(0.45f);
    else if (down)
        fill = fill.brighter(0.18f);
    else if (highlighted)
        fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 6.0f);
    if (!on)
    {
        g.setColour(highlighted ? kLine.brighter(0.3f) : kLine);
        g.drawRoundedRectangle(r, 6.0f, 1.0f);
    }
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions(juce::jmin(kLabel, static_cast<float>(buttonHeight) * 0.55f), juce::Font::bold);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A small switch, then the label.
    auto r = b.getLocalBounds().toFloat();
    const float h = 16.0f;
    const auto track = juce::Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - h * 0.5f, 28.0f, h);
    const bool on = b.getToggleState();
    const auto accent = b.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? accent : (highlighted ? kCellOff.brighter(0.1f) : kCellOff));
    g.fillRoundedRectangle(track, h * 0.5f);
    const float knob = h - 4.0f;
    const float kx = on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f;
    g.setColour(on ? juce::Colours::black.withAlpha(0.75f) : kDim);
    g.fillEllipse(kx, track.getY() + 2.0f, knob, knob);
    g.setColour(b.isEnabled() ? b.findColour(juce::ToggleButton::textColourId) : kDim);
    g.setFont(juce::FontOptions(kLabel));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(track.getRight() + 8.0f - r.getX()), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).brighter(box.isMouseOver(true) ? 0.06f : 0.0f));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(box.hasKeyboardFocus(true) ? kAccent : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    // Chevron.
    const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
    juce::Path p;
    p.startNewSubPath(cx - 4.0f, cy - 2.0f);
    p.lineTo(cx, cy + 2.5f);
    p.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::FontOptions(kLabel);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end,
                                   juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float stroke = juce::jmax(3.0f, radius * 0.16f);
    const float arcRadius = radius - stroke * 0.5f;
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour(s.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar ranges (pan, pitch) fill from the middle.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0
                         && std::abs(s.getMinimum() + s.getMaximum()) < 1.0e-6 * (s.getMaximum() - s.getMinimum());
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs(angle - from) > 1.0e-3f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId).withMultipliedAlpha(s.isEnabled() ? 1.0f : 0.4f));
        g.strokePath(value, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // A short pointer from near the centre.
    const float inner = radius * 0.28f, outer = radius * 0.62f;
    const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
    g.setColour(s.isMouseOverOrDragging() ? kText : kText.withAlpha(0.8f));
    g.drawLine(centre.x + dir.x * inner, centre.y + dir.y * inner, centre.x + dir.x * outer, centre.y + dir.y * outer, 2.2f);
}

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(juce::FontOptions(kLabel - 1.0f));
    l->setColour(juce::Label::textColourId, kText);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType(juce::Justification::centred);
    return l;
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(kPanel);
    g.setColour(kLine);
    g.drawRect(0, 0, width, height, 1);
}

void LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active, bool highlighted,
                                    bool ticked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                                    const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (separator)
    {
        g.setColour(kLine);
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    auto r = area.reduced(4, 1);
    if (highlighted && active)
    {
        g.setColour(kAccent.withAlpha(0.22f));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
    }
    g.setColour(!active ? kDim.withAlpha(0.6f) : textColour != nullptr ? *textColour : kText);
    g.setFont(getPopupMenuFont());
    auto textArea = r.reduced(10, 0);
    if (ticked)
    {
        const auto dot = textArea.removeFromLeft(14).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        g.setColour(kAccent);
        g.fillEllipse(dot);
        g.setColour(active ? kText : kDim);
    }
    else
        textArea.removeFromLeft(14);
    if (icon != nullptr)
        icon->drawWithin(g, textArea.removeFromLeft(16).toFloat(), juce::RectanglePlacement::centred, 1.0f);
    if (hasSubMenu)
    {
        const auto a = textArea.removeFromRight(10).toFloat();
        juce::Path p;
        p.startNewSubPath(a.getX() + 2.0f, a.getCentreY() - 4.0f);
        p.lineTo(a.getX() + 6.0f, a.getCentreY());
        p.lineTo(a.getX() + 2.0f, a.getCentreY() + 4.0f);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }
    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setColour(kDim);
        g.drawText(shortcut, textArea, juce::Justification::centredRight);
    }
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions(kLabel + 1.0f);
}

void LookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setColour(kDim);
    g.setFont(juce::FontOptions(kCaption, juce::Font::bold));
    g.drawText(name.toUpperCase(), area.reduced(14, 0), juce::Justification::bottomLeft);
}

int LookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int)
{
    return juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(kLabel + 1.0f, juce::Font::bold), b.getButtonText()) + 32;
}

void LookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    const auto r = b.getLocalBounds();
    const bool front = b.isFrontTab();
    g.setColour(front ? kText : (over ? kText.withAlpha(0.8f) : kDim));
    g.setFont(juce::FontOptions(kLabel + 1.0f, juce::Font::bold));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
    if (front)
    {
        g.setColour(kAccent);
        g.fillRoundedRectangle(r.toFloat().removeFromBottom(3.0f).reduced(10.0f, 0.0f), 1.5f);
    }
}

void LookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
{
    g.setColour(kLine);
    g.fillRect(0, h - 1, w, 1);
}

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(kPanel);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(kLine);
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(kText);
    g.setFont(juce::FontOptions(kLabel - 1.0f));
    g.drawFittedText(text, r.reduced(10.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const int w = juce::jmin(320, juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(kLabel - 1.0f), text) + 24);
    const int lines = juce::jmax(1, (juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(kLabel - 1.0f), text) + 24) / 320 + 1);
    const int h = 12 + lines * 16;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

juce::String formatValue(const juce::RangedAudioParameter& param, double v)
{
    const auto unit = param.getLabel();
    const auto range = param.getNormalisableRange();
    if (auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(&param))
        return choice->choices[juce::jlimit(0, choice->choices.size() - 1, juce::roundToInt(v))];
    if (param.getName(64).endsWithIgnoreCase("Root Note"))
        return juce::MidiMessage::getMidiNoteName(juce::roundToInt(v), true, true, 3);
    if (param.getName(64).endsWithIgnoreCase("Pan"))
    {
        const int amount = juce::roundToInt(std::abs(v) * 100.0);
        return amount == 0 ? juce::String("C") : (v < 0.0 ? "L " : "R ") + juce::String(amount);
    }
    if (unit == "Hz")
        return v >= 1000.0 ? juce::String(v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
    if (unit == "ms")
        return v >= 1000.0 ? juce::String(v / 1000.0, 2) + " s" : juce::String(juce::roundToInt(v)) + " ms";
    if (unit == "s")
        return v < 0.005 ? juce::String("Off") : juce::String(v, 2) + " s";
    if (unit == "dB")
        return juce::String(v, 1) + " dB";
    if (unit == "%")
        return juce::String(juce::roundToInt(v)) + " %";
    if (unit == "st")
        return (v > 0.0 ? "+" : "") + juce::String(v, 1) + " st";
    if (unit.startsWithChar(':'))
        return juce::String(v, 1) + unit; // ratios: "4.0:1"
    if (unit.isNotEmpty())
        return juce::String(v, std::abs(v) >= 100.0 ? 0 : 1) + " " + unit;
    // Plain amounts read best as a percentage: signed when the range is centred on zero.
    if (range.start < 0.0f && juce::approximatelyEqual(range.start, -range.end) && range.end <= 1.0f)
    {
        const int amount = juce::roundToInt(v * 100.0);
        return (amount > 0 ? "+" : "") + juce::String(amount) + " %";
    }
    if (range.start >= 0.0f && range.end <= 1.0f)
        return juce::String(juce::roundToInt(v * 100.0)) + " %";
    return std::abs(v) >= 100.0 ? juce::String(juce::roundToInt(v)) : juce::String(v, std::abs(v) >= 10.0 ? 1 : 2);
}

std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachKnob(juce::AudioProcessorValueTreeState& state,
                                                                                const juce::String& id, juce::Slider& slider)
{
    auto attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider);
    // The attachment installs the parameter's full-precision text; replace it with a readable one.
    if (auto* param = state.getParameter(id))
        slider.textFromValueFunction = [param](double v) { return formatValue(*param, v); };
    slider.updateText();
    return attachment;
}

void styleKnob(juce::Slider& slider, juce::Label& caption, const juce::String& text, juce::Colour colour)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 16);
    slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    caption.setText(text, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    caption.setColour(juce::Label::textColourId, kDim);
    caption.setFont(juce::FontOptions(kCaption, juce::Font::bold));
}

} // namespace zdui
