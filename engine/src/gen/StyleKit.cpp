#include "zd/gen/StyleKit.h"

#include <algorithm>
#include <cmath>

namespace zd::gen {

namespace {

// One row per style, in Style order. Times in ms, levels in dB. Source: the genre tuning
// guide's "Kit presets" tables. Its hardstyle kick band-pass is left out: the strip filter
// is in series, so it would remove the body; the drive stays.
struct StripRow
{
    float attack, sustain;
    FilterMode mode;
    float cutoff, drive;
};

struct Row
{
    int kInterval;
    float kTune, kSweep, kSweepMs, kDecay, kClick, kDrive;
    float sTune, sBody, sWire, sTone, sSnappy, sDrive;
    float hPitch, hDecay;
    float pTune, pRatio, pIndex, pIndexDecay, pDecay, pFeedback;
    float bDecay, bGlide, bPunch, bDrive;
    int root;
    float lk, ls, lh, lp, lb;
    StripRow kick, snare;
    FilterMode hatMode;
    float hatCutoff, hatDrive;
    FilterMode percMode;
    float percCutoff;
    float trimDb; ///< added to every level so the style peaks under 0 dBFS
};

constexpr std::array<Row, static_cast<std::size_t>(Style::kCount)> kRows { {
    // Trap
    { 7, 65.4f, 5.0f, 20.0f, 200.0f, 0.6f, 0.35f, 220.0f, 90.0f, 220.0f, 4500.0f, 0.75f, 0.3f, 2.0f, 60.0f, 1800.0f, 2.76f, 4.0f, 8.0f, 60.0f, 0.1f, 1800.0f, 60.0f, 1.5f, 0.55f, 29, -3.0f, -4.0f, -12.0f, -14.0f, -2.0f,
      { 0.3f, -0.4f, FilterMode::Off, 0.0f, 0.0f }, { 0.4f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.0f, FilterMode::Off, 0.0f, -3.0f },
    // Drill
    { 12, 73.4f, 4.0f, 18.0f, 170.0f, 0.55f, 0.3f, 240.0f, 80.0f, 180.0f, 5000.0f, 0.7f, 0.35f, 3.0f, 50.0f, 900.0f, 1.41f, 3.0f, 10.0f, 90.0f, 0.15f, 1400.0f, 120.0f, 1.2f, 0.6f, 26, -3.0f, -4.0f, -12.0f, -14.0f, -2.0f,
      { 0.3f, -0.4f, FilterMode::Off, 0.0f, 0.0f }, { 0.5f, -0.3f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.0f, FilterMode::Off, 0.0f, -5.0f },
    // BoomBap
    { 0, 55.0f, 2.5f, 35.0f, 380.0f, 0.45f, 0.45f, 200.0f, 180.0f, 260.0f, 3000.0f, 0.55f, 0.45f, -2.0f, 90.0f, 2400.0f, 3.5f, 6.0f, 15.0f, 120.0f, 0.4f, 600.0f, 0.0f, 0.8f, 0.3f, 33, 0.0f, -1.0f, -13.0f, -15.0f, -14.0f,
      { 0.2f, 0.0f, FilterMode::LowPass, 10000.0f, 0.3f }, { 0.3f, 0.1f, FilterMode::LowPass, 10000.0f, 0.3f }, FilterMode::LowPass, 9000.0f, 0.0f, FilterMode::LowPass, 9000.0f, -1.5f },
    // House
    { 0, 55.0f, 3.5f, 30.0f, 450.0f, 0.5f, 0.2f, 230.0f, 60.0f, 260.0f, 1800.0f, 0.85f, 0.2f, 0.0f, 120.0f, 2600.0f, 2.4f, 5.0f, 10.0f, 70.0f, 0.35f, 250.0f, 0.0f, 0.5f, 0.2f, 33, 0.0f, -5.0f, -11.0f, -13.0f, -8.0f,
      { 0.2f, -0.1f, FilterMode::Off, 0.0f, 0.0f }, { 0.2f, 0.0f, FilterMode::HighPass, 200.0f, 0.0f }, FilterMode::HighPass, 7000.0f, 0.0f, FilterMode::Off, 0.0f, -1.0f },
    // Techno
    { 0, 43.7f, 4.0f, 35.0f, 600.0f, 0.55f, 0.55f, 250.0f, 50.0f, 200.0f, 2200.0f, 0.8f, 0.4f, -1.0f, 70.0f, 3400.0f, 2.9f, 9.0f, 300.0f, 1200.0f, 0.5f, 350.0f, 0.0f, 0.8f, 0.4f, 29, 0.0f, -7.0f, -10.0f, -12.0f, -8.0f,
      { 0.1f, 0.2f, FilterMode::Off, 0.0f, 0.0f }, { 0.0f, 0.2f, FilterMode::HighPass, 250.0f, 0.0f }, FilterMode::HighPass, 5000.0f, 0.0f, FilterMode::Off, 0.0f, -0.5f },
    // UKGarage
    { 0, 49.0f, 3.0f, 25.0f, 300.0f, 0.5f, 0.25f, 330.0f, 60.0f, 120.0f, 5500.0f, 0.45f, 0.3f, 1.0f, 45.0f, 1700.0f, 1.6f, 3.0f, 8.0f, 50.0f, 0.1f, 700.0f, 40.0f, 1.0f, 0.35f, 31, -1.0f, -4.0f, -11.0f, -12.0f, -5.0f,
      { 0.3f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, { 0.4f, -0.3f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.2f, FilterMode::Off, 0.0f, -1.5f },
    // DrumAndBass
    { 7, 61.7f, 5.0f, 15.0f, 260.0f, 0.7f, 0.35f, 220.0f, 150.0f, 260.0f, 4500.0f, 0.65f, 0.4f, 2.0f, 45.0f, 3200.0f, 2.9f, 8.0f, 200.0f, 900.0f, 0.5f, 900.0f, 30.0f, 1.0f, 0.4f, 28, -2.0f, 0.0f, -12.0f, -14.0f, -6.0f,
      { 0.5f, -0.3f, FilterMode::Off, 0.0f, 0.0f }, { 0.5f, -0.1f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 7000.0f, 0.0f, FilterMode::Off, 0.0f, -3.5f },
    // Afrobeats
    { 0, 55.0f, 2.0f, 30.0f, 300.0f, 0.3f, 0.15f, 380.0f, 45.0f, 80.0f, 6000.0f, 0.35f, 0.2f, 1.0f, 40.0f, 330.0f, 1.5f, 1.2f, 15.0f, 220.0f, 0.0f, 700.0f, 50.0f, 0.8f, 0.25f, 33, -3.0f, -6.0f, -12.0f, -8.0f, -5.0f,
      { 0.0f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, { 0.3f, -0.4f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.0f, FilterMode::Off, 0.0f, -0.5f },
    // Amapiano
    { 7, 65.4f, 1.5f, 30.0f, 280.0f, 0.2f, 0.1f, 260.0f, 70.0f, 160.0f, 3500.0f, 0.6f, 0.2f, 2.0f, 35.0f, 700.0f, 2.0f, 2.5f, 10.0f, 120.0f, 0.1f, 450.0f, 40.0f, 3.0f, 0.5f, 29, -5.0f, -6.0f, -12.0f, -9.0f, -2.0f,
      { -0.2f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, { 0.1f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 7000.0f, 0.0f, FilterMode::Off, 0.0f, 2.0f },
    // JerseyClub
    { 7, 73.4f, 4.0f, 18.0f, 220.0f, 0.65f, 0.3f, 240.0f, 50.0f, 200.0f, 2000.0f, 0.85f, 0.35f, 2.0f, 40.0f, 1400.0f, 0.5f, 6.0f, 60.0f, 180.0f, 0.2f, 900.0f, 40.0f, 1.2f, 0.45f, 31, -1.0f, -3.0f, -12.0f, -10.0f, -5.0f,
      { 0.4f, -0.3f, FilterMode::Off, 0.0f, 0.0f }, { 0.5f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.0f, FilterMode::Off, 0.0f, -6.0f },
    // Reggaeton
    { 0, 55.0f, 3.0f, 25.0f, 280.0f, 0.5f, 0.25f, 230.0f, 90.0f, 160.0f, 4000.0f, 0.6f, 0.3f, 0.0f, 50.0f, 520.0f, 1.5f, 2.0f, 20.0f, 200.0f, 0.05f, 800.0f, 30.0f, 1.0f, 0.35f, 33, -1.0f, -3.0f, -12.0f, -11.0f, -5.0f,
      { 0.3f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, { 0.4f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 6000.0f, 0.0f, FilterMode::Off, 0.0f, -2.5f },
    // Pop
    { 0, 55.0f, 3.5f, 25.0f, 350.0f, 0.55f, 0.25f, 210.0f, 140.0f, 300.0f, 4500.0f, 0.6f, 0.2f, 1.0f, 60.0f, 2000.0f, 2.76f, 5.0f, 6.0f, 50.0f, 0.2f, 1000.0f, 30.0f, 1.0f, 0.3f, 33, -1.0f, -3.0f, -12.0f, -13.0f, -5.0f,
      { 0.3f, -0.2f, FilterMode::Off, 0.0f, 0.0f }, { 0.3f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 7000.0f, 0.0f, FilterMode::Off, 0.0f, 1.5f },
    // HipHop
    { 0, 49.0f, 2.0f, 45.0f, 650.0f, 0.4f, 0.3f, 190.0f, 140.0f, 280.0f, 3500.0f, 0.65f, 0.35f, 0.0f, 80.0f, 540.0f, 1.48f, 1.5f, 40.0f, 250.0f, 0.0f, 1200.0f, 30.0f, 0.8f, 0.35f, 31, -1.0f, -2.0f, -12.0f, -16.0f, -4.0f,
      { 0.2f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, { 0.4f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 5000.0f, 0.0f, FilterMode::Off, 0.0f, -2.0f },
    // Dubstep
    { 7, 65.4f, 6.0f, 15.0f, 240.0f, 0.85f, 0.5f, 200.0f, 120.0f, 220.0f, 5500.0f, 0.7f, 0.6f, 1.0f, 40.0f, 1200.0f, 3.14f, 8.0f, 20.0f, 120.0f, 0.3f, 900.0f, 40.0f, 1.0f, 0.5f, 29, -1.0f, -1.0f, -13.0f, -14.0f, -5.0f,
      { 0.5f, -0.4f, FilterMode::HighPass, 40.0f, 0.0f }, { 0.6f, -0.3f, FilterMode::HighPass, 150.0f, 0.0f }, FilterMode::HighPass, 500.0f, 0.0f, FilterMode::HighPass, 500.0f, -6.0f },
    // Trance
    { 0, 55.0f, 4.0f, 25.0f, 380.0f, 0.6f, 0.35f, 220.0f, 70.0f, 280.0f, 2500.0f, 0.8f, 0.25f, 2.0f, 90.0f, 3600.0f, 3.1f, 8.0f, 80.0f, 400.0f, 0.5f, 200.0f, 0.0f, 0.6f, 0.3f, 33, 0.0f, -6.0f, -10.0f, -13.0f, -6.0f,
      { 0.3f, -0.3f, FilterMode::Off, 0.0f, 0.0f }, { 0.2f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 8000.0f, 0.0f, FilterMode::Off, 0.0f, -2.0f },
    // Hardstyle
    { 7, 65.4f, 8.0f, 60.0f, 750.0f, 0.9f, 0.95f, 210.0f, 100.0f, 300.0f, 5000.0f, 0.75f, 0.5f, 4.0f, 50.0f, 2500.0f, 3.5f, 7.0f, 15.0f, 80.0f, 0.4f, 300.0f, 0.0f, 0.0f, 0.0f, 29, 0.0f, -5.0f, -14.0f, -15.0f, -48.0f,
      { 0.2f, 0.3f, FilterMode::Off, 0.0f, 0.5f }, { 0.4f, 0.0f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 8000.0f, 0.0f, FilterMode::Off, 0.0f, -2.0f },
    // Festival
    { 7, 65.4f, 6.0f, 30.0f, 500.0f, 0.8f, 0.6f, 200.0f, 110.0f, 350.0f, 4500.0f, 0.8f, 0.45f, 3.0f, 70.0f, 3000.0f, 2.9f, 7.0f, 50.0f, 300.0f, 0.45f, 600.0f, 0.0f, 1.0f, 0.4f, 29, 0.0f, -3.0f, -12.0f, -13.0f, -8.0f,
      { 0.4f, 0.0f, FilterMode::Off, 0.0f, 0.3f }, { 0.4f, 0.2f, FilterMode::Off, 0.0f, 0.0f }, FilterMode::HighPass, 7000.0f, 0.0f, FilterMode::Off, 0.0f, -3.5f },
} };

StripParams strip(const StripRow& r) noexcept
{
    StripParams p;
    p.attack = r.attack;
    p.sustain = r.sustain;
    p.filterMode = r.mode;
    if (r.mode != FilterMode::Off)
        p.cutoffHz = r.cutoff;
    p.drive = r.drive;
    return p;
}

// Sidechain per style, in Style order: 808 duck, perc duck, release ms. Four-on-the-floor
// styles pump; trap-family styles take a short duck so kick and 808 don't stack on the hit.
struct DuckRow
{
    float sub, perc, releaseMs;
};
constexpr std::array<DuckRow, static_cast<std::size_t>(Style::kCount)> kDucks { {
    { 0.50f, 0.0f, 80 },  // Trap
    { 0.50f, 0.0f, 80 },  // Drill
    { 0.30f, 0.0f, 100 }, // Boom bap
    { 0.70f, 0.2f, 160 }, // House
    { 0.70f, 0.2f, 140 }, // Techno
    { 0.60f, 0.0f, 120 }, // UK garage
    { 0.50f, 0.0f, 90 },  // Drum and bass
    { 0.40f, 0.0f, 100 }, // Afrobeats
    { 0.40f, 0.4f, 120 }, // Amapiano (perc is the log drum)
    { 0.40f, 0.0f, 90 },  // Jersey club
    { 0.40f, 0.0f, 100 }, // Reggaeton
    { 0.40f, 0.0f, 100 }, // Pop
    { 0.40f, 0.0f, 90 },  // Hip-hop
    { 0.60f, 0.0f, 120 }, // Dubstep
    { 0.80f, 0.3f, 180 }, // Trance
    { 0.00f, 0.3f, 150 }, // Hardstyle (no 808)
    { 0.80f, 0.3f, 180 }, // Festival
} };

} // namespace

StyleKit styleKit(Style style) noexcept
{
    const auto index = std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::kCount) - 1);
    const Row& r = kRows[static_cast<std::size_t>(index)];
    StyleKit k;
    k.kick.tuneHz = r.kTune;
    k.kick.pitchAmount = r.kSweep;
    k.kick.pitchDecay = r.kSweepMs * 0.001f;
    k.kick.decay = r.kDecay * 0.001f;
    k.kick.click = r.kClick;
    k.kick.drive = r.kDrive;

    k.snare.tuneHz = r.sTune;
    k.snare.toneDecay = r.sBody * 0.001f;
    k.snare.noiseDecay = r.sWire * 0.001f;
    k.snare.noiseTone = r.sTone;
    k.snare.snappy = r.sSnappy;
    k.snare.drive = r.sDrive;

    k.hat.pitchSemitones = r.hPitch;
    k.hat.decay = r.hDecay * 0.001f;

    k.perc.tuneHz = r.pTune;
    k.perc.ratio = r.pRatio;
    k.perc.index = r.pIndex;
    k.perc.indexDecay = r.pIndexDecay * 0.001f;
    k.perc.decay = r.pDecay * 0.001f;
    k.perc.feedback = r.pFeedback;

    k.sub.decay = r.bDecay * 0.001f;
    k.sub.glide = r.bGlide * 0.001f;
    k.sub.punch = r.bPunch;
    k.sub.drive = r.bDrive;

    k.strips[0] = strip(r.kick);
    k.strips[1] = strip(r.snare);
    k.strips[2].filterMode = r.hatMode;
    k.strips[2].cutoffHz = r.hatCutoff;
    k.strips[2].drive = r.hatDrive;
    if (r.percMode != FilterMode::Off)
    {
        k.strips[3].filterMode = r.percMode;
        k.strips[3].cutoffHz = r.percCutoff;
    }
    k.levelDb = { r.lk + r.trimDb, r.ls + r.trimDb, r.lh + r.trimDb, r.lp + r.trimDb, r.lb + r.trimDb };
    k.rootNote = r.root;
    const DuckRow& d = kDucks[static_cast<std::size_t>(index)];
    k.subDuck = d.sub;
    k.percDuck = d.perc;
    k.duckRelease = d.releaseMs * 0.001f;
    k.kickInterval = r.kInterval;
    return k;
}

float rootTunedKickHz(int rootNote, int interval, float nominalHz) noexcept
{
    const double base = noteToHz(rootNote + interval);
    const double octaves = std::round(std::log2(nominalHz / base));
    return static_cast<float>(base * std::exp2(octaves));
}

void applyKit(const StyleKit& kit, Engine& engine, int rootNote, std::uint32_t skipMask) noexcept
{
    auto skip = [&](int part) { return ((skipMask >> part) & 1u) != 0; };
    if (!skip(0))
    {
        engine.part(0).kick = kit.kick;
        engine.part(0).kick.tuneHz = rootTunedKickHz(rootNote, kit.kickInterval, kit.kick.tuneHz);
    }
    if (!skip(1))
        engine.part(1).snare = kit.snare;
    if (!skip(2))
        engine.part(2).sampleParams = kit.hat;
    if (!skip(3))
    {
        engine.part(3).fm = kit.perc;
        engine.part(3).mix.duck = kit.percDuck;
        engine.part(3).mix.duckRelease = kit.duckRelease;
    }
    if (!skip(4))
    {
        engine.part(4).sub = kit.sub;
        engine.part(4).sub.tuneHz = static_cast<float>(noteToHz(rootNote));
        engine.part(4).mix.duck = kit.subDuck;
        engine.part(4).mix.duckRelease = kit.duckRelease;
    }
    for (int i = 0; i < 5; ++i)
        if (!skip(i))
        {
            engine.part(i).strip = kit.strips[static_cast<std::size_t>(i)];
            engine.part(i).mix.gainDb = kit.levelDb[static_cast<std::size_t>(i)];
        }
}

} // namespace zd::gen
