#pragma once

#include "zd/Sequencer.h"
#include "zd/Strip.h"
#include "zd/Types.h"
#include "zd/engines/AnalogKick.h"
#include "zd/engines/AnalogSnare.h"
#include "zd/engines/FmPerc.h"
#include "zd/engines/SamplePlayer.h"
#include "zd/engines/Sub808.h"
#include "zd/engines/Clap.h"
#include "zd/dsp/Clipper.h"

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
    /// Sidechain: how far each kick (part 0) ducks this part, 0 = not at all, 1 = to silence.
    /// A 3 ms raised-cosine attack keeps sustained sounds (808s) click-free; then it recovers over duckRelease.
    float duck = 0.0f;
    float duckRelease = 0.12f; ///< seconds, time constant of the recovery
};

/// The part whose hits drive the sidechain.
inline constexpr int kDuckSourcePart = 0;

/// One of the 16 drum parts: an engine type, its parameters, its voices and its mix.
class Part
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    /// `note` is a MIDI note for pitched engines (808); -1 uses the part's own tune.
    void trigger(float velocity, int note = -1) noexcept;
    void render(float* left, float* right, int numSamples) noexcept;
    bool isActive() const noexcept;
    /// Cuts every sounding voice with the short kill fade, unless this part was triggered
    /// on this very sample.
    void choke() noexcept;
    void reset() noexcept;
    /// Starts a sidechain duck now (sample-accurate: the engine calls it at the kick's offset).
    void duckTrigger() noexcept
    {
        duckAttacking_ = true;
        duckStart_ = duckLevel_;
        duckPhase_ = 0.0;
    }

    EngineType type = EngineType::Off;
    KickParams kick;
    SnareParams snare;
    FmParams fm;
    Sub808Params sub;
    SampleParams sampleParams;
    ClapParams clap;
    /// Second layer: every hit also plays this engine (with its own params above), e.g. a
    /// clap stacked on the snare. Off = no layer. Layering the part's own type is ignored.
    EngineType layer = EngineType::Off;
    float layerGainDb = -4.0f;
    /// Velocity to tone, 0..1: softer hits get a shorter decay and a darker tone, so ghost
    /// notes sound like ghost notes rather than quiet copies. At 1, velocity 0.3 gives
    /// about -40% decay and a darker body; full velocity is unchanged. The 808 ignores it.
    float velocityTone = 0.0f;
    /// Choke: bit i set = a hit on this part cuts part i (e.g. closed hat cuts open hat,
    /// 808 cuts the kick's tail). Hits on the same sample never cut each other.
    std::uint32_t chokes = 0;
    SampleDataPtr sample; ///< set only while audio is stopped (see Engine::setSample)
    StripParams strip;
    PartMix mix;

private:
    Voice& voice(int i) noexcept { return voiceOf(type, i); }
    Voice& voiceOf(EngineType t, int i) noexcept;
    int allocateVoice(EngineType t) noexcept;
    void triggerEngine(EngineType t, float velocity, float toneDrop, int note, bool glideFromPrevious) noexcept;
    template <typename F> void forEachVoice(F&& f) noexcept;

    std::array<AnalogKickVoice, kVoicesPerPart> kickVoices_;
    std::array<AnalogSnareVoice, kVoicesPerPart> snareVoices_;
    std::array<SamplePlayerVoice, kVoicesPerPart> sampleVoices_;
    std::array<FmPercVoice, kVoicesPerPart> fmVoices_;
    std::array<Sub808Voice, kVoicesPerPart> subVoices_;
    std::array<ClapVoice, kVoicesPerPart> clapVoices_;
    Strip strip_;
    std::vector<float> scratch_;
    unsigned ageCounter_ = 0;
    std::int64_t samplesSinceTrigger_ = 0;
    double lastSubHz_ = 0.0;

    /// Advances the duck envelope one sample and returns the gain to apply.
    float duckGain() noexcept;
    double sampleRate_ = 48000.0;
    double duckLevel_ = 0.0;   ///< 0 = no ducking, 1 = full depth
    double duckAttackStep_ = 0.01;
    double duckStart_ = 0.0;
    double duckPhase_ = 0.0;
    double duckReleaseCoef_ = 0.0;
    float cachedDuckRelease_ = -1.0f;
    bool duckAttacking_ = false;
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
    /// kDefaultMasterDb to leave headroom into the master clipper.
    void setMasterGainDb(float db) noexcept;
    static constexpr float kDefaultMasterDb = -2.0f;

    /// The master stage: the oversampled clipper (or, when the clipper is off, a matching
    /// delay), after the master gain. Off by default, so the engine has no latency; once
    /// enabled, output is delayed by masterLatency() samples whether or not it clips.
    /// Call from the thread that calls process() or while it is not running.
    void setMasterStage(bool enabled) noexcept;
    void setMasterClip(bool clipOn, float driveDb, float ceilingDb) noexcept;
    struct MasterClipSettings
    {
        bool clipOn = true;
        float driveDb = 0.0f;
        float ceilingDb = -1.0f;
    };
    const MasterClipSettings& masterClip() const noexcept { return masterClip_; }
    int masterLatency() const noexcept { return masterStage_ ? dsp::MasterClipper::kLatency : 0; }

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
    bool masterStage_ = false;
    dsp::MasterClipper clipper_;
    MasterClipSettings masterClip_ {};
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
