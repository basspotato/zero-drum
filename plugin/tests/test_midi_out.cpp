// Plugin-level tests: the JUCE shell around the engine, run headless (no editor).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "PluginProcessor.h"

#include <map>
#include <memory>

namespace {

struct Juce
{
    juce::ScopedJuceInitialiser_GUI init;
};

std::unique_ptr<ZeroDrumProcessor> makeProcessor(double sr, int block)
{
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->setPlayConfigDetails(0, 2, sr, block);
    p->prepareToPlay(sr, block);
    return p;
}

void setParam(ZeroDrumProcessor& p, const char* id, float plain)
{
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

struct Note
{
    long long on = -1, off = -1;
};

} // namespace

TEST_CASE("MIDI out: the beat plays out as balanced note on/off pairs")
{
    Juce juce;
    constexpr double sr = 48000.0;
    for (int block : { 64, 512, 1500 })
    {
        auto p = makeProcessor(sr, block);
        setParam(*p, "freeRun", 1.0f); // the free-running clock: no host transport needed
        setParam(*p, "midiNoteLength", 100.0f);

        juce::AudioBuffer<float> audio(2, block);
        juce::MidiBuffer midi;
        midi.ensureSize(4096);
        std::map<std::pair<int, int>, long long> held; // (channel, note) -> on time
        int ons = 0, offs = 0, sub = 0;
        long long pos = 0;
        bool overlapped = false, orphanOff = false, tooShort = false;
        for (; pos < static_cast<long long>(sr * 4); pos += block)
        {
            midi.clear();
            p->processBlock(audio, midi);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                const auto key = std::make_pair(m.getChannel(), m.getNoteNumber());
                const long long t = pos + meta.samplePosition;
                if (m.isNoteOn())
                {
                    ++ons;
                    sub += m.getChannel() == ZeroDrumProcessor::kSubMidiChannel ? 1 : 0;
                    overlapped = overlapped || held.count(key) != 0;
                    held[key] = t;
                }
                else if (m.isNoteOff())
                {
                    ++offs;
                    auto it = held.find(key);
                    if (it == held.end())
                        orphanOff = true;
                    else
                    {
                        tooShort = tooShort || t - it->second <= 0;
                        held.erase(it);
                    }
                }
            }
        }
        // Switching MIDI out off ends every held note at once.
        setParam(*p, "midiOut", 0.0f);
        midi.clear();
        p->processBlock(audio, midi);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOff())
            {
                ++offs;
                held.erase(std::make_pair(meta.getMessage().getChannel(), meta.getMessage().getNoteNumber()));
            }
            else if (meta.getMessage().isNoteOn())
                ++ons; // must not happen
        MESSAGE("block " << block << ": " << ons << " notes, " << sub << " on the 808 channel");
        CHECK(ons > 16);
        CHECK(sub > 0);
        CHECK(ons == offs);
        CHECK(held.empty());
        CHECK_FALSE(overlapped);
        CHECK_FALSE(orphanOff);
        CHECK_FALSE(tooShort);
    }
}

TEST_CASE("MIDI out: off means no notes, and incoming MIDI is not echoed")
{
    Juce juce;
    auto p = makeProcessor(48000.0, 512);
    setParam(*p, "freeRun", 1.0f);
    setParam(*p, "midiOut", 0.0f);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    int out = 0;
    for (int b = 0; b < 200; ++b)
    {
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
        p->processBlock(audio, midi);
        out += midi.getNumEvents();
    }
    CHECK(out == 0);
}

int main(int argc, char** argv)
{
    doctest::Context context(argc, argv);
    return context.run();
}
