#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace zd;

namespace {

constexpr double kSr = 48000.0;

std::unique_ptr<Engine> makeEngine()
{
    auto e = std::make_unique<Engine>();
    e->prepare(kSr, 4096);
    loadDefaultKit(*e);
    for (int p = 0; p < kMaxParts; ++p)
        e->part(p).strip = StripParams {};
    return e;
}

/// Renders `total` samples with the given hits and returns the left channel.
std::vector<float> run(Engine& e, std::vector<NoteEvent> hits, int total, int block = 512)
{
    std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
    std::vector<NoteEvent> blockHits;
    blockHits.reserve(hits.size());
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min(block, total - pos);
        blockHits.clear();
        for (const auto& h : hits)
            if (h.offset >= pos && h.offset < pos + n)
                blockHits.push_back(NoteEvent { h.offset - pos, h.part, h.velocity, h.note });
        Transport t;
        t.playing = false;
        e.process(t, blockHits.data(), static_cast<int>(blockHits.size()), l.data() + pos, r.data() + pos, n);
    }
    return l;
}

double rms(const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    return std::sqrt(s / (to - from));
}

} // namespace

TEST_CASE("Choke: a hit on the choking part cuts the other part's tail, but not a hit on the same sample")
{
    auto setup = [](bool choke) {
        auto e = makeEngine();
        e->part(3).fm.decay = 2.0f; // a long ringing perc
        e->part(2).mix.mute = true; // hear only the perc
        if (choke)
            e->part(2).chokes = 1u << 3;
        return e;
    };
    {
        auto e = setup(true);
        const auto y = run(*e, { { 0, 3, 1.0f, -1 }, { 4800, 2, 1.0f, -1 } }, 24000);
        CHECK(rms(y, 1000, 4800) > 1e-3);
        CHECK(rms(y, 4800 + 200, 24000) < 1e-6);
    }
    {
        auto e = setup(false);
        const auto y = run(*e, { { 0, 3, 1.0f, -1 }, { 4800, 2, 1.0f, -1 } }, 24000);
        CHECK(rms(y, 4800 + 200, 24000) > 1e-3);
    }
    {
        // Both on sample 0: the perc is not cut.
        auto e = setup(true);
        const auto y = run(*e, { { 0, 3, 1.0f, -1 }, { 0, 2, 1.0f, -1 } }, 24000);
        CHECK(rms(y, 4800, 24000) > 1e-3);
    }
}

TEST_CASE("Choke: sequenced chokes stay bit-identical for any block size")
{
    auto render = [](int block) {
        auto e = makeEngine();
        e->part(4).chokes = 1u; // 808 cuts kick
        e->part(0).chokes = 1u << 4;
        const int total = static_cast<int>(kSr * 4.0);
        std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
        InternalClock clock(120.0);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min(block, total - pos);
            e->process(clock.next(kSr, n), nullptr, 0, l.data() + pos, r.data() + pos, n);
        }
        return l;
    };
    const auto a = render(32);
    CHECK(test::peak(a) > 0.1);
    CHECK(a == render(4096));
    CHECK(a == render(333));
}

TEST_CASE("Velocity to tone: soft hits are shorter, full hits are unchanged")
{
    auto snare = [](float amount, float velocity) {
        auto e = makeEngine();
        e->part(1).velocityTone = amount;
        return run(*e, { { 0, 1, velocity, -1 } }, 24000);
    };
    // Full velocity: identical whatever the amount.
    CHECK(snare(0.0f, 1.0f) == snare(1.0f, 1.0f));

    // A ghost note: its tail (relative to its own attack) is much lower with the amount on.
    const auto plain = snare(0.0f, 0.3f);
    const auto toned = snare(1.0f, 0.3f);
    const double plainRatio = rms(plain, 4800, 9600) / rms(plain, 0, 960);
    const double tonedRatio = rms(toned, 4800, 9600) / rms(toned, 0, 960);
    MESSAGE("tail/attack: plain " << plainRatio << ", toned " << tonedRatio);
    CHECK(tonedRatio < plainRatio * 0.6);

    // Darker: less energy in the top octave band relative to the whole hit.
    auto hf = [](const std::vector<float>& y) {
        double s = 0.0;
        for (std::size_t i = 1; i < 4800; ++i)
        {
            const double d = static_cast<double>(y[i]) - y[i - 1]; // first difference tilts toward highs
            s += d * d;
        }
        double t = 0.0;
        for (std::size_t i = 0; i < 4800; ++i)
            t += static_cast<double>(y[i]) * y[i];
        return s / t;
    };
    CHECK(hf(toned) < hf(plain));
}
