#include "zd/gen/Generator.h"

#include "zd/dsp/Random.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace zd::gen {

namespace {

constexpr int kSteps = 16; // one bar of 16ths; patterns are two bars

using Weights = std::array<float, kSteps>;

struct LaneDef
{
    std::uint16_t anchors = 0; ///< bit i = always hit on step i
    Weights weights {};        ///< chance of an extra hit on each step at density 0.5
    float velocity = 0.9f;     ///< base velocity of non-anchor hits
    float microOffset = 0.0f;  ///< style feel, as a fraction of a step (+ = late)
};

struct StyleDef
{
    std::string_view name;
    TempoRange tempo;
    double swing;
    std::array<LaneDef, kNumRoles> lanes;
    float ghostSnare;   ///< chance of snare ghost notes at complexity 1
    float rollChance;   ///< chance of a hat ratchet per step at complexity 1
    int rollSize;       ///< hits in a roll (2 = 32nds, 3 = triplet feel, 4 = 64ths)
    bool subFollowsKick;
};

constexpr std::uint16_t bits(std::initializer_list<int> steps)
{
    std::uint16_t b = 0;
    for (int s : steps)
        b = static_cast<std::uint16_t>(b | (1u << s));
    return b;
}

constexpr Weights uniform(float w)
{
    Weights a {};
    for (auto& x : a)
        x = w;
    return a;
}

const StyleDef& def(Style style) noexcept
{
    static const std::array<StyleDef, static_cast<std::size_t>(Style::kCount)> table { {
        { "Trap", { 130, 160 }, 0.5,
          { { { bits({ 0 }), { 0, 0, .1f, .35f, 0, 0, .25f, .5f, 0, .15f, .45f, .2f, 0, .15f, .3f, .1f }, .9f },
              { bits({ 8 }), { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .05f, 0, 0, .1f, .2f }, .8f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.45f), .6f },
              { 0, { 0, 0, 0, .1f, 0, 0, .15f, 0, 0, 0, .1f, 0, 0, .15f, 0, .1f }, .7f },
              {} } },
          .2f, .45f, 2, true },
        { "Drill", { 138, 146 }, 0.5,
          { { { bits({ 0 }), { 0, 0, 0, .3f, 0, 0, .5f, 0, .2f, 0, .5f, 0, 0, .4f, 0, .2f }, .9f },
              { bits({ 8 }), { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .3f, 0, 0, .6f, 0 }, .85f },
              { bits({ 0, 4, 8, 12 }), uniform(.35f), .6f },
              { 0, { 0, 0, .1f, 0, 0, 0, 0, .1f, 0, 0, .1f, 0, 0, 0, 0, .1f }, .7f },
              {} } },
          .15f, .5f, 3, true },
        { "Boom bap", { 84, 96 }, 0.6,
          { { { bits({ 0 }), { 0, 0, .1f, .2f, 0, 0, 0, .45f, 0, .2f, .6f, 0, 0, 0, .2f, .1f }, .9f },
              { bits({ 4, 12 }), uniform(0.f), .9f, 0.06f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.15f), .65f },
              { 0, { 0, 0, 0, 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0, 0, .15f, 0 }, .6f },
              {} } },
          .5f, 0.f, 2, false },
        { "House", { 120, 128 }, 0.54,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { bits({ 4, 12 }), uniform(0.f), .9f },
              { bits({ 2, 6, 10, 14 }), uniform(.3f), .6f },
              { 0, { 0, 0, 0, .3f, 0, 0, .2f, 0, 0, .25f, 0, .3f, 0, 0, .2f, 0 }, .65f },
              {} } },
          .1f, .05f, 2, false },
        { "Techno", { 126, 140 }, 0.5,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { 0, { 0, 0, 0, 0, .2f, 0, 0, 0, 0, 0, 0, 0, .3f, 0, 0, .1f }, .7f },
              { bits({ 2, 6, 10, 14 }), uniform(.6f), .55f },
              { 0, { 0, .2f, 0, .3f, 0, .2f, 0, .3f, 0, .2f, 0, .3f, 0, .2f, 0, .3f }, .6f },
              {} } },
          0.f, .05f, 2, false },
        { "UK garage", { 128, 136 }, 0.62,
          { { { bits({ 0 }), { 0, 0, 0, 0, 0, 0, 0, .5f, 0, .3f, .55f, 0, 0, 0, .15f, 0 }, .9f },
              { bits({ 4, 12 }), uniform(0.f), .9f },
              { bits({ 2, 6, 10, 14 }), uniform(.45f), .6f },
              { 0, { 0, 0, 0, .2f, 0, 0, 0, 0, 0, 0, 0, .2f, 0, 0, 0, .2f }, .6f },
              {} } },
          .3f, .1f, 2, true },
        { "Drum and bass", { 170, 176 }, 0.5,
          { { { bits({ 0, 10 }), { 0, 0, .1f, 0, 0, 0, .15f, .1f, 0, 0, 0, 0, 0, .2f, 0, 0 }, .9f },
              { bits({ 4, 12 }), uniform(0.f), .95f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.4f), .6f },
              { 0, { 0, 0, 0, 0, 0, 0, 0, .2f, 0, 0, 0, 0, 0, 0, .2f, .2f }, .6f },
              {} } },
          .35f, .1f, 2, true },
        { "Afrobeats", { 100, 112 }, 0.56,
          { { { bits({ 0, 8 }), { 0, 0, 0, .2f, 0, 0, .4f, .3f, 0, 0, 0, .2f, .3f, 0, .2f, 0 }, .9f },
              { 0, { 0, 0, 0, 0, .6f, 0, 0, 0, 0, 0, 0, 0, .7f, 0, 0, .1f }, .8f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.35f), .55f },
              { bits({ 0, 3, 6, 8, 11, 14 }), uniform(.1f), .75f },
              {} } },
          .2f, .05f, 2, true },
        { "Amapiano", { 110, 115 }, 0.55,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), .85f },
              { 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .8f, 0, 0, .2f }, .8f },
              { 0, uniform(.75f), .5f },
              { bits({ 3, 10 }), { 0, .2f, 0, 0, 0, .2f, .3f, 0, 0, 0, 0, 0, 0, .3f, .2f, 0 }, .7f },
              { bits({ 3, 6, 10, 14 }), { 0, .2f, 0, 0, 0, 0, 0, .3f, 0, .2f, 0, .2f, 0, 0, 0, .3f }, .85f } } },
          .15f, .05f, 2, false },
        { "Jersey club", { 136, 145 }, 0.5,
          { { { bits({ 0, 6, 8, 12, 14 }), { 0, 0, .2f, .3f, 0, 0, 0, 0, 0, 0, .4f, .3f, 0, 0, 0, 0 }, .95f },
              { bits({ 4, 12 }), uniform(0.f), .9f },
              { bits({ 0, 4, 8, 12 }), uniform(.3f), .6f },
              { 0, { 0, 0, .2f, 0, 0, 0, .2f, 0, 0, 0, .2f, 0, 0, 0, .3f, 0 }, .7f },
              {} } },
          .1f, .15f, 2, true },
        { "Reggaeton", { 88, 100 }, 0.5,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { bits({ 3, 6, 11, 14 }), uniform(0.f), .9f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.2f), .55f },
              { 0, { 0, 0, .2f, 0, 0, 0, 0, .2f, 0, 0, .2f, 0, 0, 0, 0, .2f }, .6f },
              {} } },
          .1f, 0.f, 2, true },
        { "Pop", { 100, 124 }, 0.52,
          { { { bits({ 0, 8 }), { 0, 0, 0, 0, 0, 0, .35f, 0, 0, 0, .5f, 0, 0, 0, .15f, 0 }, .9f },
              { bits({ 4, 12 }), uniform(0.f), .95f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.2f), .6f },
              { 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .3f, .2f }, .6f },
              {} } },
          .15f, .03f, 2, false },
        // Modern hip-hop: laid-back kick, 2 and 4 backbeat, 8th hats with 16th pickups, 808 under the kick.
        { "Hip-hop", { 88, 100 }, 0.55,
          { { { bits({ 0 }), { 0, 0, .15f, .25f, 0, 0, .2f, .45f, 0, .15f, .55f, .1f, 0, 0, .25f, .1f }, .9f },
              { bits({ 4, 12 }), uniform(0.f), .9f, 0.03f },
              { bits({ 0, 2, 4, 6, 8, 10, 12, 14 }), uniform(.25f), .6f },
              { 0, { 0, 0, 0, .1f, 0, 0, .1f, 0, 0, 0, 0, .1f, 0, 0, .15f, 0 }, .6f },
              {} } },
          .35f, .2f, 2, true },
        // Dubstep: half-time at 140, snare on beat 3, sparse syncopated kicks, sub under the kick.
        { "Dubstep", { 138, 150 }, 0.5,
          { { { bits({ 0 }), { 0, 0, 0, .25f, 0, 0, .2f, 0, 0, 0, .5f, .15f, 0, 0, .2f, 0 }, .95f },
              { bits({ 8 }), { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .15f, .25f }, .95f },
              { bits({ 0, 4, 8, 12 }), uniform(.3f), .55f },
              { 0, { 0, 0, .1f, 0, 0, 0, .15f, 0, 0, .1f, 0, 0, 0, .15f, 0, .1f }, .7f },
              {} } },
          .1f, .25f, 3, true },
        // Trance: four-on-the-floor, clap on 2 and 4, offbeat open hats, rolling offbeat bass.
        { "Trance", { 132, 142 }, 0.5,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { bits({ 4, 12 }), uniform(0.f), .85f },
              { bits({ 2, 6, 10, 14 }), uniform(.5f), .6f },
              { 0, { 0, .15f, 0, .2f, 0, .15f, 0, .2f, 0, .15f, 0, .2f, 0, .15f, 0, .2f }, .55f },
              { bits({ 2, 6, 10, 14 }), { 0, .3f, 0, .5f, 0, .3f, 0, .5f, 0, .3f, 0, .5f, 0, .3f, 0, .5f }, .8f } } },
          0.f, .05f, 2, false },
        // Hardstyle: hard four-on-the-floor kick, offbeat bass stabs between kicks, offbeat hats.
        { "Hardstyle", { 150, 160 }, 0.5,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { 0, { 0, 0, 0, 0, .5f, 0, 0, 0, 0, 0, 0, 0, .6f, 0, 0, .15f }, .8f },
              { bits({ 2, 6, 10, 14 }), uniform(.3f), .6f },
              { 0, { 0, 0, .15f, 0, 0, 0, .15f, 0, 0, 0, .15f, 0, 0, 0, .2f, .1f }, .65f },
              { bits({ 2, 6, 10, 14 }), { 0, 0, 0, .3f, 0, 0, 0, .3f, 0, 0, 0, .3f, 0, 0, 0, .3f }, .85f } } },
          0.f, .1f, 2, false },
        // Festival / big room EDM: four-on-the-floor, clap on 2 and 4, offbeat hats and
        // tom-style percussion pickups at the end of each bar.
        { "Festival", { 124, 130 }, 0.5,
          { { { bits({ 0, 4, 8, 12 }), uniform(0.f), 1.f },
              { bits({ 4, 12 }), uniform(0.f), .95f },
              { bits({ 2, 6, 10, 14 }), uniform(.35f), .6f },
              { 0, { 0, 0, 0, .15f, 0, 0, 0, .15f, 0, 0, 0, .2f, 0, 0, .35f, .35f }, .75f },
              {} } },
          .1f, .1f, 2, false },
    } };
    return table[static_cast<std::size_t>(std::min(style, Style::Festival))];
}

