#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/dsp/Reverb.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace zd;

namespace {

constexpr double kSr = 48000.0;

struct Wet
{
    std::vector<float> l, r;
};

Wet run(dsp::Reverb& rv, const std::vector<float>& in, int block = 256)
{
    Wet w { std::vector<float>(in.size()), std::vector<float>(in.size()) };
    const int n = static_cast<int>(in.size());
    for (int pos = 0; pos < n; pos += block)
        rv.process(in.data() + pos, w.l.data() + pos, w.r.data() + pos, std::min(block, n - pos));
    return w;
}

double energy(const std::vector<float>& x, int from, int to)
{
    double e = 0.0;
    for (int i = from; i < to; ++i)
        e += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    return e / (to - from);
}

} // namespace

TEST_CASE("Reverb: decays at the set RT60, stereo, and stays finite")
{
    for (float rt : { 0.6f, 1.5f })
    {
        dsp::Reverb rv;
        rv.prepare(kSr);
        dsp::ReverbSettings s;
        s.decay = rt;
        s.highPassHz = 20.0f;
        s.dampingHz = 20000.0f;
        rv.set(s);
        std::vector<float> in(static_cast<std::size_t>(kSr * 4.0), 0.0f);
        in[0] = 1.0f;
        const auto w = run(rv, in);
        // Energy decay between two 50 ms windows 0.3 s apart, extrapolated to 60 dB.
        const int a = static_cast<int>(0.15 * kSr), b = static_cast<int>(0.45 * kSr), win = static_cast<int>(0.05 * kSr);
        const double drop = 10.0 * std::log10(energy(w.l, a, a + win) / energy(w.l, b, b + win));
        const double measured = 60.0 / drop * 0.3;
        MESSAGE("RT60 set " << rt << " s, measured " << measured << " s");
        CHECK(measured == doctest::Approx(rt).epsilon(0.3));
        CHECK(w.l != w.r);
        for (float v : w.l)
            CHECK(std::isfinite(v));
    }
}

TEST_CASE("Reverb: the return high-pass keeps the low end dry")
{
    auto wetOf = [](double hz) {
        dsp::Reverb rv;
        rv.prepare(kSr);
        dsp::ReverbSettings s;
        s.highPassHz = 500.0f;
        rv.set(s);
        std::vector<float> in(static_cast<std::size_t>(kSr), 0.0f);
        for (std::size_t i = 0; i < 4800; ++i)
            in[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.141592653589793 * hz * static_cast<double>(i) / kSr));
        const auto w = run(rv, in);
        return energy(w.l, 0, static_cast<int>(kSr));
    };
    CHECK(wetOf(50.0) < wetOf(2000.0) * 0.05);
}

TEST_CASE("Reverb in the engine: sends are post-fader, muted parts send nothing, any block size")
{
    auto render = [](int block, float send, bool mute) {
        auto e = std::make_unique<Engine>();
        e->prepare(kSr, 4096);
        loadDefaultKit(*e);
        e->part(1).mix.reverbSend = send;
        e->part(1).mix.mute = mute;
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
    const auto dry = render(512, 0.0f, false);
    const auto wet = render(32, 0.5f, false);
    CHECK(wet != dry);
    CHECK(wet == render(4096, 0.5f, false));
    CHECK(wet == render(333, 0.5f, false));
    // A muted part neither plays nor feeds the reverb.
    CHECK(render(512, 0.5f, true) == render(512, 0.0f, true));
}
