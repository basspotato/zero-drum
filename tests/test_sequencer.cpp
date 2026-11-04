#include <doctest/doctest.h>

#include "zd/Sequencer.h"
#include "zd/PatternIO.h"
#include "zd/export/Export.h"

#include <algorithm>
#include <array>
#include <vector>

using namespace zd;

namespace {

/// Runs the sequencer over `totalSamples` in blocks and returns absolute trigger positions per lane.
std::vector<std::pair<long long, int>> collectAll(const Sequencer& seq, double sr, double bpm,
                                                  int totalSamples, int blockSize)
{
    std::vector<std::pair<long long, int>> hits;
    std::array<NoteEvent, kMaxEventsPerBlock> events {};
    for (long long pos = 0; pos < totalSamples; pos += blockSize)
    {
        const int n = static_cast<int>(std::min<long long>(blockSize, totalSamples - pos));
        Transport t { true, bpm, static_cast<double>(pos) * bpm / (60.0 * sr) };
        const int count = seq.collect(t, sr, n, events.data(), kMaxEventsPerBlock);
        for (int i = 0; i < count; ++i)
            hits.emplace_back(pos + events[static_cast<std::size_t>(i)].offset, events[static_cast<std::size_t>(i)].part);
    }
    std::sort(hits.begin(), hits.end());
    return hits;
}

} // namespace

TEST_CASE("Sequencer: 16th notes land on exact samples at 120 BPM / 48 kHz")
{
    Sequencer seq;
    seq.pattern() = Pattern {};
    for (int s = 0; s < 16; s += 4)
        seq.pattern().lanes[0].steps[static_cast<std::size_t>(s)].on = true;

    // One beat = 24000 samples; one 16th = 6000.
    const auto hits = collectAll(seq, 48000.0, 120.0, 96000, 512);
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].first == 0);
    CHECK(hits[1].first == 24000);
    CHECK(hits[2].first == 48000);
    CHECK(hits[3].first == 72000);
}

TEST_CASE("Sequencer: triggers do not depend on block size")
{
    Sequencer seq;
    loadDefaultPattern(seq.pattern());
    seq.pattern().swing = 0.62;
    seq.pattern().lanes[2].steps[3].microOffset = -0.21f;
    const auto a = collectAll(seq, 44100.0, 93.0, 44100 * 8, 32);
    const auto b = collectAll(seq, 44100.0, 93.0, 44100 * 8, 2048);
    const auto c = collectAll(seq, 44100.0, 93.0, 44100 * 8, 441);
    CHECK(a == b);
    CHECK(a == c);
    CHECK(a.size() > 20);
}

TEST_CASE("Sequencer: swing delays odd steps only")
{
    Sequencer seq;
    seq.pattern() = Pattern {};
    seq.pattern().swing = 0.75;
    seq.pattern().lanes[0].steps[0].on = true;
    seq.pattern().lanes[0].steps[1].on = true;
    seq.pattern().lanes[0].steps[2].on = true;
    seq.pattern().lanes[0].length = 4;

    const auto hits = collectAll(seq, 48000.0, 120.0, 24000, 256);
    REQUIRE(hits.size() == 3);
    CHECK(hits[0].first == 0);
    CHECK(hits[1].first == 6000 + 3000); // 75% swing: half a 16th late
    CHECK(hits[2].first == 12000);
}

TEST_CASE("Sequencer: independent lane lengths give a polymeter")
{
    Sequencer seq;
    seq.pattern() = Pattern {};
    seq.pattern().lanes[0].length = 3;
    seq.pattern().lanes[0].steps[0].on = true;

    const auto hits = collectAll(seq, 48000.0, 120.0, 96000, 1000); // 16 steps
    REQUIRE(hits.size() == 6); // steps 0, 3, 6, 9, 12, 15
    CHECK(hits[1].first == 3 * 6000);
}

TEST_CASE("Sequencer: probability is seeded and repeatable")
{
    Sequencer seq;
    seq.pattern() = Pattern {};
    for (auto& s : seq.pattern().lanes[0].steps)
        s = { true, 1.0f, 0.5f, 0.0f };
    seq.setSeed(1234);
    const auto a = collectAll(seq, 48000.0, 120.0, 48000 * 8, 256);
    const auto b = collectAll(seq, 48000.0, 120.0, 48000 * 8, 777);
    CHECK(a == b);
    // 64 steps at 50%: expect roughly half.
    CHECK(a.size() > 16);
    CHECK(a.size() < 48);

    seq.setSeed(999);
    const auto c = collectAll(seq, 48000.0, 120.0, 48000 * 8, 256);
    CHECK(a != c);
}

TEST_CASE("Sequencer: stopped transport emits nothing")
{
    Sequencer seq;
    loadDefaultPattern(seq.pattern());
    std::array<NoteEvent, 16> events {};
    Transport t { false, 120.0, 0.0 };
    CHECK(seq.collect(t, 48000.0, 512, events.data(), 16) == 0);
}

