#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/PatternIO.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace zd;
using namespace zd::gen;

namespace {

bool hitOn(const Pattern& p, int lane, int step) { return p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)].on; }

bool lanesEqual(const Lane& a, const Lane& b)
{
    if (a.length != b.length)
        return false;
    for (std::size_t i = 0; i < a.steps.size(); ++i)
    {
        const Step& x = a.steps[i];
        const Step& y = b.steps[i];
        if (x.on != y.on || x.velocity != y.velocity || x.probability != y.probability || x.microOffset != y.microOffset
            || x.note != y.note || x.ratchet != y.ratchet)
            return false;
    }
    return true;
}

bool patternsEqual(const Pattern& a, const Pattern& b)
{
    for (int i = 0; i < kMaxParts; ++i)
        if (!lanesEqual(a.lanes[static_cast<std::size_t>(i)], b.lanes[static_cast<std::size_t>(i)]))
            return false;
    return a.swing == b.swing && a.stepBeats == b.stepBeats;
}

} // namespace

TEST_CASE("Generator: same params and seed give the same beat")
{
    for (int s = 0; s < static_cast<int>(Style::kCount); ++s)
    {
        GenerateParams p;
        p.style = static_cast<Style>(s);
        p.seed = 77;
        Pattern a, b;
        generate(p, a);
        generate(p, b);
        CHECK(patternsEqual(a, b));
        p.seed = 78;
        Pattern c;
        generate(p, c);
        CHECK(countHits(c.lanes[Kick]) > 0);
        CHECK(countHits(c.lanes[Snare]) + countHits(c.lanes[Perc]) > 0);
        CHECK_FALSE(styleName(p.style).empty());
        CHECK(styleTempo(p.style).low < styleTempo(p.style).high);
    }
}

TEST_CASE("Generator: genre anchors are always present")
{
    for (std::uint64_t seed = 1; seed < 20; ++seed)
    {
        Pattern house, trap, reggaeton;
        generate({ Style::House, 0.2f, 0.2f, 33, seed, 0 }, house);
        generate({ Style::Trap, 0.8f, 0.9f, 33, seed, 0 }, trap);
        generate({ Style::Reggaeton, 0.5f, 0.5f, 33, seed, 0 }, reggaeton);
        for (int bar = 0; bar < 2; ++bar)
        {
            for (int s : { 0, 4, 8, 12 })
                REQUIRE(hitOn(house, Kick, bar * 16 + s)); // four on the floor
            REQUIRE(hitOn(trap, Snare, bar * 16 + 8));      // half-time backbeat
            for (int s : { 3, 6, 11, 14 })
                REQUIRE(hitOn(reggaeton, Snare, bar * 16 + s)); // dembow
        }
    }
}

TEST_CASE("Generator: anchors for hip-hop, dubstep, trance, hardstyle and festival")
{
    for (std::uint64_t seed = 1; seed < 20; ++seed)
    {
        Pattern hiphop, dubstep, trance, hardstyle, festival;
        generate({ Style::HipHop, 0.5f, 0.5f, 33, seed, 0 }, hiphop);
        generate({ Style::Dubstep, 0.5f, 0.8f, 33, seed, 0 }, dubstep);
        generate({ Style::Trance, 0.5f, 0.5f, 33, seed, 0 }, trance);
        generate({ Style::Hardstyle, 0.5f, 0.5f, 33, seed, 0 }, hardstyle);
        generate({ Style::Festival, 0.5f, 0.5f, 33, seed, 0 }, festival);
        for (int bar = 0; bar < 2; ++bar)
        {
            const int b = bar * 16;
            REQUIRE(hitOn(hiphop, Snare, b + 4));
            REQUIRE(hitOn(hiphop, Snare, b + 12));
            REQUIRE(hitOn(dubstep, Snare, b + 8));    // half-time
            REQUIRE_FALSE(hitOn(dubstep, Snare, b + 4));
            for (int s : { 0, 4, 8, 12 })
            {
                REQUIRE(hitOn(trance, Kick, b + s));
                REQUIRE(hitOn(hardstyle, Kick, b + s));
                REQUIRE(hitOn(festival, Kick, b + s));
            }
            for (int s : { 2, 6, 10, 14 })
                REQUIRE(hitOn(trance, Sub, b + s));   // offbeat bass
        }
    }
    CHECK(std::string(styleName(Style::Festival)) == "Festival"); // MSVC: doctest cannot print string_view
    CHECK(static_cast<int>(Style::kCount) == 17);
}