constexpr std::array<int, 5> kMinorPentatonic { 0, 3, 5, 7, 10 };

bool locked(std::uint32_t mask, int lane) noexcept { return (mask >> lane) & 1u; }

/// Scale the base chance of an extra hit by density: 0 -> none, 0.5 -> as written, 1 -> x2.2.
float densityScale(float density) noexcept
{
    return density <= 0.5f ? density * 2.0f : 1.0f + (density - 0.5f) * 2.4f;
}

void writeLane(const LaneDef& d, Role role, const GenerateParams& p, const StyleDef& style,
               dsp::Random& rng, Lane& lane) noexcept
{
    lane = Lane {};
    lane.length = 2 * kSteps;
    const float scale = densityScale(p.density);
    const float c = std::clamp(p.complexity, 0.0f, 1.0f);

    for (int bar = 0; bar < 2; ++bar)
    {
        for (int s = 0; s < kSteps; ++s)
        {
            Step& step = lane.steps[static_cast<std::size_t>(bar * kSteps + s)];
            const bool anchor = (d.anchors >> s) & 1u;
            const double draw = rng.uniform();
            // Each bar draws its extras independently, so bar 2 varies from bar 1.
            const bool hit = anchor || draw < d.weights[static_cast<std::size_t>(s)] * scale;
            if (!hit)
                continue;

            step.on = true;
            const bool downbeat = s % 4 == 0;
            step.velocity = anchor ? std::max(d.velocity, 0.85f) : d.velocity * static_cast<float>(0.8 + 0.2 * rng.uniform());
            if (downbeat && !anchor)
                step.velocity = std::min(1.0f, step.velocity + 0.1f);
            step.microOffset = d.microOffset;

            if (role == Hat)
            {
                // Hats: lighter offbeats, small human jitter, rolls on busy styles.
                if (s % 2 != 0)
                    step.velocity *= 0.8f;
                step.microOffset += static_cast<float>(rng.bipolar() * 0.04 * c);
                if (rng.uniform() < style.rollChance * c)
                    step.ratchet = style.rollSize;
            }
            // Extras at high complexity become probabilistic, so the loop evolves on its own.
            if (!anchor && c > 0.6f && rng.uniform() < (c - 0.6f))
                step.probability = 0.7f;
        }
        // Snare ghost notes sit just before the backbeats.
        if (role == Snare)
            for (int s : { 3, 7, 11, 15 })
            {
                Step& step = lane.steps[static_cast<std::size_t>(bar * kSteps + s)];
                if (!step.on && rng.uniform() < style.ghostSnare * c)
                    step = Step { true, 0.3f, 1.0f, d.microOffset, -1, 1 };
            }
    }
}