TEST_CASE("Chain: slots play one after another, each from its first step, at any block size")
{
    // A = 1 bar with a kick on every beat, B = 2 bars with a single snare at its start.
    Chain chain;
    chain.count = 2;
    Pattern& a = chain.slots[0];
    a = Pattern {};
    for (int s : { 0, 4, 8, 12 })
        a.lanes[0].steps[static_cast<std::size_t>(s)] = Step { true, 1.0f, 1.0f, 0.0f, -1, 1 };
    Pattern& b = chain.slots[1];
    b = Pattern {};
    b.lanes[1].length = 32;
    b.lanes[1].steps[0] = Step { true, 1.0f, 1.0f, 0.0f, -1, 1 };
    CHECK(patternBars(a) == 1);
    CHECK(patternBars(b) == 2);

    auto run = [&](int block) {
        Sequencer seq;
        seq.setChain(chain);
        std::vector<std::pair<long long, int>> hits;
        std::array<NoteEvent, 256> ev {};
        const double sr = 48000.0;
        for (long long pos = 0; pos < static_cast<long long>(sr * 12); pos += block)
        {
            Transport t { true, 120.0, static_cast<double>(pos) * 120.0 / (60.0 * sr) };
            const int n = seq.collect(t, sr, block, ev.data(), static_cast<int>(ev.size()));
            for (int i = 0; i < n; ++i)
            {
                const long long at = pos + ev[static_cast<std::size_t>(i)].offset;
                if (at < static_cast<long long>(sr * 12)) // the last block can run past the window
                    hits.emplace_back(at, ev[static_cast<std::size_t>(i)].part);
            }
        }
        std::sort(hits.begin(), hits.end());
        return hits;
    };
    const auto ref = run(64);
    // At 120 BPM a bar is 2 s = 96000 samples; the chain is 3 bars = 6 s, so 12 s is two cycles.
    std::vector<std::pair<long long, int>> expected;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const long long base = cycle * 288000LL;
        for (int k = 0; k < 4; ++k)
            expected.emplace_back(base + k * 24000LL, 0);
        expected.emplace_back(base + 96000LL, 1);
    }
    CHECK(ref == expected);
    for (int block : { 1, 100, 512, 4096, 9999 })
        CHECK(run(block) == ref);
    CHECK(&Sequencer {}.patternAt(1.0) != nullptr);
}

TEST_CASE("Chain: probability draws afresh on every pass, the same at any block size")
{
    Chain chain;
    chain.count = 2;
    for (auto& slot : { 0, 1 })
    {
        Pattern& p = chain.slots[static_cast<std::size_t>(slot)];
        p = Pattern {};
        for (int s = 0; s < 16; ++s)
            p.lanes[2].steps[static_cast<std::size_t>(s)] = Step { true, 1.0f, 0.5f, 0.0f, -1, 1 };
    }
    auto run = [&](int block) {
        Sequencer seq;
        seq.setChain(chain);
        std::vector<long long> hits;
        std::array<NoteEvent, 256> ev {};
        const double sr = 48000.0;
        const long long total = 4LL * 96000; // four 1-bar passes at 120 BPM
        for (long long pos = 0; pos < total; pos += block)
        {
            Transport t { true, 120.0, static_cast<double>(pos) * 120.0 / (60.0 * sr) };
            const int n = seq.collect(t, sr, block, ev.data(), static_cast<int>(ev.size()));
            for (int i = 0; i < n; ++i)
                if (pos + ev[static_cast<std::size_t>(i)].offset < total)
                    hits.push_back(pos + ev[static_cast<std::size_t>(i)].offset);
        }
        std::sort(hits.begin(), hits.end());
        return hits;
    };
    const auto ref = run(512);
    // Which of the 16 steps played, per pass.
    std::array<std::uint32_t, 4> masks {};
    for (long long h : ref)
        masks[static_cast<std::size_t>(h / 96000)] |= 1u << static_cast<int>((h % 96000) / 6000);
    CHECK(masks[0] != masks[1]);
    CHECK(masks[0] != masks[2]); // same slot, next cycle: a new draw too
    CHECK(masks[1] != masks[3]);
    for (int block : { 1, 333, 4096 })
        CHECK(run(block) == ref);

    // The exported MIDI makes the same draws as playback.
    Sequencer seq;
    seq.setChain(chain);
    const auto notes = exporting::realizeLane(seq, 2, 0, 64, 42);
    REQUIRE(notes.size() == ref.size());
    for (std::size_t i = 0; i < notes.size(); ++i)
        CHECK(notes[i].tick == ref[i] / 6000 * (exporting::kMidiPpq / 4));
}

TEST_CASE("Straight lanes ignore the swing and survive a save")
{
    Pattern p {};
    p.swing = 0.66;
    p.lanes[0].steps[1].on = true;
    p.lanes[2].steps[1].on = true;
    p.lanes[0].straight = true;
    CHECK(Sequencer::stepTimeIn(p, 0, 1) == doctest::Approx(0.25));
    CHECK(Sequencer::stepTimeIn(p, 2, 1) > 0.3);
    Pattern back {};
    REQUIRE(deserializePattern(serializePattern(p), back));
    CHECK(back.lanes[0].straight);
    CHECK_FALSE(back.lanes[2].straight);
}
