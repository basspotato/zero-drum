#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/PatternIO.h"
#include "zd/gen/Generator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace zd;

namespace {

constexpr double kSr = 48000.0;

/// A steady 60 Hz + 3 kHz pair fed through the sweep stage by an always-on sample part.
std::unique_ptr<Engine> toneEngine(int sweepSteps)
{
    auto e = std::make_unique<Engine>();
    e->prepare(kSr, 4096);
    auto tone = std::make_shared<SampleData>();
    tone->sampleRate = kSr;
    tone->samples.resize(static_cast<std::size_t>(kSr * 20));
    for (std::size_t i = 0; i < tone->samples.size(); ++i)
        tone->samples[i] = static_cast<float>(0.3 * std::sin(dsp::kTwoPi * 60.0 * static_cast<double>(i) / kSr)
                                            + 0.1 * std::sin(dsp::kTwoPi * 3000.0 * static_cast<double>(i) / kSr));
    e->setSample(5, tone);
    e->part(5).type = EngineType::Sample;
    e->part(5).mix.gainDb = 0.0f;
    auto& pat = e->sequencer().pattern();
    pat = Pattern {};
    pat.lanes[5].length = 64;
    pat.lanes[5].steps[0] = Step { true, 1.0f, 1.0f, 0.0f, -1, 1 };
    pat.sweep = Sweep { sweepSteps, 30.0f, 1500.0f, 0.5f };
    return e;
}

std::vector<float> render(Engine& e, int total, int block)
{
    std::vector<float> out(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(block));
    InternalClock clock(120.0);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min(block, total - pos);
        e.process(clock.next(kSr, n), nullptr, 0, out.data() + pos, r.data(), n);
    }
    return out;
}

double level(const std::vector<float>& y, double hz, int from, int length)
{
    std::vector<double> d(y.begin() + from, y.begin() + from + length);
    return test::goertzel(d, hz, kSr);
}

} // namespace

TEST_CASE("Sweep: the high-pass rises over the build and lets the highs through")
{
    // 64 steps of 16ths at 120 BPM = 8 s.
    auto e = toneEngine(64);
    const auto y = render(*e, static_cast<int>(kSr * 8), 512);
    const int win = 9600; // 200 ms
    const double lowStart = level(y, 60.0, 4800, win);
    const double lowEnd = level(y, 60.0, static_cast<int>(kSr * 8) - win - 100, win);
    const double highStart = level(y, 3000.0, 4800, win);
    const double highEnd = level(y, 3000.0, static_cast<int>(kSr * 8) - win - 100, win);
    MESSAGE("60 Hz: " << test::toDb(lowStart) << " -> " << test::toDb(lowEnd) << " dB; 3 kHz: " << test::toDb(highStart)
                      << " -> " << test::toDb(highEnd) << " dB");
    CHECK(test::toDb(lowEnd) < test::toDb(lowStart) - 30.0); // the low end is gone by the drop
    CHECK(std::abs(test::toDb(highEnd) - test::toDb(highStart)) < 3.0); // the highs stay
}

TEST_CASE("Sweep: bit-identical for any block size, and off leaves the bus untouched")
{
    auto ref = toneEngine(32);
    const auto a = render(*ref, 60000, 1);
    for (int block : { 7, 100, 512, 4096 })
    {
        auto e = toneEngine(32);
        CHECK(render(*e, 60000, block) == a);
    }
    // steps 0: identical to an engine that never heard of sweeps (no filter in the path).
    auto off = toneEngine(0);
    auto plain = toneEngine(0);
    plain->sequencer().pattern().sweep = Sweep {};
    CHECK(render(*off, 30000, 333) == render(*plain, 30000, 333));
}

TEST_CASE("Sweep: Build sets it, a fresh beat clears it, and it survives save and load")
{
    Pattern p;
    gen::makeBuild(p, 4, 0);
    CHECK(p.sweep.steps == 64);
    CHECK(p.sweep.endHz > p.sweep.startHz);

    Pattern loaded;
    REQUIRE(deserializePattern(serializePattern(p), loaded));
    CHECK(loaded.sweep.steps == p.sweep.steps);
    CHECK(loaded.sweep.startHz == doctest::Approx(p.sweep.startHz));
    CHECK(loaded.sweep.endHz == doctest::Approx(p.sweep.endHz));
    CHECK(loaded.sweep.resonance == doctest::Approx(p.sweep.resonance));

    gen::GenerateParams g;
    gen::generate(g, p);
    CHECK(p.sweep.steps == 0);
    Pattern again;
    REQUIRE(deserializePattern(serializePattern(p), again));
    CHECK(again.sweep.steps == 0);
}

TEST_CASE("MIDI out: the engine reports the sequencer's hits, the same for any block size")
{
    auto collect = [](int block) {
        auto e = std::make_unique<Engine>();
        e->prepare(kSr, 256); // larger blocks are chunked inside process()
        loadDefaultKit(*e);
        loadDefaultPattern(e->sequencer().pattern());
        InternalClock clock(120.0);
        std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
        std::vector<std::pair<long long, int>> hits;
        long long pos = 0;
        const NoteEvent pad { 0, 9, 1.0f, -1 }; // external hits are not reported
        for (int b = 0; pos < static_cast<long long>(kSr * 4); ++b)
        {
            e->process(clock.next(kSr, block), &pad, b == 0 ? 1 : 0, l.data(), r.data(), block);
            int last = -1;
            for (int i = 0; i < e->numSequencedEvents(); ++i)
            {
                const auto& ev = e->sequencedEvents()[i];
                CHECK(ev.offset >= last); // sorted
                CHECK(ev.offset < block);
                CHECK(ev.part != 9);
                last = ev.offset;
                if (pos + ev.offset < static_cast<long long>(kSr * 4))
                    hits.emplace_back(pos + ev.offset, ev.part);
            }
            pos += block;
        }
        return hits;
    };
    const auto a = collect(64);
    CHECK(a.size() > 20);
    for (int block : { 100, 256, 1000, 4096 })
    {
        auto b = collect(block);
        auto sa = a;
        std::sort(sa.begin(), sa.end());
        std::sort(b.begin(), b.end());
        CHECK(b == sa);
    }
}
