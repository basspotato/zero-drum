#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/dsp/Adaa.h"
#include "zd/dsp/Math.h"
#include "zd/dsp/Svf.h"
#include "zd/export/Export.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace zd;

namespace {

constexpr double kSr = 48000.0;

/// Frequency from zero crossings over [from, to).
double pitchOf(const std::vector<float>& y, int from, int to)
{
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if ((y[static_cast<std::size_t>(i - 1)] < 0.0f) != (y[static_cast<std::size_t>(i)] < 0.0f))
            ++crossings;
    return crossings * 0.5 * kSr / (to - from);
}

std::vector<float> kickHit(int note, float drive2)
{
    auto e = std::make_unique<Engine>();
    e->prepare(kSr, 512);
    loadDefaultKit(*e);
    e->part(0).strip = StripParams {};
    e->part(0).kick.pitchAmount = 0.0f;
    e->part(0).kick.click = 0.0f;
    e->part(0).kick.decay = 2.0f;
    e->part(0).kick.drive2 = drive2;
    const int total = 24000;
    std::vector<float> l(total), r(total);
    const NoteEvent hit { 0, 0, 1.0f, note };
    Transport t;
    for (int pos = 0; pos < total; pos += 512)
        e->process(t, pos == 0 ? &hit : nullptr, pos == 0 ? 1 : 0, l.data() + pos, r.data() + pos, std::min(512, total - pos));
    return l;
}

} // namespace

TEST_CASE("Melodic kick: a note sets the kick's pitch, no note keeps its own tune")
{
    CHECK(pitchOf(kickHit(36, 0.0f), 2400, 24000) == doctest::Approx(noteToHz(36)).epsilon(0.03));
    CHECK(pitchOf(kickHit(43, 0.0f), 2400, 24000) == doctest::Approx(noteToHz(43)).epsilon(0.03));
    CHECK(pitchOf(kickHit(-1, 0.0f), 2400, 24000) == doctest::Approx(50.0).epsilon(0.03)); // default tune
    // The two-stage drive keeps the pitch and stays finite.
    const auto driven = kickHit(36, 1.0f);
    CHECK(pitchOf(driven, 2400, 24000) == doctest::Approx(noteToHz(36)).epsilon(0.03));
    CHECK(test::peak(driven) < 4.0);
}

TEST_CASE("Melodic kick: the generator writes a chord line for hardstyle, nothing for trap")
{
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        gen::GenerateParams p;
        p.style = gen::Style::Hardstyle;
        p.seed = seed;
        p.rootNote = 29;
        Pattern pat;
        gen::generate(p, pat);
        const auto kit = gen::styleKit(p.style);
        const int base = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(gen::rootTunedKickHz(29, kit.kickInterval, kit.kick.tuneHz) / 440.0)));
        const Lane& k = pat.lanes[gen::Kick];
        for (int i = 0; i < k.length; ++i)
        {
            const Step& s = k.steps[static_cast<std::size_t>(i)];
            if (!s.on)
                continue;
            REQUIRE(s.note >= 0);
            const int move = s.note - base;
            CHECK((move == 0 || move == -4 || move == 3 || move == -2 || move == 5));
            if (i < 8)
                CHECK(move == 0);
        }
        CHECK(exporting::melodicKick(pat, 0));
    }
    gen::GenerateParams p;
    p.style = gen::Style::Trap;
    Pattern pat;
    gen::generate(p, pat);
    CHECK_FALSE(exporting::melodicKick(pat, 0));
}

TEST_CASE("Melodic kick: MIDI export puts the kick line on channel 3 with its notes")
{
    Sequencer seq;
    gen::GenerateParams p;
    p.style = gen::Style::Hardstyle;
    gen::generate(p, seq.pattern());
    const auto tracks = exporting::patternTracks(seq, 2, {}, 1u << 4, 0);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].channel == exporting::kMelodicKickChannel);
    CHECK(tracks[0].notes.front().note == seq.pattern().lanes[0].steps[0].note);

    gen::GenerateParams t;
    t.style = gen::Style::Trap;
    gen::generate(t, seq.pattern());
    const auto drums = exporting::patternTracks(seq, 2, {}, 1u << 4, 0);
    REQUIRE(drums.size() == 1);
    CHECK(drums[0].channel == 9);
    CHECK(drums[0].notes.front().note == 36);
}

TEST_CASE("Two-stage kick drive: ADAA stages alias far less than naive tanh stages")
{
    // The kick's chain (drive, mid bell, drive) on a 7 kHz sine at 44.1 kHz, against the same
    // chain with plain tanh. Fold-back bins of the odd harmonics as in the single-stage test.
    constexpr double sr = 44100.0;
    const int n = static_cast<int>(sr);
    dsp::DriveStage a, b;
    a.setDrive(0.9);
    b.setDrive(0.7);
    dsp::Svf bellA, bellB;
    bellA.setCoefficients(800.0, 1.2, sr);
    bellB.setCoefficients(800.0, 1.2, sr);
    const double g = dsp::dbToGain(8.0) - 1.0, k = 1.0 / 1.2;
    const double ga = 1.0 + 19.0 * 0.81, gb = 1.0 + 19.0 * 0.49;
    std::vector<double> adaa(static_cast<std::size_t>(n)), naive(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double x = 0.8 * std::sin(2.0 * dsp::kPi * 7000.0 * i / sr);
        double y = a.process(x);
        y += g * k * bellA.process(y).band;
        adaa[static_cast<std::size_t>(i)] = b.process(y);
        double z = std::tanh(x * ga) / std::tanh(ga);
        z += g * k * bellB.process(z).band;
        naive[static_cast<std::size_t>(i)] = std::tanh(z * gb) / std::tanh(gb);
    }
    double na = 0.0, aa = 0.0;
    for (double f : { 9100.0, 4900.0, 18900.0 })
    {
        na += test::goertzel(naive, f, sr);
        aa += test::goertzel(adaa, f, sr);
    }
    const double improvement = test::toDb(na) - test::toDb(aa);
    MESSAGE("two-stage alias reduction: " << improvement << " dB");
    CHECK(improvement > 6.0);
}
