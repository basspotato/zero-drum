#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/Strip.h"
#include "zd/engines/FmPerc.h"
#include "zd/engines/Sub808.h"

#include <vector>

using namespace zd;

namespace {

template <typename V>
std::vector<float> renderVoice(V& voice, int numSamples, int blockSize)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; i += blockSize)
        voice.render(out.data() + i, std::min(blockSize, numSamples - i));
    return out;
}

double zeroCrossingHz(const std::vector<float>& x, int from, int to, double sr)
{
    int c = 0;
    for (int i = from + 1; i < to; ++i)
        if ((x[static_cast<std::size_t>(i - 1)] < 0.0f) != (x[static_cast<std::size_t>(i)] < 0.0f))
            ++c;
    return c / 2.0 / ((to - from) / sr);
}

double energy(const std::vector<float>& x, int from, int to)
{
    double e = 0.0;
    for (int i = from; i < to; ++i)
        e += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    return e;
}

} // namespace

// ------------------------------------------------------------------------------------- FM

TEST_CASE("FM: index limit keeps the Carson bandwidth under Nyquist")
{
    constexpr double sr = 44100.0;
    const double limited = FmPercVoice::limitIndex(20.0, 6000.0, 9000.0, sr);
    CHECK(limited > 0.0);
    CHECK(6000.0 + (limited + 1.0) * 9000.0 <= 0.45 * sr + 1e-6);
    CHECK(FmPercVoice::limitIndex(2.0, 200.0, 300.0, sr) == doctest::Approx(2.0)); // low notes untouched
    CHECK(FmPercVoice::limitIndex(5.0, 30000.0, 1000.0, sr) == 0.0);               // never negative
}

TEST_CASE("FM: renders a bounded, finite hit that ends, independent of block size")
{
    FmParams p;
    p.index = 8.0f;
    p.feedback = 0.8f;
    FmPercVoice a, b;
    a.prepare(48000.0);
    b.prepare(48000.0);
    a.trigger(p, 1.0f);
    b.trigger(p, 1.0f);
    const auto x = renderVoice(a, 48000, 1);
    const auto y = renderVoice(b, 48000, 3000);
    CHECK(x == y);
    CHECK(zd::test::allFinite(x));
    CHECK(zd::test::peak(x) > 0.5);
    CHECK(zd::test::peak(x) <= 1.0);
    CHECK_FALSE(a.isActive());
}

TEST_CASE("FM: a high, bright hit stays alias-limited")
{
    // 5 kHz carrier, ratio 2.7, index 15 at 44.1 kHz would put sidebands far past Nyquist.
    // With the index limit, energy below the carrier's lowest legal sideband stays small.
    constexpr double sr = 44100.0;
    FmParams p;
    p.tuneHz = 5000.0f;
    p.ratio = 2.7f;
    p.index = 15.0f;
    p.indexDecay = 10.0f; // hold the index
    p.pitchDrop = 0.0f;
    p.feedback = 0.0f;
    p.decay = 10.0f;
    FmPercVoice v;
    v.prepare(sr);
    v.trigger(p, 1.0f);
    const auto out = renderVoice(v, static_cast<int>(sr), 512);
    std::vector<double> x(out.begin(), out.end());
    // fc - fm = -8.5 kHz reflects to 8.5 kHz (a legal sideband). Folded content from above
    // Nyquist would land on non-sideband frequencies such as 1 kHz; that bin must be quiet.
    const double legal = zd::test::goertzel(x, 8500.0, sr);
    const double folded = zd::test::goertzel(x, 1000.0, sr);
    CHECK(zd::test::toDb(folded) < zd::test::toDb(legal) - 40.0);
}

// ------------------------------------------------------------------------------------ 808

TEST_CASE("808: settles on the MIDI note's frequency")
{
    constexpr double sr = 48000.0;
    Sub808Params p;
    p.drive = 0.0f;
    p.decay = 3.0f;
    Sub808Voice v;
    v.prepare(sr);
    v.trigger(p, 1.0f, noteToHz(33), noteToHz(33)); // A1 = 55 Hz
    const auto out = renderVoice(v, 48000, 256);
    CHECK(zeroCrossingHz(out, 24000, 48000, sr) == doctest::Approx(55.0).epsilon(0.03));
    CHECK(noteToHz(69) == doctest::Approx(440.0));
}

TEST_CASE("808: glide starts at the previous note and slides to the new one")
{
    constexpr double sr = 48000.0;
    Sub808Params p;
    p.drive = 0.0f;
    p.punch = 0.0f;
    p.glide = 0.05f;
    p.decay = 3.0f;
    Sub808Voice v;
    v.prepare(sr);
    v.trigger(p, 1.0f, noteToHz(45), noteToHz(33)); // 55 Hz up to 110 Hz
    const auto out = renderVoice(v, 48000, 128);
    const double early = zeroCrossingHz(out, 0, 1440, sr);         // first 30 ms
    const double late = zeroCrossingHz(out, 24000, 48000, sr);
    CHECK(early < 95.0);
    CHECK(late == doctest::Approx(110.0).epsilon(0.03));
}

