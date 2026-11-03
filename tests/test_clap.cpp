#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/engines/Clap.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace zd;

namespace {

std::vector<float> renderClap(const ClapParams& p, int n, int block = 64)
{
    ClapVoice v;
    v.prepare(48000.0);
    v.trigger(p, 1.0f);
    std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
    for (int pos = 0; pos < n; pos += block)
        v.render(out.data() + pos, std::min(block, n - pos));
    return out;
}

/// Short-window RMS envelope, 0.5 ms hop.
std::vector<double> envelope(const std::vector<float>& x)
{
    std::vector<double> e;
    for (std::size_t i = 0; i + 24 <= x.size(); i += 24)
    {
        double s = 0.0;
        for (std::size_t k = i; k < i + 24; ++k)
            s += static_cast<double>(x[k]) * x[k];
        e.push_back(std::sqrt(s / 24.0));
    }
    return e;
}

} // namespace

TEST_CASE("Clap: separate hands, then a tail that dies away")
{
    ClapParams p;
    p.bursts = 4;
    p.spread = 0.010f;
    p.decay = 0.25f;
    const auto y = renderClap(p, 48000);
    CHECK(test::allFinite(y));
    CHECK(test::peak(y) > 0.2);
    CHECK(test::peak(y) < 1.5);

    // Count the hands: envelope rises after a dip, in the first 40 ms.
    const auto e = envelope(y);
    int rises = 0;
    for (std::size_t i = 2; i < 80; ++i)
        if (e[i] > 1.6 * e[i - 2] && e[i - 2] > 1.0e-4)
            ++rises;
    CHECK(rises >= 2); // hands 2..4 each restart the envelope (hand 1 starts from silence)

    // Tail: 200 ms in is far quieter than the hits, and the voice ends by 1 s.
    CHECK(test::toDb(e[400] / *std::max_element(e.begin(), e.end())) < -25.0);
    CHECK(test::peak(std::vector<float>(y.begin() + 40000, y.end())) < 1.0e-3);
}

TEST_CASE("Clap: the body follows the tone setting")
{
    auto centroid = [](const std::vector<float>& y) {
        std::vector<double> d(y.begin(), y.begin() + 4800);
        double num = 0.0, den = 0.0;
        for (double hz = 200.0; hz <= 8000.0; hz += 100.0)
        {
            const double m = test::goertzel(d, hz, 48000.0);
            num += hz * m;
            den += m;
        }
        return num / den;
    };
    ClapParams low, high;
    low.toneHz = 800.0f;
    high.toneHz = 2500.0f;
    low.snap = high.snap = 0.0f;
    CHECK(centroid(renderClap(high, 4800)) > centroid(renderClap(low, 4800)) * 1.4);
}

TEST_CASE("Clap: deterministic and block-size independent; layering on a snare part")
{
    ClapParams p;
    CHECK(renderClap(p, 20000, 64) == renderClap(p, 20000, 997));

    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 512);
    loadDefaultKit(*e);
    std::vector<float> l(512), r(512);
    auto hit = [&](int part) {
        std::vector<float> acc;
        e->reset();
        const NoteEvent ev { 0, part, 1.0f, -1 };
        for (int b = 0; b < 40; ++b)
        {
            e->process(Transport { false, 120.0, 0.0 }, b == 0 ? &ev : nullptr, b == 0 ? 1 : 0, l.data(), r.data(), 512);
            acc.insert(acc.end(), l.begin(), l.end());
        }
        return acc;
    };
    const auto snareOnly = hit(1);
    e->part(1).layer = EngineType::Clap;
    e->part(1).layerGainDb = 0.0f;
    const auto layered = hit(1);
    CHECK(layered != snareOnly);
    CHECK(test::peak(layered) > test::peak(snareOnly));

    // Switching a part to the clap engine: the old snare voice rings out, the next hit claps.
    e->part(1).layer = EngineType::Off;
    e->reset();
    const NoteEvent ev { 0, 1, 1.0f, -1 };
    e->process(Transport { false, 120.0, 0.0 }, &ev, 1, l.data(), r.data(), 512);
    e->part(1).type = EngineType::Clap;
    e->process(Transport { false, 120.0, 0.0 }, nullptr, 0, l.data(), r.data(), 512);
    CHECK(test::peak(l) > 1.0e-3); // the snare tail is still sounding
}
