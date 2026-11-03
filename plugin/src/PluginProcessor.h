#pragma once

#include "zd/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

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

    /// Maps a MIDI note to a part: GM kick/snare/closed hat on 36/38/42, else 36..51 -> parts 0..15.
    static int noteToPart(int note) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParameters() noexcept;
    float value(const char* id) const noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    zd::Engine engine_;
    zd::InternalClock freeRunClock_ { 120.0 };
    std::array<zd::NoteEvent, 256> midiEvents_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZeroDrumProcessor)
};
