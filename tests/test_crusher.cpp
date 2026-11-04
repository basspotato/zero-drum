#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Strip.h"
#include "zd/dsp/Crusher.h"
#include "zd/dsp/Math.h"
#include "zd/gen/StyleKit.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace zd;

namespace {

constexpr double kSr = 48000.0;

std::vector<double> crush(double hz, float bits, float rate, bool filter, int n)
{
    dsp::Crusher c;
    c.prepare(kSr);
    c.set(bits, rate);
    std::vector<double> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        y[static_cast<std::size_t>(i)] = c.process(0.8 * std::sin(2.0 * dsp::kPi * hz * i / kSr), filter);
    return y;
}

double binDb(const std::vector<double>& y, double hz)
{
    const std::vector<double> d(y.begin() + 4800, y.begin() + 4800 + static_cast<long>(kSr));
    return test::toDb(test::goertzel(d, hz, kSr));
}

} // namespace

TEST_CASE("Crusher: bits quantize to the grid and off is transparent")
{
    const auto y = crush(440.0, 4.0f, 0.0f, true, 4800);
    for (double v : y)
        CHECK(std::abs(v * 8.0 - std::round(v * 8.0)) < 1e-12);
    for (std::size_t i = 0; i < y.size(); ++i)
        CHECK(std::abs(y[i] - 0.8 * std::sin(2.0 * dsp::kPi * 440.0 * static_cast<double>(i) / kSr)) <= 1.0 / 16.0 + 1e-12);

    const auto clean = crush(440.0, 0.0f, 0.0f, true, 480);
    for (std::size_t i = 0; i < clean.size(); ++i)
        CHECK(clean[i] == 0.8 * std::sin(2.0 * dsp::kPi * 440.0 * static_cast<double>(i) / kSr));
}

TEST_CASE("Crusher: the hold repeats each sample at the reduced rate")
{
    const auto y = crush(300.0, 0.0f, 12000.0f, false, 4000);
    for (std::size_t i = 0; i + 4 <= y.size(); i += 4)
        for (std::size_t k = 1; k < 4; ++k)
            CHECK(y[i + k] == y[i]);
    CHECK(y[4] != y[0]); // and moves on at the next held sample
}

TEST_CASE("Crusher: the pre-filter stops high input from folding down (aliasing test)")
{
    // 20 kHz into a 24 kHz hold folds to 4 kHz. The pre-filter must take most of it out.
    const double raw = binDb(crush(20000.0, 0.0f, 24000.0f, false, 60000), 4000.0);
    const double filtered = binDb(crush(20000.0, 0.0f, 24000.0f, true, 60000), 4000.0);
    MESSAGE("4 kHz fold: unfiltered " << raw << " dB, filtered " << filtered << " dB");
    CHECK(filtered < raw - 18.0);

    // A tone below the new Nyquist goes through the filter nearly untouched.
    const double pass = binDb(crush(2000.0, 0.0f, 24000.0f, true, 60000), 2000.0);
    const double passRaw = binDb(crush(2000.0, 0.0f, 24000.0f, false, 60000), 2000.0);
    CHECK(std::abs(pass - passRaw) < 0.5);
}

TEST_CASE("Crusher in the strip: bit-identical for any block size")
{
    StripParams p;
    p.crushBits = 12.0f;
    p.crushRateHz = 26040.0f;
    p.filterMode = FilterMode::LowPass;
    p.cutoffHz = 10000.0f;

    const int n = 20000;
    std::vector<float> in(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<std::size_t>(i)] = static_cast<float>(0.7 * std::sin(2.0 * dsp::kPi * 180.0 * i / kSr) * std::exp(-i / 6000.0));

    auto render = [&](int block) {
        Strip s;
        s.prepare(kSr);
        std::vector<float> y = in;
        for (int pos = 0; pos < n; pos += block)
            s.process(y.data() + pos, std::min(block, n - pos), p);
        return y;
    };
    const auto a = render(1);
    const auto b = render(512);
    const auto c = render(777);
    CHECK(a == b);
    CHECK(a == c);
}

TEST_CASE("Style kits: boom bap drums go through the 12-bit sampler, trap stays clean")
{
    const auto bb = gen::styleKit(gen::Style::BoomBap);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(bb.strips[static_cast<std::size_t>(i)].crushBits == 12.0f);
        CHECK(bb.strips[static_cast<std::size_t>(i)].crushRateHz == doctest::Approx(26040.0f));
    }
    CHECK(bb.strips[4].crushBits == 0.0f); // the 808 stays clean
    const auto trap = gen::styleKit(gen::Style::Trap);
    for (const auto& s : trap.strips)
        CHECK((s.crushBits == 0.0f && s.crushRateHz == 0.0f));
}