void writeSub(const GenerateParams& p, const StyleDef& style, const Lane& kick, dsp::Random& rng, Lane& sub) noexcept
{
    const LaneDef& d = style.lanes[Sub];
    if (!style.subFollowsKick)
    {
        writeLane(d, Sub, p, style, rng, sub);
    }
    else
    {
        sub = Lane {};
        sub.length = kick.length;
        for (int i = 0; i < kick.length; ++i)
        {
            const Step& k = kick.steps[static_cast<std::size_t>(i)];
            if (k.on && (i % kSteps == 0 || rng.uniform() < 0.85))
                sub.steps[static_cast<std::size_t>(i)] = Step { true, k.velocity, 1.0f, 0.0f, -1, 1 };
        }
    }

    // Notes: the root on the first hit, then pentatonic moves, more of them with complexity.
    int degree = 0;
    const float c = std::clamp(p.complexity, 0.0f, 1.0f);
    for (int i = 0; i < sub.length; ++i)
    {
        Step& s = sub.steps[static_cast<std::size_t>(i)];
        if (!s.on)
            continue;
        if (i != 0 && rng.uniform() < 0.25 + 0.5 * c)
            degree = static_cast<int>(rng.next() % kMinorPentatonic.size());
        const int octave = (degree >= 3 && rng.uniform() < 0.3) ? -12 : 0;
        s.note = p.rootNote + kMinorPentatonic.at(static_cast<std::size_t>(degree)) + octave;
    }
}

} // namespace

