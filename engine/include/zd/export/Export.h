#pragma once

#include "zd/Engine.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace zd::exporting {

/// Writes a 32-bit float stereo WAV. Returns false on I/O failure.
bool writeWav(const std::filesystem::path& path, const std::vector<float>& left,
              const std::vector<float>& right, int sampleRate);

/// One note in an exported MIDI track, in ticks at kMidiPpq.
struct MidiNote
{
    int tick = 0;
    int lengthTicks = 0;
    int note = 36;
    int velocity = 100;
};

inline constexpr int kMidiPpq = 480;

/// The notes a lane actually plays over `numSteps` absolute steps starting at `firstStep`,
/// with swing, micro-timing, ratchets and seeded probability applied exactly as the
/// sequencer plays them. Ticks are relative to `firstStep`. `defaultNote` is used for
/// steps without a note (drum lanes).
std::vector<MidiNote> realizeLane(const Sequencer& sequencer, int lane, long long firstStep,
                                  long long numSteps, int defaultNote);

/// Writes a type-1 standard MIDI file: a tempo track plus one named track per entry.
struct MidiTrack
{
    std::string name;
    int channel = 9; ///< 0-based; 9 = General MIDI drums
    std::vector<MidiNote> notes;
};
bool writeMidi(const std::filesystem::path& path, const std::vector<MidiTrack>& tracks, double bpm);

struct ExportOptions
{
    int bars = 2;
    double bpm = 120.0;
    /// Render one loop first and keep the second, so decays from the end of the loop
    /// wrap into its start: the files loop seamlessly in a DAW or sampler.
    bool seamlessLoop = true;
    std::array<std::string, kMaxParts> partNames {};
    /// Only mix.wav (no part files, MIDI or pattern): for dragging the loop out as audio.
    bool mixOnly = false;
};

struct ExportResult
{
    bool ok = false;
    std::vector<std::filesystem::path> files;
    std::string error;
};

/// Exports the engine's current kit and pattern into `directory`:
///   mix.wav, parts/NN-name.wav (one per part that plays), midi/NN-name.mid per lane,
///   midi/all-parts.mid, and pattern.zdpattern.
/// Each part file is rendered on its own with the other parts muted, so the part files
/// sum exactly to the mix: stems by construction, no separation.
/// `engine` must be prepared; it is reset before and after. Not realtime safe.
ExportResult exportLoop(Engine& engine, const ExportOptions& options, const std::filesystem::path& directory);

/// Bars in one loop of the pattern: the longest lane that plays, rounded up to whole bars (1..16).
int loopBars(const Pattern& pattern) noexcept;
/// Bars in one cycle of what the sequencer plays: loopBars() of slot A, or a chain's total.
int loopBars(const Sequencer& sequencer) noexcept;

/// MIDI tracks for the first `bars` of the pattern, one per lane that plays (or only `lane`
/// when it is >= 0). Lanes in `pitchedMask` go on MIDI channel 2 with their notes, a melodic
/// kick on channel 3, the rest on channel 10 with defaultMidiNote. Used for drag-and-drop clips.
std::vector<MidiTrack> patternTracks(const Sequencer& sequencer, int bars, const std::array<std::string, kMaxParts>& names,
                                     std::uint32_t pitchedMask, int lane = -1);

/// The kick lane (part 0) with notes on its steps (hardstyle, festival) is exported on this
/// zero-based MIDI channel (3 in DAW terms) with its notes, like the 808 on channel 2; the
/// plugin plays the kick melodically from channel 3 too.
inline constexpr int kMelodicKickChannel = 2;
bool melodicKick(const Pattern& pattern, int lane) noexcept;
/// Whether the lane plays melodically in any slot of the sequencer's chain.
bool melodicKick(const Sequencer& sequencer, int lane) noexcept;

/// General MIDI drum note for a default-kit part, used for drum lanes in MIDI export.
int defaultMidiNote(int part) noexcept;

} // namespace zd::exporting
