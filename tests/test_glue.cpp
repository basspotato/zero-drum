#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/dsp/Glue.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace zd;
using zd::dsp::GlueCompressor;
using zd::dsp::GlueSettings;

namespace {

constexpr double kSr = 48000.0;

std::vector<float> sine(double hz, double amp, int n)
{
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(dsp::kTwoPi * hz * i / kSr));
    return x;
}

GlueSettings settings(float threshold, float ratio, float attack = 10.0f, float release = 120.0f)
{
    GlueSettings s;
    s.on = true;
    s.thresholdDb = threshold;
    s.ratio = ratio;
    s.attackMs = attack;
    s.releaseMs = release;
    return s;
}

/// Runs `l`/`r` through a fresh compressor in blocks of `block` samples.
void run(const GlueSettings& s, std::vector<float>& l, std::vector<float>& r, int block)
{
    GlueCompressor c;
    c.prepare(kSr);
    c.set(s);
    for (std::size_t pos = 0; pos < l.size(); pos += static_cast<std::size_t>(block))
    {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), l.size() - pos));
        c.process(l.data() + pos, r.data() + pos, n);
    }
}

double binDb(const std::vector<float>& y, double hz, std::size_t skip)
{
    std::vector<double> d(y.begin() + static_cast<long>(skip), y.end());
    return test::toDb(test::goertzel(d, hz, kSr));
}

} // namespace

TEST_CASE("Glue: static curve has the ratio above the knee and nothing below it")
{
    GlueCompressor c;
    c.prepare(kSr);
    c.set(settings(-20.0f, 4.0f));
    CHECK(c.staticReductionDb(-40.0) == 0.0);
    CHECK(c.staticReductionDb(-23.0) == 0.0); // knee starts 3 dB under the threshold
    CHECK(c.staticReductionDb(-20.0) > 0.0);  // soft knee: a little at the threshold
    CHECK(c.staticReductionDb(-20.0) < 1.0);
    // 10 dB over at 4:1 comes out 2.5 dB over: 7.5 dB of reduction.
    CHECK(c.staticReductionDb(-10.0) == doctest::Approx(7.5));
    // The knee is continuous at both ends.
    CHECK(c.staticReductionDb(-17.0 - 1e-9) == doctest::Approx(c.staticReductionDb(-17.0 + 1e-9)).epsilon(1e-6));
}

TEST_CASE("Glue: a steady tone settles to the static gain, off is bit-transparent")
{
    // 1 kHz at -6 dBFS peak into threshold -20, 4:1: 14 dB over, 10.5 dB of reduction.
    auto l = sine(1000.0, 0.5, 48000);
    auto r = l;
    run(settings(-20.0f, 4.0f), l, r, 512);
    // The peak detector relaxes a little between the tone's peaks, so the average sits
    // just under the static figure.
    const double reduction = -6.0206 - (binDb(l, 1000.0, 24000) + 6.0206); // goertzel gives amplitude / 2
    MESSAGE("1 kHz steady reduction " << reduction << " dB (static 10.5)");
    CHECK(reduction > 9.0);
    CHECK(reduction <= 10.5);

    auto a = sine(300.0, 0.9, 4096);
    auto b = a;
    const auto ref = a;
    GlueSettings off = settings(-30.0f, 10.0f);
    off.on = false;
    run(off, a, b, 256);
    CHECK(a == ref);
}

TEST_CASE("Glue: bit-identical for any block size, and the channels share one gain")
{
    const int n = 30000;
    std::vector<float> l(static_cast<std::size_t>(n)), r(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double env = std::exp(-(i % 6000) / 1500.0);
        l[static_cast<std::size_t>(i)] = static_cast<float>(0.9 * env * std::sin(dsp::kTwoPi * 180.0 * i / kSr));
        r[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * env * std::sin(dsp::kTwoPi * 2500.0 * i / kSr));
    }
    const auto l0 = l, r0 = r;
    auto la = l, ra = r;
    run(settings(-18.0f, 4.0f, 5.0f, 100.0f), la, ra, 1);
    for (int block : { 7, 64, 333, 4096 })
    {
        auto lb = l, rb = r;
        run(settings(-18.0f, 4.0f, 5.0f, 100.0f), lb, rb, block);
        CHECK(lb == la);
        CHECK(rb == ra);
    }
    // Stereo link: both channels get the same gain, so the left/right ratio is untouched.
    bool linked = true;
    for (int i = 0; i < n; i += 37)
    {
        const auto k = static_cast<std::size_t>(i);
        if (std::abs(l0[k]) > 1e-3f && std::abs(r0[k]) > 1e-3f)
            linked = linked && std::abs(la[k] / l0[k] - ra[k] / r0[k]) < 1e-4f;
    }
    CHECK(linked);
}

TEST_CASE("Glue: the sidechain high-pass keeps a loud sub from pumping the bus")
{
    // 40 Hz at -3 dBFS: well over the threshold, but under the 90 Hz detector high-pass.
    auto sub = sine(40.0, 0.7, 48000);
    auto subR = sub;
    run(settings(-20.0f, 4.0f), sub, subR, 512);
    auto bright = sine(1000.0, 0.7, 48000);
    auto brightR = bright;
    run(settings(-20.0f, 4.0f), bright, brightR, 512);
    const double subLoss = -6.0206 - 3.098 - binDb(sub, 40.0, 24000);
    const double brightLoss = -6.0206 - 3.098 - binDb(bright, 1000.0, 24000);
    MESSAGE("40 Hz reduction " << subLoss << " dB, 1 kHz reduction " << brightLoss << " dB");
    CHECK(brightLoss > 10.0);
    CHECK(subLoss < brightLoss - 6.0);
}

TEST_CASE("Glue: gain riding adds no audible aliasing (aliasing test)")
{
    // A loud 7 kHz tone at the fastest attack. A waveshaper would put its 5th harmonic
    // (35 kHz) back at 13 kHz; a smoothed gain must keep anything there far down.
    auto l = sine(7000.0, 0.9, 96000);
    auto r = l;
    run(settings(-30.0f, 10.0f, 0.1f, 50.0f), l, r, 512);
    const double tone = binDb(l, 7000.0, 48000);
    const double fold = binDb(l, 13000.0, 48000);
    const double third = binDb(l, 21000.0, 48000);
    MESSAGE("tone " << tone << " dB, 13 kHz fold " << fold << " dB, 21 kHz " << third << " dB");
    CHECK(fold < tone - 60.0);
    CHECK(third < tone - 40.0);
}

TEST_CASE("Glue: every style kit turns it on and stays under 0 dBFS")
{
    for (int st = 0; st < static_cast<int>(gen::Style::kCount); ++st)
    {
        const auto style = static_cast<gen::Style>(st);
        const auto kit = gen::styleKit(style);
        CHECK(kit.glue.on);
        CHECK(kit.glue.ratio >= 1.5f);
        CHECK(kit.glue.attackMs >= 3.0f); // slow enough to keep the transients
        auto e = std::make_unique<Engine>();
        e->prepare(kSr, 512);
        loadDefaultKit(*e);
        gen::applyKit(kit, *e, kit.rootNote);
        CHECK(e->glue().on);
        CHECK(e->glue().ratio == kit.glue.ratio);
    }
}
