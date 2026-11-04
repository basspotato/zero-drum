#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

/// Zero Drum's own palette: near-black panels, one colour per lane.
namespace zdui {

inline const juce::Colour kBackground { 0xff15161a };
inline const juce::Colour kPanel { 0xff1e2026 };
inline const juce::Colour kCellOff { 0xff2a2d36 };
inline const juce::Colour kText { 0xffe8e8ec };
inline const juce::Colour kDim { 0xff8a8d98 };
inline const juce::Colour kAccent { 0xffff7a2f };
inline const juce::Colour kRaised { 0xff262932 };  ///< buttons and fields
inline const juce::Colour kLine { 0xff30333d };    ///< hairlines and outlines
inline const juce::Colour kGood { 0xff2fd1c1 };
inline const juce::Colour kWarn { 0xffffc23d };
inline const juce::Colour kHot { 0xffff3d5a };

/// Three type sizes, and the 8 px grid everything sits on.
inline constexpr float kHeading = 17.0f;
inline constexpr float kLabel = 13.0f;
inline constexpr float kCaption = 11.0f;
inline constexpr int kGrid = 8;

/// Lane colours: kick, snare, hat, perc, 808, then a neutral for parts without a default sound.
inline juce::Colour laneColour(int lane)
{
    static const juce::Colour colours[] { juce::Colour(0xffff7a2f), juce::Colour(0xff3a8bff), juce::Colour(0xff2fd1c1),
                                          juce::Colour(0xffb36bff), juce::Colour(0xffff3d5a) };
    if (lane >= 0 && lane < 5)
        return colours[lane];
    // Pads 6..16: hues spread between the kit colours, a little softer so the kit stays on top.
    if (lane >= 5 && lane < 16)
        return juce::Colour::fromHSV(std::fmod(0.11f + 0.0909f * static_cast<float>(lane - 5), 1.0f), 0.55f, 0.85f, 1.0f);
    return juce::Colour(0xff5a5e6b);
}

} // namespace zdui
