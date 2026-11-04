#include "zd/gen/Generator.h"

#include "zd/dsp/Random.h"
#include "zd/gen/StyleKit.h"

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
              { bits({ 4, 12 }), uniform(0.f), .9f },
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
              { bits({ 4, 12 }), uniform(0.f), .9f },
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

/// Feel of one lane, indexed by the step's position inside its beat: 0 = on the beat,
/// 1 = the "e", 2 = the "and", 3 = the "a".
struct LaneGroove
{
    std::array<float, 4> timing { 0, 0, 0, 0 }; ///< fraction of a step, + = late
    std::array<float, 4> accent { 1, 1, 1, 1 }; ///< velocity multiplier
};

/// The groove table: how each style sits against the grid, on top of its swing.
/// Hip-hop pulls the snare late and pushes the kick, house leans on the offbeat hat,
/// garage and amapiano drag the 16th pickups, the EDM styles stay locked to the grid.
struct GrooveDef
{
    std::array<LaneGroove, kNumRoles> lanes;
    float ghostVelocity; ///< snare ghost note velocity
    float jitter;        ///< random timing spread on hats and perc, fraction of a step
};

constexpr std::array<float, 4> kOnTheGrid { 0, 0, 0, 0 };
constexpr std::array<float, 4> kFlat { 1, 1, 1, 1 };
constexpr std::array<float, 4> kHatDown { 1.0f, .72f, .86f, .72f };   ///< downbeat-led 8ths and 16ths
constexpr std::array<float, 4> kHatOff { .72f, .6f, 1.0f, .6f };     ///< offbeat open hat leads
constexpr std::array<float, 4> kKick16 { 1.0f, .88f, .94f, .88f };   ///< 16th kick pickups a touch softer

