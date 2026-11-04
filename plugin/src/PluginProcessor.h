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

    /// Tempo used for exports: the free-run tempo, else the host's last reported tempo.
    double exportBpm() const noexcept;

    // ---- Pads and step grid (message thread) ----
    /// Plays a part once, as soon as the audio thread runs its next block. Lock-free.
    void auditionPart(int part, float velocity) noexcept;
    /// Note repeat: plays `part` on every 16th (synced to the host, or free-running when
    /// stopped) until called with part -1.
    void setNoteRepeat(int part, float velocity) noexcept;
    std::uint32_t partHits(int part) const noexcept { return engine_.hitCount(part); }
    /// A copy of the pattern the UI edits, and an edit that is applied, saved and sent to
    /// the audio thread.
    zd::Pattern currentPattern() const;
    void editPattern(const std::function<void(zd::Pattern&)>& edit);
    /// Where the audio thread last was, in beats, and whether the transport runs.
    double playheadBeats() const noexcept { return playheadBeats_.load(std::memory_order_relaxed); }
    bool isTransportRunning() const noexcept { return transportRunning_.load(std::memory_order_relaxed); }

    /// Maps a MIDI note to a part: GM kick/snare/closed hat on 36/38/42, part 6 on 37, else 36..51 -> parts 0..15.
    /// Matches zd::exporting::defaultMidiNote, so dragged clips play back on the right pads.
    static int noteToPart(int note) noexcept;

    static constexpr int kSubMidiChannel = 2;
    static constexpr int kKickMidiChannel = 3; ///< plays the kick melodically
    static constexpr int kNumStripParts = 5;
    static constexpr int kNumStripSuffixes = 12;
    static constexpr const char* kStripNames[kNumStripParts] { "Kick", "Snare", "Hat", "Perc", "808" };
    static constexpr const char* kStripSuffixes[kNumStripSuffixes] { "Attack", "Sustain", "FilterMode", "Cutoff", "Resonance", "StripDrive",
                                                                      "Crush", "CrushBits", "CrushRate", "CrushFilter", "VelTone", "RevSend" };

    // ---- Pad samples: parts 6..16 (indices 5..15) play user samples (message thread) ----
    static constexpr int kFirstSamplePad = kNumStripParts;
    static constexpr int kNumPadSuffixes = 6;
    static constexpr const char* kPadSuffixes[kNumPadSuffixes] { "SmpPitch", "SmpDecay", "SmpStart", "Level", "Pan", "RevSend" };
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
    zd::Pattern patternSnapshot() const;
    std::array<std::string, zd::kMaxParts> partNames() const;

    juce::AudioProcessorValueTreeState apvts_;
    zd::Engine engine_;
    zd::InternalClock freeRunClock_ { 120.0 };
    std::array<zd::NoteEvent, 256> midiEvents_ {};
    std::array<std::array<std::atomic<float>*, kNumStripSuffixes>, kNumStripParts> stripParams_ {};
    // The pattern as the UI and the saved state see it; the engine gets copies via submitPattern.
    mutable std::mutex patternMutex_;
    zd::Pattern uiPattern_;
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
