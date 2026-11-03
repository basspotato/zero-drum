#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/dsp/Clipper.h"
#include "zd/export/Export.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace zd;
using zd::dsp::MasterClipper;

namespace {

constexpr double kSr = 48000.0;

std::vector<float> sine(double hz, double amp, int n)
{
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(dsp::kTwoPi * hz * i / kSr));
    return x;
}

/// Runs a mono signal through both channels of a clipper.
std::vector<float> run(MasterClipper& c, std::vector<float> x, int block = 256)
{
    std::vector<float> r = x;
    for (std::size_t pos = 0; pos < x.size(); pos += static_cast<std::size_t>(block))
    {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - pos));
        c.process(x.data() + pos, r.data() + pos, n);
    }
    return x;
}

double bin(const std::vector<float>& y, double hz, int skip)
{
    std::vector<double> d(y.begin() + skip, y.begin() + skip + static_cast<long>(kSr));
    return test::goertzel(d, hz, kSr);
}

} // namespace

TEST_CASE("Master clipper: constant 64-sample latency, transparent below the knee")
{
    MasterClipper c;
    c.setCeilingDb(-1.0f);
    std::vector<float> imp(512, 0.0f);
    imp[10] = 0.25f;

    auto on = run(c, imp);
    const auto peakAt = std::max_element(on.begin(), on.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }) - on.begin();
    CHECK(peakAt == 10 + MasterClipper::kLatency);
    double dc = 0.0;
    for (float v : on)
        dc += v;
    CHECK(dc == doctest::Approx(0.25).epsilon(0.001)); // unity gain; the peak is band-limited

    MasterClipper bypass;
    bypass.setEnabled(false);
    auto off = run(bypass, imp);
    for (std::size_t i = 0; i < off.size(); ++i)
        CHECK(off[i] == (i == 10u + MasterClipper::kLatency ? 0.25f : 0.0f));

    // A -10 dBFS tone up to 18 kHz comes out unchanged, only delayed.
    for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
    {
        MasterClipper t;
        const auto x = sine(hz, 0.316, 8192);
        const auto y = run(t, x);
        double err = 0.0;
        for (std::size_t i = 1000; i < x.size(); ++i)
            err = std::max(err, static_cast<double>(std::abs(y[i] - x[i - MasterClipper::kLatency])));
        CAPTURE(hz);
        CHECK(err < 0.005);
    }
}

TEST_CASE("Master clipper: holds the ceiling and aliases far less than clipping at 1x")
{
    MasterClipper c;
    c.setCeilingDb(-1.0f);
    c.setDriveDb(12.0f);
    const auto x = sine(7000.0, 0.9, 60000);
    const auto y = run(c, x);
    CHECK(test::peak(y) < 1.0);
    CHECK(test::peak(y) > 0.8);

    // The same curve at 1x.
    std::vector<float> naive(x.size());
    const double ceiling = dsp::dbToGain(-1.0f), knee = ceiling * 0.7079457843841379;
    for (std::size_t i = 0; i < x.size(); ++i)
        naive[i] = static_cast<float>(dsp::softKneeClip(x[i] * dsp::dbToGain(12.0f), knee, ceiling));

    // 7 kHz harmonics at 35 and 49 kHz fold to 13 and 1 kHz at 1x.
    double worst = -1.0e9;
    for (double alias : { 13000.0, 1000.0 })
    {
        const double over = test::toDb(bin(y, alias, 2000));
        const double base = test::toDb(bin(naive, alias, 2000));
        MESSAGE("alias at " << alias << " Hz: oversampled " << over << " dB, 1x " << base << " dB");
        worst = std::max(worst, over - base);
    }
    CHECK(worst < -20.0);
    // The wanted harmonic (21 kHz) is still there: it is a clipper, not a filter.
    CHECK(test::toDb(bin(y, 21000.0, 2000)) > -60.0);
}

TEST_CASE("Master stage: output is bit-identical for any block size")
{
    auto render = [](int block) {
        auto e = std::make_unique<Engine>();
        e->prepare(kSr, 512);
        loadDefaultKit(*e);
        e->setMasterStage(true);
        e->setMasterClip(true, 9.0f, -1.0f);
        gen::generate({ gen::Style::Dubstep, 0.8f, 0.8f, 29, 3, 0 }, e->sequencer().pattern());
        std::vector<float> l(48000), r(48000);
        InternalClock clock(140.0);
        for (int pos = 0; pos < 48000; pos += block)
        {
            const int n = std::min(block, 48000 - pos);
            e->process(clock.next(kSr, n), nullptr, 0, l.data() + pos, r.data() + pos, n);
        }
        return l;
    };
    const auto a = render(512), b = render(37);
    CHECK(a == b);
    CHECK(test::peak(a) < 1.0);
}

TEST_CASE("Export with the master stage: latency compensated, part files sum to the unclipped mix")
{
    auto makeEngine = [](bool stage, bool clip) {
        auto e = std::make_unique<Engine>();
        e->prepare(kSr, 512);
        loadDefaultKit(*e);
        gen::applyKit(gen::styleKit(gen::Style::Trap), *e, 29);
        gen::generate({ gen::Style::Trap, 0.7f, 0.7f, 29, 5, 0 }, e->sequencer().pattern());
        e->setMasterStage(stage);
        e->setMasterClip(clip, 6.0f, -1.0f);
        return e;
    };
    auto readWav = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::vector<char> b { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
        std::vector<float> out((b.size() - 44) / 4);
        std::memcpy(out.data(), b.data() + 44, out.size() * 4);
        return out;
    };
    const auto tmp = std::filesystem::temp_directory_path();
    exporting::ExportOptions opt;
    opt.bpm = 140.0;

    const auto dirPlain = tmp / "zd-test-master-plain";
    const auto dirDelay = tmp / "zd-test-master-delay";
    const auto dirClip = tmp / "zd-test-master-clip";
    for (const auto& d : { dirPlain, dirDelay, dirClip })
        std::filesystem::remove_all(d);
    REQUIRE(exporting::exportLoop(*makeEngine(false, false), opt, dirPlain).ok);
    REQUIRE(exporting::exportLoop(*makeEngine(true, false), opt, dirDelay).ok);
    auto clipped = makeEngine(true, true);
    REQUIRE(exporting::exportLoop(*clipped, opt, dirClip).ok);

    // The delay-only stage exports exactly what no stage does: latency is compensated.
    CHECK(readWav(dirPlain / "mix.wav") == readWav(dirDelay / "mix.wav"));
    // Part files skip the clipper, the mix does not; the clip settings are restored.
    CHECK(readWav(dirPlain / "parts" / "01-part-1.wav") == readWav(dirClip / "parts" / "01-part-1.wav"));
    const auto mixClip = readWav(dirClip / "mix.wav");
    CHECK(test::peak(mixClip) < 1.0);
    CHECK(mixClip != readWav(dirPlain / "mix.wav"));
    CHECK(clipped->masterClip().clipOn);
    CHECK(clipped->masterClip().driveDb == 6.0f);
    for (const auto& d : { dirPlain, dirDelay, dirClip })
        std::filesystem::remove_all(d);
}
