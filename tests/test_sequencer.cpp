#include <doctest/doctest.h>

#include "zd/Sequencer.h"

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
