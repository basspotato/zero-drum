#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"

#include <vector>

using namespace zd;

namespace {

struct Render
{
    std::vector<float> left, right;
};

Render renderEngine(Engine& engine, double sr, double bpm, int totalSamples, const std::vector<int>& blockSizes)
{
    Render r;
    r.left.assign(static_cast<std::size_t>(totalSamples), 0.0f);
    r.right.assign(static_cast<std::size_t>(totalSamples), 0.0f);
    InternalClock clock(bpm);
    int pos = 0, b = 0;
    while (pos < totalSamples)
    {
        const int n = std::min(blockSizes[static_cast<std::size_t>(b++ % static_cast<int>(blockSizes.size()))], totalSamples - pos);
        const Transport t = clock.next(sr, n);
        engine.process(t, nullptr, 0, r.left.data() + pos, r.right.data() + pos, n);
        pos += n;
    }
    return r;
}

std::unique_ptr<Engine> makeEngine(double sr, int maxBlock)
{
    auto e = std::make_unique<Engine>();
    e->prepare(sr, maxBlock);
    loadDefaultKit(*e);
    return e;
}

} // namespace

TEST_CASE("Null test: full engine output is bit-identical for any block size")
{
    constexpr double sr = 48000.0;
    const int total = static_cast<int>(sr * 4.0); // two bars at 120 BPM

    auto a = makeEngine(sr, 4096);
    auto b = makeEngine(sr, 4096);
    auto c = makeEngine(sr, 4096);
    const auto ra = renderEngine(*a, sr, 120.0, total, { 32 });
    const auto rb = renderEngine(*b, sr, 120.0, total, { 2048 });
    const auto rc = renderEngine(*c, sr, 120.0, total, { 17, 512, 1, 333, 4096 });

    CHECK(zd::test::peak(ra.left) > 0.1);
    CHECK(ra.left == rb.left);
    CHECK(ra.right == rb.right);
    CHECK(ra.left == rc.left);
    CHECK(ra.right == rc.right);
}

TEST_CASE("Determinism: same seed renders the same beat, different seed differs")
{
    constexpr double sr = 44100.0;
    const int total = static_cast<int>(sr * 8.0);
    auto a = makeEngine(sr, 512);
    auto b = makeEngine(sr, 512);
    auto c = makeEngine(sr, 512);
    a->sequencer().setSeed(5);
    b->sequencer().setSeed(5);
    c->sequencer().setSeed(6);
    const auto ra = renderEngine(*a, sr, 128.0, total, { 512 });
    const auto rb = renderEngine(*b, sr, 128.0, total, { 512 });
    const auto rc = renderEngine(*c, sr, 128.0, total, { 512 });
    CHECK(ra.left == rb.left);
    CHECK(ra.left != rc.left); // the default pattern has probability steps
}

TEST_CASE("Engine: oversized host blocks are handled in chunks")
{
    constexpr double sr = 48000.0;
    auto a = makeEngine(sr, 256);
    auto b = makeEngine(sr, 8192);
    const auto ra = renderEngine(*a, sr, 120.0, 48000, { 8192 });
    const auto rb = renderEngine(*b, sr, 120.0, 48000, { 8192 });
    CHECK(ra.left == rb.left);
}

TEST_CASE("Engine: external MIDI triggers land on their sample offset")
{
    constexpr double sr = 48000.0;
    auto e = makeEngine(sr, 1024);
    std::vector<float> l(1024), r(1024);
    const NoteEvent ev { 700, 0, 1.0f };
    e->process(Transport { false, 120.0, 0.0 }, &ev, 1, l.data(), r.data(), 1024);
    for (int i = 0; i < 700; ++i)
        REQUIRE(l[static_cast<std::size_t>(i)] == 0.0f);
    double energy = 0.0;
    for (int i = 700; i < 1024; ++i)
        energy += std::abs(l[static_cast<std::size_t>(i)]);
    CHECK(energy > 0.0);
}

TEST_CASE("Engine: output stays finite and below clipping with the default kit")
{
    constexpr double sr = 96000.0;
    auto e = makeEngine(sr, 512);
    const auto r = renderEngine(*e, sr, 174.0, static_cast<int>(sr * 6), { 512 });
    CHECK(zd::test::allFinite(r.left));
    CHECK(zd::test::allFinite(r.right));
    CHECK(zd::test::peak(r.left) < 2.0);
}

TEST_CASE("Engine: mono parts choke the previous hit")
{
    // A: two long kicks a block apart. B: only the second kick. Once the first kick's
    // 2 ms fade is over, both engines must output exactly the same samples.
    constexpr double sr = 48000.0;
    auto a = makeEngine(sr, 1024);
    auto b = makeEngine(sr, 1024);
    a->part(0).kick.decay = 5.0f;
    b->part(0).kick.decay = 5.0f;
    std::vector<float> la(1024), ra(1024), lb(1024), rb(1024);
    const NoteEvent ev { 0, 0, 1.0f };

    a->process(Transport {}, &ev, 1, la.data(), ra.data(), 1024);
    b->process(Transport {}, nullptr, 0, lb.data(), rb.data(), 1024);
    a->process(Transport {}, &ev, 1, la.data(), ra.data(), 1024);
    b->process(Transport {}, &ev, 1, lb.data(), rb.data(), 1024);
    CHECK(la != lb); // still fading during this block
    a->process(Transport {}, nullptr, 0, la.data(), ra.data(), 1024);
    b->process(Transport {}, nullptr, 0, lb.data(), rb.data(), 1024);
    CHECK(la == lb);
}
