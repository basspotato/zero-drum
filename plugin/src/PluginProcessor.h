#pragma once

#include "zd/Engine.h"
#include "zd/gen/Generator.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
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
    bool producesMidi() const override { return false; }
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
    juce::String describeBeat() const;

    /// Maps a MIDI note to a part: GM kick/snare/closed hat on 36/38/42, else 36..51 -> parts 0..15.
    static int noteToPart(int note) noexcept;

    static constexpr int kSubMidiChannel = 2;
    static constexpr int kNumStripParts = 5;
    static constexpr int kNumStripSuffixes = 6;
    static constexpr const char* kStripNames[kNumStripParts] { "Kick", "Snare", "Hat", "Perc", "808" };
    static constexpr const char* kStripSuffixes[kNumStripSuffixes] { "Attack", "Sustain", "FilterMode", "Cutoff", "Resonance", "StripDrive" };

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
    void pullParameters() noexcept;
    float value(const char* id) const noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    zd::Engine engine_;
    zd::InternalClock freeRunClock_ { 120.0 };
    std::array<zd::NoteEvent, 256> midiEvents_ {};
    std::array<std::array<std::atomic<float>*, kNumStripSuffixes>, kNumStripParts> stripParams_ {};
    // The pattern as the UI and the saved state see it; the engine gets copies via submitPattern.
    mutable std::mutex patternMutex_;
    zd::Pattern uiPattern_;
    std::uint64_t seed_ = 1;

    // Parameter values by ID, built once so the audio thread never constructs juce::Strings.
    std::vector<std::string> paramIds_;
    std::unordered_map<std::string_view, std::atomic<float>*> paramValues_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumProcessor)
};
