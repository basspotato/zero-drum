#pragma once

#include "zd/Sequencer.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace zd::gen {

/// Lane roles used by the generator. They match the default kit's part order.
enum Role : int { Kick = 0, Snare = 1, Hat = 2, Perc = 3, Sub = 4, kNumRoles = 5 };

enum class Style : std::uint8_t
{
    Trap,
    Drill,
    BoomBap,
    House,
    Techno,
    UKGarage,
    DrumAndBass,
    Afrobeats,
    Amapiano,
    JerseyClub,
    Reggaeton,
    Pop,
    HipHop,
    Dubstep,
    Trance,
    Hardstyle,
    Festival,
    kCount,
};

std::string_view styleName(Style style) noexcept;

/// Typical tempo range for a style, as a hint for the UI and the standalone clock.
struct TempoRange { double low, high; };
TempoRange styleTempo(Style style) noexcept;

struct GenerateParams
{
    Style style = Style::Trap;
    float density = 0.5f;     ///< 0 = sparse, 1 = busy
    float complexity = 0.5f;  ///< 0 = straight and plain, 1 = syncopation, ghosts, rolls
    int rootNote = 33;        ///< 808 root (MIDI); A1 by default
    std::uint64_t seed = 1;
    /// Lanes that must not change. Bit i locks lane i.
    std::uint32_t lockMask = 0;
    float groove = 1.0f;      ///< 0 = on the grid, flat accents, straight; 1 = the style's full feel
};

/// Rule-based beat generator.
///
/// A style is a grammar: anchor hits that define the genre (four-on-the-floor kick, the
/// half-time snare, the dembow figure) plus per-step weights that a seeded random draw
/// fills in, scaled by density and complexity. Same params and seed, same beat.
///
/// Not realtime safe in spirit (it is meant for the message or worker thread), though it
/// does not allocate. Writes into `pattern`, leaving locked lanes untouched.
void generate(const GenerateParams& params, Pattern& pattern) noexcept;

/// Small controlled changes to unlocked lanes: moves, adds or removes one to three hits
/// per lane and may toggle a roll. `amount` 0..1 scales how many changes happen.
void mutate(Pattern& pattern, float amount, std::uint64_t seed, std::uint32_t lockMask) noexcept;

/// Busier last bar-quarter on snare and perc lanes, for a fill variation.
void addFill(Pattern& pattern, float intensity, std::uint64_t seed, std::uint32_t lockMask) noexcept;

/// A build-up of `bars` bars (1..4) into a drop: a snare roll that rises from 8ths to 16ths
/// to 32nds to 64ths with a velocity ramp, a kick on the quarters that drops out for the
/// last quarter of the build, offbeat hats, no 808 or perc, and an 8th of silence at the
/// end before the drop. Unlocked lanes are replaced; all of them take the build's length.
/// It also sets the pattern's sweep: a high-pass rising over the whole build (see Sweep).
void makeBuild(Pattern& pattern, int bars, std::uint32_t lockMask) noexcept;

/// Euclidean rhythm: spreads `hits` as evenly as possible over the lane's length (Bjorklund's
/// even distribution, written as a ceiling formula), starting from `rotate` steps in. The
/// lane's existing steps are replaced; velocities come out slightly accented on the first hit.
void euclid(Lane& lane, int hits, int rotate = 0) noexcept;

/// Seeded humanize: small timing nudges (up to 6% of a step at amount 1) and velocity
/// variation (up to +/-12%) on every hit of the unlocked lanes. The same seed always gives the
/// same result; amount 0 changes nothing.
void humanize(Pattern& pattern, float amount, std::uint64_t seed, std::uint32_t lockMask) noexcept;

/// Counts the active steps in one lane (within its length).
int countHits(const Lane& lane) noexcept;

} // namespace zd::gen