const GrooveDef& groove(Style style) noexcept
{
    static const std::array<GrooveDef, static_cast<std::size_t>(Style::kCount)> table { {
        // Trap: programmed and tight; the feel is in hat accents and rolls, not timing.
        { { { { kOnTheGrid, kKick16 }, { kOnTheGrid, kFlat }, { kOnTheGrid, { 1.0f, .74f, .9f, .74f } },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .25f, .02f },
        // Drill: snare a hair late, sliding hats.
        { { { { kOnTheGrid, kKick16 }, { { .02f, .02f, .02f, .02f }, kFlat }, { kOnTheGrid, kHatDown },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .3f, .03f },
        // Boom bap: kick pushes, snare lays back, hats drag the 16ths. Loose hands.
        { { { { { -.03f, -.02f, -.03f, -.02f }, kKick16 }, { { .08f, .06f, .08f, .06f }, kFlat },
              { { 0, .03f, 0, .03f }, { 1.0f, .6f, .85f, .6f } }, { { 0, .03f, .02f, .03f }, kFlat },
              { kOnTheGrid, kFlat } } }, .35f, .06f },
        // House: clap slightly late, offbeat hat leans forward and carries the accent.
        { { { { kOnTheGrid, kFlat }, { { .02f, .02f, .02f, .02f }, kFlat }, { { 0, 0, -.03f, 0 }, kHatOff },
              { { 0, .02f, 0, .02f }, { 1.0f, .85f, .95f, .85f } }, { kOnTheGrid, kFlat } } }, .3f, .03f },
        // Techno: machine tight, accent on the offbeat hat and the perc 16ths.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatOff },
              { kOnTheGrid, { .8f, 1.0f, .8f, 1.0f } }, { kOnTheGrid, kFlat } } }, .3f, .015f },
        // UK garage: shuffled 16ths dragged late, snare a touch behind.
        { { { { kOnTheGrid, kKick16 }, { { .02f, .02f, .02f, .02f }, kFlat },
              { { 0, .04f, 0, .04f }, { .85f, .65f, 1.0f, .7f } }, { { 0, .04f, 0, .04f }, kFlat },
              { kOnTheGrid, kFlat } } }, .3f, .04f },
        // Drum and bass: tight snare, ghosted breaks, hats dragged slightly.
        { { { { kOnTheGrid, kKick16 }, { kOnTheGrid, kFlat }, { { 0, .03f, 0, .03f }, { 1.0f, .6f, .85f, .65f } },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .25f, .03f },
        // Afrobeats: perc pushed into the pocket, rim slightly late, shaker accents.
        { { { { kOnTheGrid, kKick16 }, { { .02f, .02f, .02f, .02f }, kFlat }, { kOnTheGrid, { 1.0f, .65f, .85f, .7f } },
              { { 0, .04f, .02f, .04f }, { 1.0f, .8f, .9f, .85f } }, { kOnTheGrid, kFlat } } }, .3f, .05f },
        // Amapiano: shaker 16ths with the "and" accented, log drum dragged behind the beat.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { { 0, .03f, 0, .03f }, { .7f, .55f, 1.0f, .6f } },
              { { 0, .03f, 0, .03f }, kFlat }, { { .03f, .03f, .03f, .03f }, kFlat } } }, .3f, .04f },
        // Jersey club: tight and bouncy.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, { 1.0f, .7f, .9f, .7f } },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .3f, .02f },
        // Reggaeton: dembow on the grid.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, { 1.0f, .65f, .85f, .65f } },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .3f, .02f },
        // Pop: clean, light accents.
        { { { { kOnTheGrid, kKick16 }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatDown },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .28f, .02f },
        // Hip-hop: a milder boom bap pocket.
        { { { { { -.02f, -.01f, -.02f, -.01f }, kKick16 }, { { .05f, .04f, .05f, .04f }, kFlat },
              { { 0, .02f, 0, .02f }, { 1.0f, .65f, .85f, .65f } }, { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } },
          .32f, .04f },
        // Dubstep: tight; the weight comes from the half-time snare.
        { { { { kOnTheGrid, kKick16 }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatDown },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .25f, .02f },
        // Trance: on the grid, offbeat hat leads.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatOff },
              { kOnTheGrid, { .8f, .9f, .8f, .9f } }, { kOnTheGrid, kFlat } } }, .3f, .01f },
        // Hardstyle: on the grid, offbeat hat leads.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatOff },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .3f, .01f },
        // Festival: on the grid, offbeat hat leads.
        { { { { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat }, { kOnTheGrid, kHatOff },
              { kOnTheGrid, kFlat }, { kOnTheGrid, kFlat } } }, .3f, .015f },
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
    const float g = std::clamp(p.groove, 0.0f, 1.0f);
    const GrooveDef& feel = groove(p.style);
    const LaneGroove& lg = feel.lanes[static_cast<std::size_t>(role)];
    const bool loose = role == Hat || role == Perc;

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
            const auto pos = static_cast<std::size_t>(s % 4);
            step.velocity = anchor ? std::max(d.velocity, 0.85f) : d.velocity * static_cast<float>(0.8 + 0.2 * rng.uniform());
            if (downbeat && !anchor)
                step.velocity = std::min(1.0f, step.velocity + 0.1f);
            step.velocity *= 1.0f + (lg.accent[pos] - 1.0f) * g;
            step.microOffset = lg.timing[pos] * g;

            // Hats and perc: small human jitter, scaled by the style's looseness.
            const double jitter = rng.bipolar();
            if (loose)
                step.microOffset += static_cast<float>(jitter * feel.jitter * g * (0.5 + 0.5 * c));

            if (role == Hat && rng.uniform() < style.rollChance * c)
                step.ratchet = style.rollSize;
            // Extras at high complexity become probabilistic, so the loop evolves on its own.
            if (!anchor && c > 0.6f && rng.uniform() < (c - 0.6f))
                step.probability = 0.7f;
            step.microOffset = std::clamp(step.microOffset, -0.5f, 0.5f);
        }
        // Snare ghost notes sit just before the backbeats.
        if (role == Snare)
            for (int s : { 3, 7, 11, 15 })
            {
                Step& step = lane.steps[static_cast<std::size_t>(bar * kSteps + s)];
                if (!step.on && rng.uniform() < style.ghostSnare * c)
                    step = Step { true, feel.ghostVelocity, 1.0f, lg.timing[3] * g, -1, 1 };
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
                sub.steps[static_cast<std::size_t>(i)] = Step { true, k.velocity, 1.0f, k.microOffset, -1, 1 }; // locked to the kick
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

    // Deliberate slides (the guide): in trap, drill and amapiano, about 30% of bars move the
    // bar's last 808 hit to a fourth or fifth below the root or the octave above. The 808 is
    // mono with glide, so it slides there from the note still ringing.
    if (p.style == Style::Trap || p.style == Style::Drill || p.style == Style::Amapiano)
    {
        constexpr std::array<int, 3> kSlides { -5, -7, 12 };
        for (int bar = 0; bar * kSteps < sub.length; ++bar)
        {
            int last = -1, hits = 0;
            for (int i = bar * kSteps; i < std::min(sub.length, (bar + 1) * kSteps); ++i)
                if (sub.steps[static_cast<std::size_t>(i)].on)
                {
                    last = i;
                    ++hits;
                }
            const double draw = rng.uniform();
            const auto pick = static_cast<std::size_t>(rng.next() % kSlides.size());
            if (hits >= 2 && last % kSteps != 0 && draw < 0.3)
                sub.steps[static_cast<std::size_t>(last)].note = p.rootNote + kSlides.at(pick);
        }
    }
}

/// Melodic kick (hardstyle, festival): every kick takes a note. The first half bar sits on
/// the kick's root-tuned note; each later half bar moves with the chords of the natural
/// minor key (VI, III, VII, iv) or stays, so the kick plays the progression.
void writeKickMelody(const GenerateParams& p, dsp::Random& rng, Lane& kick) noexcept
{
    const StyleKit kit = styleKit(p.style);
    const double hz = rootTunedKickHz(p.rootNote, kit.kickInterval, kit.kick.tuneHz);
    const int base = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
    constexpr std::array<int, 6> kMoves { 0, 0, -4, 3, -2, 5 };
    int move = 0;
    for (int i = 0; i < kick.length; ++i)
    {
        if (i % (kSteps / 2) == 0)
            move = i == 0 ? 0 : kMoves.at(static_cast<std::size_t>(rng.next() % kMoves.size()));
        Step& s = kick.steps[static_cast<std::size_t>(i)];
        if (s.on)
            s.note = base + move;
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
    pattern.swing = 0.5 + (style.swing - 0.5) * static_cast<double>(std::clamp(p.groove, 0.0f, 1.0f));
    pattern.sweep = Sweep {}; // a fresh beat drops any build sweep

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
        if (role == Kick && (p.style == Style::Hardstyle || p.style == Style::Festival))
            writeKickMelody(p, laneRng, lane);
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

void euclid(Lane& lane, int hits, int rotate) noexcept
{
    const int n = std::clamp(lane.length, 1, kMaxStepsPerLane);
    const int k = std::clamp(hits, 0, n);
    for (int i = 0; i < n; ++i)
        lane.steps[static_cast<std::size_t>(i)] = Step {};
    for (int j = 0; j < k; ++j)
    {
        // The j-th of k hits lands on ceil(j * n / k): Bjorklund's spacing, phased so the
        // longest gaps come first (3 over 8 is the tresillo, x..x..x.).
        const int at = ((j * n + k - 1) / k + rotate % n + n) % n;
        lane.steps[static_cast<std::size_t>(at)] = Step { true, j == 0 ? 0.95f : 0.8f, 1.0f, 0.0f, -1, 1 };
    }
}

void humanize(Pattern& pattern, float amount, std::uint64_t seed, std::uint32_t lockMask) noexcept
{
    const float a = std::clamp(amount, 0.0f, 1.0f);
    if (a <= 0.0f)
        return;
    for (int lane = 0; lane < kMaxParts; ++lane)
    {
        if (locked(lockMask, lane))
            continue;
        dsp::Random rng(dsp::hash64(seed ^ (0x4855u + static_cast<std::uint64_t>(lane) * 0x9E3779B97F4A7C15ull)));
        Lane& l = pattern.lanes[static_cast<std::size_t>(lane)];
        for (int i = 0; i < std::clamp(l.length, 0, kMaxStepsPerLane); ++i)
        {
            Step& st = l.steps[static_cast<std::size_t>(i)];
            const double t = rng.bipolar(), v = rng.bipolar(); // drawn for every step: stable streams
            if (!st.on)
                continue;
            st.microOffset = std::clamp(st.microOffset + static_cast<float>(0.06 * a * t), -0.5f, 0.5f);
            st.velocity = std::clamp(st.velocity * (1.0f + static_cast<float>(0.12 * a * v)), 0.05f, 1.0f);
        }
    }
}

void makeBuild(Pattern& pattern, int bars, std::uint32_t lockMask) noexcept
{
    const int n = std::clamp(bars, 1, kMaxStepsPerLane / kSteps) * kSteps;
    const int breath = n >= 2 * kSteps ? 2 : 0; // an 8th of silence before the drop
    // The whole bus thins out with a rising high-pass over the build, back to full at the drop.
    pattern.sweep = Sweep { n, 30.0f, 1500.0f, 0.5f };
    for (int role = 0; role < kNumRoles; ++role)
    {
        if (locked(lockMask, role))
            continue;
        Lane& lane = pattern.lanes[static_cast<std::size_t>(role)];
        lane = Lane {};
        lane.length = n;
        for (int i = 0; i < n - breath; ++i)
        {
            const int segment = i * 4 / n; // 0..3: 8ths, 16ths, 32nds, 64ths
            const float ramp = static_cast<float>(i) / static_cast<float>(std::max(1, n - breath - 1));
            Step& st = lane.steps[static_cast<std::size_t>(i)];
            switch (role)
            {
                case Snare:
                    if (segment > 0 || i % 2 == 0)
                    {
                        st.on = true;
                        st.velocity = 0.35f + 0.65f * ramp;
                        st.ratchet = segment == 2 ? 2 : segment == 3 ? kMaxRatchet : 1;
                    }
                    break;
                case Kick:
                    if (segment < 3 && i % 4 == 0)
                        st = Step { true, 0.95f, 1.0f, 0.0f, -1, 1 };
                    break;
                case Hat:
                    if (i % 4 == 2)
                        st = Step { true, 0.5f + 0.3f * ramp, 1.0f, 0.0f, -1, 1 };
                    break;
                default: break; // perc and 808 rest until the drop
            }
        }
    }
}

} // namespace zd::gen
