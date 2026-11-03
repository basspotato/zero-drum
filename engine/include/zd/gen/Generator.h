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

/// Counts the active steps in one lane (within its length).
int countHits(const Lane& lane) noexcept;

} // namespace zd::gen