TEST_CASE("Generator: higher density means more hits on average")
{
    int sparse = 0, busy = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        Pattern a, b;
        generate({ Style::Trap, 0.1f, 0.5f, 33, seed, 0 }, a);
        generate({ Style::Trap, 0.95f, 0.5f, 33, seed, 0 }, b);
        sparse += countHits(a.lanes[Kick]) + countHits(a.lanes[Hat]);
        busy += countHits(b.lanes[Kick]) + countHits(b.lanes[Hat]);
    }
    CHECK(busy > sparse * 1.3);
}

TEST_CASE("Generator: locked lanes are never touched, and locking does not change other lanes")
{
    Pattern base;
    generate({ Style::Drill, 0.5f, 0.7f, 33, 5, 0 }, base);

    Pattern locked = base;
    const std::uint32_t mask = (1u << Kick) | (1u << Sub);
    generate({ Style::Drill, 0.5f, 0.7f, 33, 99, mask }, locked);
    CHECK(lanesEqual(locked.lanes[Kick], base.lanes[Kick]));
    CHECK(lanesEqual(locked.lanes[Sub], base.lanes[Sub]));

    Pattern unlocked = base;
    generate({ Style::Drill, 0.5f, 0.7f, 33, 99, 0 }, unlocked);
    CHECK(lanesEqual(locked.lanes[Hat], unlocked.lanes[Hat])); // per-lane random streams

    Pattern mutated = base;
    mutate(mutated, 1.0f, 3, mask);
    CHECK(lanesEqual(mutated.lanes[Kick], base.lanes[Kick]));
    addFill(mutated, 1.0f, 3, 1u << Snare);
    CHECK(lanesEqual(mutated.lanes[Snare], [&] { Pattern m = base; mutate(m, 1.0f, 3, mask); return m.lanes[Snare]; }()));
}

TEST_CASE("Generator: 808 notes follow the kick and stay in the root's minor pentatonic")
{
    for (std::uint64_t seed = 1; seed < 30; ++seed)
    {
        Pattern p;
        generate({ Style::Trap, 0.6f, 0.8f, 33, seed, 0 }, p);
        const Lane& sub = p.lanes[Sub];
        const Lane& kick = p.lanes[Kick];
        REQUIRE(sub.steps[0].on);
        CHECK(sub.steps[0].note == 33);
        for (int i = 0; i < sub.length; ++i)
        {
            const Step& s = sub.steps[static_cast<std::size_t>(i)];
            if (!s.on)
                continue;
            REQUIRE(kick.steps[static_cast<std::size_t>(i)].on);
            const int pc = ((s.note - 33) % 12 + 12) % 12;
            REQUIRE((pc == 0 || pc == 3 || pc == 5 || pc == 7 || pc == 10));
        }
    }
}

TEST_CASE("Generator: mutate changes something but keeps every downbeat")
{
    Pattern base;
    generate({ Style::House, 0.5f, 0.5f, 33, 11, 0 }, base);
    int changed = 0;
    for (std::uint64_t seed = 1; seed <= 10; ++seed)
    {
        Pattern m = base;
        mutate(m, 0.8f, seed, 0);
        changed += patternsEqual(m, base) ? 0 : 1;
        for (int bar = 0; bar < 2; ++bar)
            REQUIRE(hitOn(m, Kick, bar * 16));
    }
    CHECK(changed >= 8);
}

TEST_CASE("Sequencer: ratchets split a step into even repeats")
{
    Sequencer seq;
    seq.pattern() = Pattern {};
    seq.pattern().lanes[2].length = 16;
    seq.pattern().lanes[2].steps[0] = Step { true, 1.0f, 1.0f, 0.0f, -1, 4 };
    std::array<NoteEvent, 32> ev {};
    const int n = seq.collect(Transport { true, 120.0, 0.0 }, 48000.0, 6000, ev.data(), 32);
    REQUIRE(n == 4);
    CHECK(ev[0].offset == 0);
    CHECK(ev[1].offset == 1500);
    CHECK(ev[2].offset == 3000);
    CHECK(ev[3].offset == 4500);
    CHECK(ev[1].velocity < ev[0].velocity);
}

TEST_CASE("Pattern text format round-trips exactly")
{
    Pattern p;
    generate({ Style::UKGarage, 0.7f, 0.9f, 36, 42, 0 }, p);
    p.lanes[9].length = 5; // a polymetric lane
    p.lanes[9].steps[2] = Step { true, 0.5f, 0.25f, -0.125f, 60, 3 };

    const std::string text = serializePattern(p);
    Pattern q;
    REQUIRE(deserializePattern(text, q));
    CHECK(patternsEqual(p, q));
    CHECK(serializePattern(q) == text);

    Pattern untouched = q;
    CHECK_FALSE(deserializePattern("zdpattern 2\n", q));
    CHECK_FALSE(deserializePattern("zdpattern 1\nlane 99 16\n", q));
    CHECK_FALSE(deserializePattern("zdpattern 1\nstep 0 1 1 0 -1 1\n", q)); // step before lane
    CHECK(patternsEqual(q, untouched));
}

