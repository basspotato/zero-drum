#pragma once

#include "zd/Engine.h"
#include "zd/export/Export.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// JUCE shell around the framework-free zd::Engine. It owns no DSP: it maps host
/// parameters, MIDI and transport onto the engine and copies audio out.
class ZeroDrumProcessor final : public juce::AudioProcessor
{
public:
    ZeroDrumProcessor();
    ~ZeroDrumProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }

    /// Beat generation, called from the message thread (editor buttons).
    void generateBeat();
    void mutateBeat();
    void addFillToBeat();
    /// Replaces the unlocked lanes with a 4-bar build-up into the drop.
    void makeBuildBeat();
    juce::String describeBeat() const;

    /// Export, called from the message thread. Renders on a background thread with its own
    /// engine copy (current sounds, mix and pattern) and calls `done` on the message thread.
    using ExportCallback = std::function<void(const zd::exporting::ExportResult&)>;
    void exportLoopAsync(const juce::File& directory, ExportCallback done);
    bool isExporting() const noexcept { return exporting_.load(); }

    /// Writes the current pattern as a MIDI clip for drag-and-drop into the DAW: all lanes
    /// (lane = -1) or one. Returns the temp file, or an invalid File if nothing plays.
    juce::File writeMidiClip(int lane) const;
    /// Renders one loop of the mix (through the bus, like playback) to a temp WAV for
    /// drag-and-drop into the DAW. Returns an invalid File on failure.
    /// `part` >= 0 renders just that part (the rest muted), for dragging one sound out as audio.
    juce::File writeLoopWav(int part = -1) const;

    /// Tempo used for exports: the free-run tempo, else the host's last reported tempo.
    double exportBpm() const noexcept;

    // ---- Pads and step grid (message thread) ----
    /// Plays a part once, as soon as the audio thread runs its next block. Lock-free.
    void auditionPart(int part, float velocity) noexcept;
    /// Note repeat: plays `part` on every 16th (synced to the host, or free-running when
    /// stopped) until called with part -1.
    void setNoteRepeat(int part, float velocity) noexcept;
    /// Full level: pads and MIDI notes in play at full velocity (the sequencer keeps its own).
    bool fullLevelPads() const noexcept { return value("padFullLevel") > 0.5f; }
    /// Note repeat spacing in beats for the repeatRate choice: 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T, 1/32T.
    static double noteRepeatBeats(int rate) noexcept;
    std::uint32_t partHits(int part) const noexcept { return engine_.hitCount(part); }
    /// Glue compressor gain reduction right now, in dB, for the Mix tab meter.
    float glueReductionDb() const noexcept { return engine_.glueReductionDb(); }
    /// A copy of the pattern the UI edits, and an edit that is applied, saved and sent to
    /// the audio thread.
    zd::Pattern currentPattern() const;
    void editPattern(const std::function<void(zd::Pattern&)>& edit);
    /// Pattern undo: call pushUndo() once at the start of a gesture (a click, a drag, a button);
    /// the generator buttons and part loads do it themselves. Up to kUndoDepth steps.
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    static constexpr std::size_t kUndoDepth = 64;
    /// Lane tools: a Euclidean rhythm, a new length (polymeters), and humanize (one lane, or
    /// every unlocked lane with onlyLane -1).
    void euclidLane(int lane, int hits);
    void setLaneLength(int lane, int length);
    void humanizeBeat(float amount, int onlyLane = -1);
    /// Pattern slots A..D. The UI edits one slot; with a chain of 2..4, slots A.. play in order
    /// (each for its own length in bars), otherwise the edited slot loops on its own. A slot
    /// opened for the first time starts as a copy of the one being edited.
    void selectSlot(int slot);
    int editSlot() const;
    void setChainCount(int count);
    int chainCount() const;
    /// The slot playing at the playhead, and the playhead in beats from that slot's pass start.
    int playingSlot(double* localBeats = nullptr) const;
    /// Slot tools: copy and paste a whole slot, clear one, and make a variation of the edited
    /// slot in the next one (returns that slot). Pasting or clearing the edited slot is undoable.
    void copySlot(int slot);
    bool pasteSlot(int slot);
    bool hasSlotCopy() const;
    void clearSlot(int slot);
    int makeVariation();
    /// Auto fill: a fill on the last slot of the chain, or every 4th pass of a lone slot.
    void setAutoFill(bool on);
    bool autoFill() const;
    /// A slot's length in bars, as the chain plays it.
    int slotBars(int slot) const;
    /// Regenerates one lane (kick, snare, hat, perc or 808) in the current style; the rest stays.
    void generateLane(int lane);
    /// Lane tools: play the lane twice (its length doubles), or play it backwards.
    void doubleLane(int lane);
    void reverseLane(int lane);
    /// Velocity ramp over the lane's hits: soft to loud (up) or loud to soft.
    void rampLane(int lane, bool up);
    /// Copy a lane, paste it onto any lane of any slot (keeps the target's straight flag off/on as copied).
    void copyLane(int lane);
    bool pasteLane(int lane);
    bool hasLaneCopy() const;
    /// The note a pitched step starts from when it has none: the 808's root, the kick's
    /// tune as a note, middle C (as recorded) for sample pads; -1 for unpitched lanes.
    int defaultNote(int lane) const noexcept;
    /// Puts every sound parameter of the part back to its default.
    void resetPartSound(int part);
    /// Highest output sample level since the last call (for the Mix tab meter); resets it.
    float takeOutputPeak() noexcept { return outputPeak_.exchange(0.0f, std::memory_order_relaxed); }

    /// Part names: a custom name, else the sample's file name, else the default.
    /// Empty resets. Saved with the session and in part files.
    void setPartName(int part, const juce::String& name);
    /// Tap tempo: call on each tap; after two taps sets the free-run tempo. Returns the BPM (0 until then).
    double tapTempo(double nowSeconds);

    /// Lane mute and solo (not automatable; saved with the session). Muted parts drop out of
    /// the mix, the exports and MIDI out; any solo mutes every part that is not soloed.
    void setLaneMute(int part, bool muted);
    void setLaneSolo(int part, bool soloed);
    bool laneMuted(int part) const noexcept { return ((muteMask_.load(std::memory_order_relaxed) >> part) & 1u) != 0; }
    bool laneSoloed(int part) const noexcept { return ((soloMask_.load(std::memory_order_relaxed) >> part) & 1u) != 0; }
    std::uint32_t audibleMask() const noexcept;
    /// Where the audio thread last was, in beats, and whether the transport runs.
    double playheadBeats() const noexcept { return playheadBeats_.load(std::memory_order_relaxed); }
    bool isTransportRunning() const noexcept { return transportRunning_.load(std::memory_order_relaxed); }

    /// Maps a MIDI note to a part: GM kick/snare/closed hat on 36/38/42, part 6 on 37, else 36..51 -> parts 0..15.
    /// Matches zd::exporting::defaultMidiNote, so dragged clips play back on the right pads.
    static int noteToPart(int note) noexcept;

    static constexpr int kDrumMidiChannel = 10; ///< MIDI out for drum parts (General MIDI drums)
    static constexpr int kSubMidiChannel = 2;
    static constexpr int kKickMidiChannel = 3; ///< plays the kick melodically
    static constexpr int kNumStripParts = 5;
    static constexpr int kNumStripSuffixes = 12;
    static constexpr const char* kStripNames[kNumStripParts] { "Kick", "Snare", "Hat", "Perc", "808" };
    static constexpr const char* kStripSuffixes[kNumStripSuffixes] { "Attack", "Sustain", "FilterMode", "Cutoff", "Resonance", "StripDrive",
                                                                      "Crush", "CrushBits", "CrushRate", "CrushFilter", "VelTone", "RevSend" };

    // ---- Pad samples: parts 6..16 (indices 5..15) play user samples (message thread) ----
    static constexpr int kFirstSamplePad = kNumStripParts;
    static constexpr int kNumPadSuffixes = 9;
    static constexpr const char* kPadSuffixes[kNumPadSuffixes] { "SmpPitch", "SmpDecay", "SmpStart", "Level", "Pan", "RevSend", "SmpReverse", "Choke", "Vary" };
    static constexpr int kChokeGroups = 4; ///< pads in the same group cut each other off (open/closed hat style)
    static constexpr double kMaxSampleSeconds = 30.0;
    static bool isSamplePad(int part) noexcept { return part >= kFirstSamplePad && part < zd::kMaxParts; }
    /// Decodes an audio file (WAV, AIFF, FLAC, Ogg, MP3 where supported) to mono and hands it to
    /// the pad. Returns false if the pad takes no samples or the file can't be read.
    bool loadPadSample(int part, const juce::File& file);
    void clearPadSample(int part);
    bool padHasSample(int part) const noexcept;
    /// The pad's display name: the part name, the sample's file name, or "Part N".
    juce::String padName(int part) const;
    bool isAudioFile(const juce::String& path) const;

    // ---- Part and kit files (message thread) ----
    /// A part file (.zdpart) holds one part's sound, mix and lane; a kit file (.zdkit) holds the
    /// whole instrument (every part, the bus and the pattern), the same as a saved session.
    static constexpr const char* kPartExtension = ".zdpart";
    static constexpr const char* kKitExtension = ".zdkit";
    /// Which part a parameter belongs to, or -1 for the bus and generator settings.
    static int partOfParam(const juce::String& id) noexcept;
    juce::ValueTree partToTree(int part) const;
    /// Loads a part file's tree into `part`. Kit parts 1..5 load only into their own slot;
    /// a pad (6..16) loads into any pad. With `withLane`, the lane replaces the pattern's.
    bool partFromTree(int part, const juce::ValueTree& tree, bool withLane);
    bool savePart(int part, const juce::File& file) const;
    bool loadPart(int part, const juce::File& file, bool withLane = true);
    bool saveKit(const juce::File& file);
    bool loadKit(const juce::File& file);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static constexpr int stripSuffixIndex(const char* suffix) noexcept
    {
        for (int k = 0; k < kNumStripSuffixes; ++k)
            if (std::string_view(kStripSuffixes[k]) == suffix)
                return k;
        return 0;
    }
    float stripValue(int part, const char* suffix) const noexcept;
    std::uint32_t lockMask() const noexcept;
    void publishPattern(const zd::Pattern& pattern);
    void setParam(const char* id, float plainValue);
    /// Loads the style's tuned kit into the parameters, skipping parts in `keepMask`.
    void loadStyleKit(zd::gen::Style style, int rootNote, std::uint32_t keepMask);
    void pullParameters() noexcept;
    /// Maps the current parameter values onto any engine (the live one or an export copy).
    void applyParameters(zd::Engine& engine) const noexcept;
    float value(const char* id) const noexcept;
    std::unique_ptr<zd::Chain> chainSnapshot() const;
    /// What plays for the slots as they are, with `edited` in the edited slot. Caller holds patternMutex_.
    void buildChainLocked(zd::Chain& chain, const zd::Pattern& edited) const;
    /// Replaces one slot (undoably when it is the edited one) and sends the result to the engine.
    void setSlotPattern(int slot, const zd::Pattern& pattern);
    std::array<std::string, zd::kMaxParts> partNames() const;

    juce::AudioProcessorValueTreeState apvts_;
    zd::Engine engine_;
    zd::InternalClock freeRunClock_ { 120.0 };
    std::array<zd::NoteEvent, 256> midiEvents_ {};
    std::array<std::array<std::atomic<float>*, kNumStripSuffixes>, kNumStripParts> stripParams_ {};
    // The pattern as the UI and the saved state see it; the engine gets copies via submitPattern.
    mutable std::mutex patternMutex_;
    zd::Pattern uiPattern_;
    std::array<zd::Pattern, zd::kMaxChain> slots_ {}; ///< guarded by patternMutex_; slots_[editSlot_] mirrors uiPattern_
    int editSlot_ = 0;                                ///< guarded by patternMutex_
    int chainCount_ = 1;                              ///< guarded by patternMutex_
    std::uint32_t slotUsed_ = 1u;                     ///< guarded by patternMutex_
    bool autoFill_ = false;                           ///< guarded by patternMutex_
    std::array<juce::String, zd::kMaxParts> customNames_; ///< message thread
    std::array<double, 4> taps_ {};                       ///< message thread
    int numTaps_ = 0;
    std::unique_ptr<zd::Pattern> slotCopy_;           ///< guarded by patternMutex_
    std::unique_ptr<zd::Lane> laneCopy_;              ///< guarded by patternMutex_
    std::atomic<float> outputPeak_ { 0.0f };
    std::atomic<std::uint32_t> muteMask_ { 0 }, soloMask_ { 0 };
    std::vector<zd::Pattern> undo_, redo_; ///< guarded by patternMutex_
    std::uint64_t humanizeCount_ = 0;      ///< guarded by patternMutex_
    std::uint64_t seed_ = 1;

    std::atomic<double> hostBpm_ { 120.0 };

    // Pad auditions: single-producer (message thread), single-consumer (audio thread) ring.
    static constexpr int kAuditionSlots = 64;
    std::array<zd::NoteEvent, kAuditionSlots> auditions_ {};
    std::atomic<int> auditionWrite_ { 0 }, auditionRead_ { 0 };
    std::atomic<int> repeatPart_ { -1 };
    std::atomic<float> repeatVelocity_ { 1.0f };
    double repeatClock_ = 0.0; ///< beats, audio thread only: note repeat while the transport is stopped
    std::atomic<double> playheadBeats_ { 0.0 };
    std::atomic<bool> transportRunning_ { false };
    std::atomic<bool> exporting_ { false };
    juce::ThreadPool exportPool_ { juce::ThreadPoolOptions {}.withThreadName("Zero Drum export").withNumberOfThreads(1) };

    // Pad samples, message thread. The engine gets them through submitSample (lock-free).
    juce::AudioFormatManager formats_;
    std::array<zd::SampleDataPtr, zd::kMaxParts> padSamples_ {};
    std::array<juce::File, zd::kMaxParts> padFiles_ {};
    std::array<std::array<std::atomic<float>*, kNumPadSuffixes>, zd::kMaxParts> padParams_ {};
    void applyPadSamples(zd::Engine& engine) const;
    /// A prepared offline engine with the current sounds, bus, samples and pattern.
    std::shared_ptr<zd::Engine> makeExportEngine() const;

    // MIDI out: the sequencer's hits as notes for the host (audio thread only).
    struct PendingNoteOff
    {
        int samplesLeft = 0;
        int channel = 1;
        int note = 0;
    };
    static constexpr int kMaxPendingOffs = 256;
    std::array<PendingNoteOff, kMaxPendingOffs> pendingOffs_ {};
    int numPendingOffs_ = 0;
    void writeMidiOut(juce::MidiBuffer& midi, int numSamples, bool playing) noexcept;

    // Parameter values by ID, built once so the audio thread never constructs juce::Strings.
    std::vector<std::string> paramIds_;
    std::unordered_map<std::string_view, std::atomic<float>*> paramValues_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumProcessor)
};
