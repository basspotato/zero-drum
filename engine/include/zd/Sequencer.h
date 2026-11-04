#pragma once

#include "zd/Types.h"

#include <array>
#include <cstdint>

namespace zd {

inline constexpr int kMaxRatchet = 4;

struct Step
{
    bool on = false;
    float velocity = 1.0f;
    float probability = 1.0f;  ///< 0..1, decided by a seeded hash so it never depends on block size
    float microOffset = 0.0f;  ///< timing nudge as a fraction of one step, -0.5..0.5
    int note = -1;             ///< MIDI note for pitched parts; -1 = the part's own tune
    int ratchet = 1;           ///< hits per step, 1..kMaxRatchet (rolls)
};

/// One lane of a pattern. Each lane drives the part with the same index.
/// Lanes have independent lengths, so polymeters (a 5-step shaker over 16 steps) just work.
struct Lane
{
    int length = 16;
    std::array<Step, kMaxStepsPerLane> steps {};
};

/// A high-pass sweep over the whole drum bus, tied to the song position: over `steps` steps
/// the cutoff rises from startHz to endHz, then it starts over. The Build button sets one,
/// so the build thins out into the drop. steps 0 = off (the bus is untouched).
struct Sweep
{
    int steps = 0;
    float startHz = 30.0f;
    float endHz = 1500.0f;
    float resonance = 0.5f; ///< 0..1: a little peak at the cutoff makes the rise audible
};

struct Pattern
{
    std::array<Lane, kMaxParts> lanes {};
    double stepBeats = 0.25; ///< length of one step in quarter notes (0.25 = 16th notes)
    double swing = 0.5;      ///< 0.5 = straight, 0.75 = maximum; delays every odd step
    Sweep sweep {};
};

/// Up to four patterns played one after another (A, B, C, D), each for its own loop length
/// (its longest lane that plays, in whole bars). count 1 = just slot A, looping.
inline constexpr int kMaxChain = 4;
struct Chain
{
    std::array<Pattern, kMaxChain> slots {};
    int count = 1;
};

/// Bars in one loop of a pattern: its longest lane with a hit, rounded up to whole bars (1..16).
int patternBars(const Pattern& pattern) noexcept;

/// Sample-accurate step sequencer.
///
/// Every step's time is computed in beats and converted to an absolute sample index with
/// one rounding, so events land on exactly the same sample whatever the host block size.
/// That property is covered by the block-size null test.
class Sequencer
{
public:
    /// Slot A: the pattern that plays when no chain is set.
    Pattern& pattern() noexcept { return slots_[0]; }
    const Pattern& pattern() const noexcept { return slots_[0]; }

    /// The chain: slots 0..count-1 play in order, each for patternBars() bars, then repeat.
    /// Realtime safe (fixed storage); the engine sets it from its lock-free hand-off.
    void setChain(const Chain& chain) noexcept;
    int chainCount() const noexcept { return chainCount_; }
    const Pattern& slot(int i) const noexcept { return slots_[static_cast<std::size_t>(i)]; }
    /// The pattern playing at song position `ppq` (beats).
    const Pattern& patternAt(double ppq) const noexcept;
    /// The beat on which the pass playing at `ppq` began (0 without a chain).
    double passStartAt(double ppq) const noexcept;

    /// Sets the swing of every slot (it is a global setting, not part of a slot).
    void setSwing(double swing) noexcept
    {
        for (auto& p : slots_)
            p.swing = swing;
    }

    void setSeed(std::uint64_t seed) noexcept { seed_ = seed; }
    std::uint64_t seed() const noexcept { return seed_; }

    /// Writes the triggers that fall inside this block into `out`, unsorted.
    /// Returns the number written (at most `capacity`). Realtime safe.
    int collect(const Transport& transport, double sampleRate, int numSamples,
                NoteEvent* out, int capacity) const noexcept;

    /// Time of absolute step `k` of slot A in beats, including swing and the step's micro offset.
    double stepTime(int lane, std::int64_t k) const noexcept { return stepTimeIn(slots_[0], lane, k); }
    static double stepTimeIn(const Pattern& pattern, int lane, std::int64_t k) noexcept;

    /// Whether absolute step `k` of `lane` passes its probability draw (seeded, block-size free).
    /// `pass` tells the passes of a chain apart, so each one draws afresh (0 without a chain).
    bool passesProbability(int lane, std::int64_t k, float probability, std::int64_t pass = 0) const noexcept;
    /// Which pass of the chain plays at `ppq`: counts every slot pass from the song start (0 without a chain).
    std::int64_t passIndexAt(double ppq) const noexcept;

private:
    int collectPattern(const Pattern& pattern, const Transport& transport, double sampleRate, int numSamples,
                       NoteEvent* out, int capacity, std::int64_t pass) const noexcept;
    /// Where `ppq` falls in the chain: the slot, and the beat that slot's current pass began on.
    void locate(double ppq, int& slot, double& slotStart, double& slotEnd) const noexcept;

    std::array<Pattern, kMaxChain> slots_ {};
    std::array<double, kMaxChain> slotBeats_ { 4.0, 4.0, 4.0, 4.0 };
    double chainBeats_ = 4.0;
    int chainCount_ = 1;
    std::uint64_t seed_ = 1;
};

/// A starter groove so a fresh instance makes sound: kick, clap/snare, hats.
void loadDefaultPattern(Pattern& pattern);

} // namespace zd
