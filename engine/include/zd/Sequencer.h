#pragma once

#include "zd/Types.h"

#include <array>
#include <cstdint>

namespace zd {

struct Step
{
    bool on = false;
    float velocity = 1.0f;
    float probability = 1.0f;  ///< 0..1, decided by a seeded hash so it never depends on block size
    float microOffset = 0.0f;  ///< timing nudge as a fraction of one step, -0.5..0.5
};

/// One lane of a pattern. Each lane drives the part with the same index.
/// Lanes have independent lengths, so polymeters (a 5-step shaker over 16 steps) just work.
struct Lane
{
    int length = 16;
    std::array<Step, kMaxStepsPerLane> steps {};
};

struct Pattern
{
    std::array<Lane, kMaxParts> lanes {};
    double stepBeats = 0.25; ///< length of one step in quarter notes (0.25 = 16th notes)
    double swing = 0.5;      ///< 0.5 = straight, 0.75 = maximum; delays every odd step
};

/// Sample-accurate step sequencer.
///
/// Every step's time is computed in beats and converted to an absolute sample index with
/// one rounding, so events land on exactly the same sample whatever the host block size.
/// That property is covered by the block-size null test.
class Sequencer
{
public:
    Pattern& pattern() noexcept { return pattern_; }
    const Pattern& pattern() const noexcept { return pattern_; }

    void setSeed(std::uint64_t seed) noexcept { seed_ = seed; }
    std::uint64_t seed() const noexcept { return seed_; }

    /// Writes the triggers that fall inside this block into `out`, unsorted.
    /// Returns the number written (at most `capacity`). Realtime safe.
    int collect(const Transport& transport, double sampleRate, int numSamples,
                NoteEvent* out, int capacity) const noexcept;

    /// Time of absolute step `k` in beats, including swing and the step's micro offset.
    double stepTime(int lane, std::int64_t k) const noexcept;

private:
    bool passesProbability(int lane, std::int64_t k, float probability) const noexcept;

    Pattern pattern_;
    std::uint64_t seed_ = 1;
};

/// A starter groove so a fresh instance makes sound: kick, clap/snare, hats.
void loadDefaultPattern(Pattern& pattern);

} // namespace zd