std::string_view styleName(Style style) noexcept { return def(style).name; }
TempoRange styleTempo(Style style) noexcept { return def(style).tempo; }

int countHits(const Lane& lane) noexcept
{
    int n = 0;
    for (int i = 0; i < lane.length; ++i)
        n += lane.steps[static_cast<std::size_t>(i)].on ? 1 : 0;
    return n;
}

void generate(const GenerateParams& p, Pattern& pattern) noexcept
{
    const StyleDef& style = def(p.style);
    dsp::Random rng(dsp::hash64(p.seed ^ (static_cast<std::uint64_t>(p.style) << 40)));

    pattern.stepBeats = 0.25;
    pattern.swing = style.swing;

    for (int role = 0; role < kNumRoles; ++role)
    {
        // Each lane draws from its own stream so locking one lane never changes the others.
        dsp::Random laneRng(dsp::hash64(rng.next() + static_cast<std::uint64_t>(role)));
        if (locked(p.lockMask, role))
            continue;
        auto& lane = pattern.lanes[static_cast<std::size_t>(role)];
        if (role == Sub)
            writeSub(p, style, pattern.lanes[Kick], laneRng, lane);
        else
            writeLane(style.lanes[static_cast<std::size_t>(role)], static_cast<Role>(role), p, style, laneRng, lane);
    }
}

