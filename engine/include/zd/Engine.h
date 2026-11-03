#pragma once

#include "zd/Sequencer.h"
#include "zd/Strip.h"
#include "zd/Types.h"
#include "zd/engines/AnalogKick.h"
#include "zd/engines/AnalogSnare.h"
#include "zd/engines/FmPerc.h"
#include "zd/engines/SamplePlayer.h"
#include "zd/engines/Sub808.h"

#include <array>
#include <atomic>
#include <vector>

namespace zd {

/// Mix settings for one part.
struct PartMix
{
    float gainDb = 0.0f;
    float pan = 0.0f;   ///< -1 left .. 1 right
    bool mono = false;  ///< true: a new hit chokes the previous one (typical for kicks and 808s)
    bool mute = false;  ///< still rendered (state stays in step), just not mixed
};

/// One of the 16 drum parts: an engine type, its parameters, its voices and its mix.
class Part
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    /// `note` is a MIDI note for pitched engines (808); -1 uses the part's own tune.
    void trigger(float velocity, int note = -1) noexcept;
    void render(float* left, float* right, int numSamples) noexcept;
    bool isActive() const noexcept;
    void reset() noexcept;

    EngineType type = EngineType::Off;
    KickParams kick;
    SnareParams snare;
    FmParams fm;
    Sub808Params sub;
    SampleParams sampleParams;
    SampleDataPtr sample; ///< set only while audio is stopped (see Engine::setSample)
    StripParams strip;
    PartMix mix;

private:
    Voice& voice(int i) noexcept;
    int allocateVoice() noexcept;

    std::array<AnalogKickVoice, kVoicesPerPart> kickVoices_;
    std::array<AnalogSnareVoice, kVoicesPerPart> snareVoices_;
    std::array<SamplePlayerVoice, kVoicesPerPart> sampleVoices_;
    std::array<FmPercVoice, kVoicesPerPart> fmVoices_;
    std::array<Sub808Voice, kVoicesPerPart> subVoices_;
    Strip strip_;
    std::vector<float> scratch_;
    unsigned ageCounter_ = 0;
    double lastSubHz_ = 0.0;
};

/// The Zero Drum engine. Framework-free: no JUCE, no OS calls.
///
/// Threading contract:
/// - prepare(), setSample() and pattern edits run only while process() is not running.
/// - process() and the parameter setters on Part are realtime safe.
class Engine
{
public:
    Engine();

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /// Renders one block. `external` holds MIDI or pad triggers for this block (any order).
    /// Output is written (not added) to `left` and `right`.
    void process(const Transport& transport, const NoteEvent* external, int numExternal,
                 float* left, float* right, int numSamples) noexcept;

    Part& part(int index) noexcept { return parts_[static_cast<std::size_t>(index)]; }
    Sequencer& sequencer() noexcept { return sequencer_; }

    void setSample(int partIndex, SampleDataPtr sample);

    /// Hands a new pattern to the audio thread from another thread (UI, generator).
    /// Lock-free and allocation-free on both sides; the audio thread picks it up at the
    /// start of its next block. Returns false if the audio thread is copying the previous
    /// one right now; call again.
    bool submitPattern(const Pattern& pattern) noexcept;

    double sampleRate() const noexcept { return sampleRate_; }

    /// Master output gain. Realtime safe. Default 0 dB; the plugin and renderer use
    /// kDefaultMasterDb for headroom until the oversampled master clipper lands.
    void setMasterGainDb(float db) noexcept;
    static constexpr float kDefaultMasterDb = -2.0f;

private:
    void renderParts(float* left, float* right, int from, int to) noexcept;
    void applyPendingPattern() noexcept;

    enum PendingState : int { Empty = 0, Writing = 1, Ready = 2, Reading = 3 };
    Pattern pendingPattern_ {};
    std::atomic<int> pendingState_ { Empty };

    std::array<Part, kMaxParts> parts_;
    Sequencer sequencer_;
    std::array<NoteEvent, kMaxEventsPerBlock> events_ {};
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    float masterGain_ = 1.0f;
};

/// Free-running clock for the standalone app and tests: advances a sample counter and
/// reports the matching transport position for each block.
class InternalClock
{
public:
    explicit InternalClock(double bpm = 120.0) noexcept : bpm_(bpm) {}

    Transport next(double sampleRate, int numSamples) noexcept
    {
        Transport t { true, bpm_, static_cast<double>(samplePosition_) * bpm_ / (60.0 * sampleRate) };
        samplePosition_ += numSamples;
        return t;
    }

private:
    double bpm_;
    long long samplePosition_ = 0;
};

/// Sets up the default kit: kick (part 0), snare (1), synthesized hat sample (2),
/// FM percussion (3) and 808 sub (4).
/// Allocates; call after prepare() (the hat is rendered at the engine's sample rate).
void loadDefaultKit(Engine& engine);

} // namespace zd
