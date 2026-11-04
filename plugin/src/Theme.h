#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/// Zero Drum's own palette: near-black panels, one colour per lane.
namespace zdui {

inline const juce::Colour kBackground { 0xff15161a };
inline const juce::Colour kPanel { 0xff1e2026 };
inline const juce::Colour kCellOff { 0xff2a2d36 };
inline const juce::Colour kText { 0xffe8e8ec };
inline const juce::Colour kDim { 0xff8a8d98 };
inline const juce::Colour kAccent { 0xffff7a2f };

/// Lane colours: kick, snare, hat, perc, 808, then a neutral for parts without a default sound.
inline juce::Colour laneColour(int lane)
{
    static const juce::Colour colours[] { juce::Colour(0xffff7a2f), juce::Colour(0xff3a8bff), juce::Colour(0xff2fd1c1),
                                          juce::Colour(0xffb36bff), juce::Colour(0xffff3d5a) };
    return lane >= 0 && lane < 5 ? colours[lane] : juce::Colour(0xff5a5e6b);
}

} // namespace zdui