TEST_CASE("808: starts without a click (1 ms attack ramp)")
{
    Sub808Voice v;
    v.prepare(48000.0);
    v.trigger(Sub808Params {}, 1.0f, 55.0, 55.0);
    std::vector<float> out(8, 0.0f);
    v.render(out.data(), 8);
    CHECK(std::abs(out[0]) < 1.0e-3f);
}

TEST_CASE("Engine: 808 part plays sequencer step notes and glides between them")
{
    constexpr double sr = 48000.0;
    auto e = std::make_unique<Engine>();
    e->prepare(sr, 512);
    auto& sub = e->part(4);
    sub.type = EngineType::Sub808;
    sub.mix.mono = true;
    sub.sub.drive = 0.0f;
    sub.sub.punch = 0.0f;
    sub.sub.decay = 4.0f;
    sub.sub.glide = 0.0f;
    std::vector<float> l(48000), r(48000);
    const NoteEvent ev { 0, 4, 1.0f, 45 }; // A2 = 110 Hz
    e->process(Transport {}, &ev, 1, l.data(), r.data(), 512);
    for (int pos = 512; pos < 48000; pos += 512)
        e->process(Transport {}, nullptr, 0, l.data() + pos, r.data() + pos, std::min(512, 48000 - pos));
    CHECK(zeroCrossingHz(l, 12000, 48000, sr) == doctest::Approx(110.0).epsilon(0.03));
}

// ---------------------------------------------------------------------------------- Strip

TEST_CASE("Strip: transient attack boost raises the hit's front, sustain cut lowers its tail")
{
    constexpr double sr = 48000.0;
    auto render = [&](float attack, float sustain) {
        AnalogSnareVoice v;
        v.prepare(sr);
        v.trigger(SnareParams {}, 1.0f);
        std::vector<float> out(24000, 0.0f);
        v.render(out.data(), 24000);
        Strip s;
        s.prepare(sr);
        StripParams p;
        p.attack = attack;
        p.sustain = sustain;
        s.process(out.data(), 24000, p);
        return out;
    };
    const auto dry = render(0.0f, 0.0f);
    const auto punchy = render(1.0f, 0.0f);
    const auto tight = render(0.0f, -1.0f);
    CHECK(energy(punchy, 0, 240) > 1.5 * energy(dry, 0, 240));         // first 5 ms
    CHECK(energy(tight, 4800, 24000) < 0.5 * energy(dry, 4800, 24000)); // after 100 ms
}

TEST_CASE("Strip: low-pass removes high frequencies")
{
    constexpr double sr = 48000.0;
    std::vector<float> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 12000.0 * static_cast<double>(i) / sr));
    Strip s;
    s.prepare(sr);
    StripParams p;
    p.filterMode = FilterMode::LowPass;
    p.cutoffHz = 500.0f;
    s.process(x.data(), 48000, p);
    CHECK(zd::test::peak(std::vector<float>(x.begin() + 24000, x.end())) < 0.005);
}

TEST_CASE("Null test: strips active on every part keep block-size independence")
{
    constexpr double sr = 44100.0;
    const int total = static_cast<int>(sr * 4.0);
    auto render = [&](const std::vector<int>& sizes) {
        auto e = std::make_unique<Engine>();
        e->prepare(sr, 4096);
        loadDefaultKit(*e);
        for (int i = 0; i < 5; ++i)
        {
            auto& st = e->part(i).strip;
            st.attack = 0.6f;
            st.sustain = -0.4f;
            st.filterMode = (i % 2 == 0) ? FilterMode::LowPass : FilterMode::HighPass;
            st.cutoffHz = (i % 2 == 0) ? 6000.0f : 80.0f;
            st.resonance = 1.2f;
            st.drive = 0.3f;
        }
        std::vector<float> l(static_cast<std::size_t>(total)), r(static_cast<std::size_t>(total));
        InternalClock clock(140.0);
        int pos = 0, b = 0;
        while (pos < total)
        {
            const int n = std::min(sizes[static_cast<std::size_t>(b++) % sizes.size()], total - pos);
            e->process(clock.next(sr, n), nullptr, 0, l.data() + pos, r.data() + pos, n);
            pos += n;
        }
        return l;
    };
    const auto a = render({ 64 });
    const auto b = render({ 4096 });
    const auto c = render({ 7, 1000, 1, 256 });
    CHECK(zd::test::allFinite(a));
    CHECK(zd::test::peak(a) > 0.1);
    CHECK(a == b);
    CHECK(a == c);
}
