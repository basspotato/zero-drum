#include "zd/Sequencer.h"

#include "zd/dsp/Random.h"

#include <cmath>

namespace zd {

double Sequencer::stepTime(int lane, std::int64_t k) const noexcept
{
    const Lane& l = pattern_.lanes[static_cast<std::size_t>(lane)];
    const Step& s = l.steps[static_cast<std::size_t>(k % l.length)];
    const double base = static_cast<double>(k) * pattern_.stepBeats;
    const double swingShift = (k % 2 != 0) ? (pattern_.swing - 0.5) * 2.0 * pattern_.stepBeats : 0.0;
    return base + swingShift + static_cast<double>(s.microOffset) * pattern_.stepBeats;
}

bool Sequencer::passesProbability(int lane, std::int64_t k, float probability) const noexcept
{
    if (probability >= 1.0f)
        return true;
    if (probability <= 0.0f)
        return false;
    const std::uint64_t key = dsp::hash64(seed_) ^ dsp::hash64((static_cast<std::uint64_t>(lane) << 48) ^ static_cast<std::uint64_t>(k));
    return dsp::hashToUnit(key) < static_cast<double>(probability);
}

int Sequencer::collect(const Transport& t, double sampleRate, int numSamples,
                       NoteEvent* out, int capacity) const noexcept
{
    if (!t.playing || t.bpm <= 0.0 || numSamples <= 0)
        return 0;

    const double samplesPerBeat = sampleRate * 60.0 / t.bpm;
    const std::int64_t blockStart = std::llround(t.ppqPosition * samplesPerBeat);
    const double beatEnd = t.ppqPosition + static_cast<double>(numSamples) / samplesPerBeat;

    // Swing and micro offsets move a step by less than one step either way, so a margin
    // of two steps on each side catches every step that can land in this block.
    const auto first = static_cast<std::int64_t>(std::floor(t.ppqPosition / pattern_.stepBeats)) - 2;
    const auto last = static_cast<std::int64_t>(std::ceil(beatEnd / pattern_.stepBeats)) + 2;

    int count = 0;
    for (int lane = 0; lane < kMaxParts; ++lane)
    {
        const Lane& l = pattern_.lanes[static_cast<std::size_t>(lane)];
        if (l.length <= 0)
            continue;

        for (std::int64_t k = std::max<std::int64_t>(first, 0); k <= last; ++k)
        {
            const Step& s = l.steps[static_cast<std::size_t>(k % l.length)];
            if (!s.on)
                continue;

            const double time = stepTime(lane, k);
            if (time < 0.0)
                continue;

            const std::int64_t offset = std::llround(time * samplesPerBeat) - blockStart;
            if (offset < 0 || offset >= numSamples)
                continue;
            if (!passesProbability(lane, k, s.probability))
                continue;
            if (count >= capacity)
                return count;

            out[count++] = NoteEvent { static_cast<int>(offset), lane, s.velocity };
        }
    }
    return count;
}

void loadDefaultPattern(Pattern& p)
{
    p = Pattern {};
    p.swing = 0.54;

    auto& kick = p.lanes[0];
    for (int s : { 0, 7, 10 })
        kick.steps[static_cast<std::size_t>(s)] = { true, 1.0f, 1.0f, 0.0f };
    kick.steps[14] = { true, 0.8f, 0.5f, 0.0f };

    auto& snare = p.lanes[1];
    snare.steps[4] = { true, 1.0f, 1.0f, 0.0f };
    snare.steps[12] = { true, 1.0f, 1.0f, 0.0f };
    snare.steps[15] = { true, 0.35f, 0.4f, 0.0f }; // ghost note

    auto& hat = p.lanes[2];
    for (int s = 0; s < 16; ++s)
        hat.steps[static_cast<std::size_t>(s)] = { true, (s % 2 == 0) ? 0.9f : 0.5f, (s % 2 == 0) ? 1.0f : 0.8f, 0.0f };
}

} // namespace zd
