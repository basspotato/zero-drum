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
        int ons = 0, offs = 0, sub = 0, drums = 0;
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
                    drums += m.getChannel() == ZeroDrumProcessor::kDrumMidiChannel ? 1 : 0;
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
        CHECK(drums + sub == ons); // no melodic kick in the default beat
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

namespace {

/// Note-ons per channel/note over 4 s of free-running playback.
std::map<std::pair<int, int>, int> countOns(ZeroDrumProcessor& p, int block)
{
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    std::map<std::pair<int, int>, int> ons;
    for (long long pos = 0; pos < 48000LL * 4; pos += block)
    {
        midi.clear();
        p.processBlock(audio, midi);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                ++ons[std::make_pair(meta.getMessage().getChannel(), meta.getMessage().getNoteNumber())];
    }
    return ons;
}

} // namespace

TEST_CASE("Mute and solo: muted parts leave MIDI out and the mix, solo keeps only itself")
{
    Juce juce;
    auto p = makeProcessor(48000.0, 512);
    setParam(*p, "freeRun", 1.0f);
    const auto all = countOns(*p, 512);
    const auto kick = std::make_pair(ZeroDrumProcessor::kDrumMidiChannel, 36);
    const auto snare = std::make_pair(ZeroDrumProcessor::kDrumMidiChannel, 38);
    REQUIRE(all.count(kick) == 1);
    REQUIRE(all.count(snare) == 1);

    p->setLaneMute(0, true);
    CHECK(p->laneMuted(0));
    auto muted = countOns(*p, 512);
    CHECK(muted.count(kick) == 0);
    CHECK(muted.count(snare) == 1);

    p->setLaneSolo(1, true);
    auto solo = countOns(*p, 512);
    CHECK(solo.size() == 1);
    CHECK(solo.count(snare) == 1);
    CHECK(p->audibleMask() == (1u << 1));

    // Both are saved with the session.
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto q = makeProcessor(48000.0, 512);
    q->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(q->laneMuted(0));
    CHECK(q->laneSoloed(1));
    CHECK_FALSE(q->laneSoloed(0));
}

TEST_CASE("Note repeat: the rate menu maps to beat spacings, triplets included")
{
    CHECK(ZeroDrumProcessor::noteRepeatBeats(0) == doctest::Approx(1.0));
    CHECK(ZeroDrumProcessor::noteRepeatBeats(2) == doctest::Approx(0.25));
    CHECK(ZeroDrumProcessor::noteRepeatBeats(3) == doctest::Approx(0.125));
    CHECK(ZeroDrumProcessor::noteRepeatBeats(5) == doctest::Approx(1.0 / 6.0));
    CHECK(ZeroDrumProcessor::noteRepeatBeats(99) == doctest::Approx(1.0 / 12.0)); // clamped
    Juce juce;
    auto p = makeProcessor(48000.0, 256);
    auto* rate = dynamic_cast<juce::AudioParameterChoice*>(p->state().getParameter("repeatRate"));
    REQUIRE(rate != nullptr);
    CHECK(rate->choices.size() == 7);
    CHECK(rate->getIndex() == 2); // 1/16 by default, as before
}