TEST_CASE("Engine: submitted patterns reach the audio thread, also under contention")
{
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 256);
    loadDefaultKit(*e);

    Pattern p;
    generate({ Style::Techno, 0.5f, 0.5f, 33, 1, 0 }, p);
    REQUIRE(e->submitPattern(p));
    std::vector<float> l(256), r(256);
    e->process(Transport { true, 130.0, 0.0 }, nullptr, 0, l.data(), r.data(), 256);
    CHECK(patternsEqual(e->sequencer().pattern(), p));

    // One thread submits while the "audio" thread processes; the last one submitted wins.
    std::atomic<bool> stop { false };
    Pattern last;
    std::thread writer([&] {
        for (std::uint64_t seed = 2; seed < 300; ++seed)
        {
            Pattern q;
            generate({ Style::Trap, 0.5f, 0.5f, 33, seed, 0 }, q);
            while (!e->submitPattern(q)) {}
            last = q;
        }
        stop = true;
    });
    InternalClock clock(140.0);
    while (!stop)
        e->process(clock.next(48000.0, 256), nullptr, 0, l.data(), r.data(), 256);
    writer.join();
    e->process(clock.next(48000.0, 256), nullptr, 0, l.data(), r.data(), 256);
    CHECK(patternsEqual(e->sequencer().pattern(), last));
    CHECK(zd::test::allFinite(l));
}

TEST_CASE("Style kits: in range, tuned to the root, and every style renders without clipping")
{
    for (int s = 0; s < static_cast<int>(Style::kCount); ++s)
    {
        const auto style = static_cast<Style>(s);
        const StyleKit kit = styleKit(style);
        CAPTURE(std::string(styleName(style)));
        CHECK(kit.kick.tuneHz >= 30.0f);
        CHECK(kit.kick.tuneHz <= 120.0f);
        CHECK(kit.kick.decay > 0.04f);
        CHECK(kit.snare.tuneHz >= 100.0f);
        CHECK(kit.snare.tuneHz <= 400.0f);
        CHECK(kit.sub.decay >= 0.1f);
        CHECK(kit.perc.ratio >= 0.25f);
        for (float db : kit.levelDb)
            CHECK(db <= 6.0f);

        auto e = std::make_unique<Engine>();
        e->prepare(48000.0, 512);
        loadDefaultKit(*e);
        e->setMasterGainDb(Engine::kDefaultMasterDb);
        applyKit(kit, *e, kit.rootNote);
        {
            // The kick body sits on root + interval in some octave, near the guide's tuning.
            const double semis = 12.0 * std::log2(e->part(0).kick.tuneHz / noteToHz(kit.rootNote));
            const int pc = ((static_cast<int>(std::lround(semis)) % 12) + 12) % 12;
            CHECK(pc == kit.kickInterval % 12);
            CHECK(std::abs(semis - std::round(semis)) < 1.0e-3);
            CHECK(std::abs(std::log2(e->part(0).kick.tuneHz / kit.kick.tuneHz)) <= 0.5);
        }
        CHECK(kit.rootNote >= 24);
        CHECK(kit.rootNote <= 47);
        CHECK(e->part(4).sub.tuneHz == doctest::Approx(noteToHz(kit.rootNote)));

        double peak = 0.0;
        for (std::uint64_t seed = 1; seed <= 3; ++seed)
        {
            GenerateParams p;
            p.style = style;
            p.seed = seed;
            p.density = 0.8f;
            p.complexity = 0.8f;
            generate(p, e->sequencer().pattern());
            e->reset();
            const auto range = styleTempo(style);
            InternalClock clock(0.5 * (range.low + range.high));
            std::vector<float> l(512), r(512);
            for (int b = 0; b < 48000 * 8 / 512; ++b)
            {
                e->process(clock.next(48000.0, 512), nullptr, 0, l.data(), r.data(), 512);
                peak = std::max({ peak, test::peak(l), test::peak(r) });
            }
        }
        CHECK(peak > 0.1);
        CHECK(peak < 1.0); // under 0 dBFS before any master clipper exists
    }
    CHECK(rootTunedKickHz(29, 7, 65.4f) == doctest::Approx(65.41).epsilon(0.001)); // F1 + fifth = C2
    CHECK(rootTunedKickHz(31, 7, 65.4f) == doctest::Approx(73.42).epsilon(0.001)); // G1 + fifth = D2
    CHECK(rootTunedKickHz(33, 0, 50.0f) == doctest::Approx(55.0));
}
