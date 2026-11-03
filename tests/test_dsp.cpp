#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/dsp/Adaa.h"
#include "zd/dsp/Math.h"
#include "zd/dsp/Random.h"
#include "zd/dsp/Svf.h"

#include <cmath>
#include <vector>

using namespace zd;

TEST_CASE("ADAA tanh matches tanh for slow signals")
{
    dsp::AdaaTanh shaper;
    // For a slowly changing input, ADAA1 equals tanh at the midpoint of consecutive samples.
    double prev = 0.0;
    for (int n = 0; n < 1000; ++n)
    {
        const double x = 3.0 * std::sin(2.0 * dsp::kPi * 20.0 * n / 48000.0);
        const double y = shaper.process(x);
        if (n > 0)
            CHECK(y == doctest::Approx(std::tanh(0.5 * (x + prev))).epsilon(1e-4));
        prev = x;
    }
}

TEST_CASE("ADAA tanh aliases far less than naive tanh")
{
    // A 7 kHz sine driven hard at 44.1 kHz. Its odd harmonics above Nyquist fold back to
    // 9.1 kHz (35 kHz), 4.9 kHz (49 kHz) and 18.9 kHz (63 kHz). Measure those bins.
    constexpr double sr = 44100.0;
    constexpr double f0 = 7000.0;
    const int n = static_cast<int>(sr);

    std::vector<double> naive(static_cast<std::size_t>(n)), adaa(static_cast<std::size_t>(n));
    dsp::AdaaTanh shaper;
    for (int i = 0; i < n; ++i)
    {
        const double x = 8.0 * std::sin(2.0 * dsp::kPi * f0 * i / sr);
        naive[static_cast<std::size_t>(i)] = std::tanh(x);
        adaa[static_cast<std::size_t>(i)] = shaper.process(x);
    }

    double naiveAlias = 0.0, adaaAlias = 0.0;
    for (double f : { 9100.0, 4900.0, 18900.0 })
    {
        naiveAlias += zd::test::goertzel(naive, f, sr);
        adaaAlias += zd::test::goertzel(adaa, f, sr);
    }
    const double improvementDb = zd::test::toDb(naiveAlias) - zd::test::toDb(adaaAlias);
    MESSAGE("ADAA alias reduction: " << improvementDb << " dB");
    CHECK(improvementDb > 6.0);
}

TEST_CASE("SVF stays stable under fast cutoff modulation at high resonance")
{
    dsp::Svf svf;
    dsp::Random rng(42);
    double maxAbs = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double cutoff = 100.0 + 15000.0 * (0.5 + 0.5 * std::sin(i * 0.05));
        svf.setCoefficients(cutoff, 20.0, 48000.0);
        const auto out = svf.process(rng.bipolar());
        REQUIRE(std::isfinite(out.low));
        REQUIRE(std::isfinite(out.band));
        REQUIRE(std::isfinite(out.high));
        maxAbs = std::max({ maxAbs, std::abs(out.low), std::abs(out.band), std::abs(out.high) });
    }
    CHECK(maxAbs < 200.0);
}

TEST_CASE("SVF low-pass passes DC and rejects high frequencies")
{
    dsp::Svf svf;
    svf.setCoefficients(1000.0, 0.707, 48000.0);
    double y = 0.0;
    for (int i = 0; i < 48000; ++i)
        y = svf.process(1.0).low;
    CHECK(y == doctest::Approx(1.0).epsilon(1e-6));

    svf.reset();
    std::vector<double> out;
    for (int i = 0; i < 48000; ++i)
        out.push_back(svf.process(std::sin(2.0 * dsp::kPi * 16000.0 * i / 48000.0)).low);
    CHECK(zd::test::toDb(zd::test::goertzel(out, 16000.0, 48000.0) * 2.0) < -40.0);
}

TEST_CASE("Random and hash are deterministic")
{
    dsp::Random a(7), b(7);
    for (int i = 0; i < 100; ++i)
        CHECK(a.next() == b.next());
    CHECK(dsp::hash64(123) == dsp::hash64(123));
    CHECK(dsp::hash64(123) != dsp::hash64(124));
    const double u = dsp::hashToUnit(99);
    CHECK(u >= 0.0);
    CHECK(u < 1.0);
}