void mutate(Pattern& pattern, float amount, std::uint64_t seed, std::uint32_t lockMask) noexcept
{
    dsp::Random rng(dsp::hash64(seed + 0x6D757461ull));
    const int changes = 1 + static_cast<int>(std::lround(std::clamp(amount, 0.0f, 1.0f) * 2.0f));

    for (int lane = 0; lane < kNumRoles; ++lane)
    {
        if (locked(lockMask, lane) || lane == Sub)
            continue;
        Lane& l = pattern.lanes[static_cast<std::size_t>(lane)];
        if (l.length <= 0)
            continue;

        for (int c = 0; c < changes; ++c)
        {
            const auto i = static_cast<std::size_t>(rng.next() % static_cast<std::uint64_t>(l.length));
            Step& s = l.steps[i];
            const double r = rng.uniform();
            if (i % static_cast<std::size_t>(kSteps) == 0)
                continue; // never touch the downbeat
            if (r < 0.4)
            {
                // Move a hit one step left or right.
                if (s.on)
                {
                    const auto j = (rng.uniform() < 0.5 ? i + 1 : i + static_cast<std::size_t>(l.length) - 1) % static_cast<std::size_t>(l.length);
                    if (!l.steps[j].on && j % static_cast<std::size_t>(kSteps) != 0)
                        std::swap(l.steps[i], l.steps[j]);
                }
            }
            else if (r < 0.7)
            {
                if (!s.on)
                    s = Step { true, static_cast<float>(0.5 + 0.3 * rng.uniform()), 1.0f, 0.0f, -1, 1 };
            }
            else if (r < 0.9)
            {
                s.on = false;
            }
            else if (lane == Hat && s.on)
            {
                s.ratchet = s.ratchet > 1 ? 1 : 2;
            }
        }
    }
}

void addFill(Pattern& pattern, float intensity, std::uint64_t seed, std::uint32_t lockMask) noexcept
{
    dsp::Random rng(dsp::hash64(seed + 0x66696C6Cull));
    const float k = std::clamp(intensity, 0.0f, 1.0f);
    for (int lane : { static_cast<int>(Snare), static_cast<int>(Perc) })
    {
        if (locked(lockMask, lane))
            continue;
        Lane& l = pattern.lanes[static_cast<std::size_t>(lane)];
        const int end = l.length;
        for (int i = std::max(0, end - 4); i < end; ++i)
        {
            Step& s = l.steps[static_cast<std::size_t>(i)];
            if (!s.on && rng.uniform() < 0.35 + 0.6 * k)
                s = Step { true, static_cast<float>(0.55 + 0.4 * (i - (end - 4)) / 3.0), 1.0f, 0.0f, -1, 1 };
            if (s.on && lane == Snare && rng.uniform() < 0.4 * k)
                s.ratchet = 2;
        }
    }
}

} // namespace zd::gen
