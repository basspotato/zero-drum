#include "zd/Sequencer.h"

#include "zd/dsp/Random.h"

#include <algorithm>
#include <cmath>

namespace zd {

int patternBars(const Pattern& pattern) noexcept
{
    double beats = 0.0;
    for (const auto& lane : pattern.lanes)
    {
        const int n = std::clamp(lane.length, 0, kMaxStepsPerLane);
        if (std::any_of(lane.steps.begin(), lane.steps.begin() + n, [](const Step& s) { return s.on; }))
            beats = std::max(beats, n * pattern.stepBeats);
    }
    return std::clamp(static_cast<int>(std::ceil(beats / 4.0 - 1e-9)), 1, 16);
}

void Sequencer::setChain(const Chain& chain) noexcept
{
    chainCount_ = std::clamp(chain.count, 1, kMaxChain);
    chainBeats_ = 0.0;
    for (int i = 0; i < chainCount_; ++i)
    {
        slots_[static_cast<std::size_t>(i)] = chain.slots[static_cast<std::size_t>(i)];
        slotBeats_[static_cast<std::size_t>(i)] = 4.0 * patternBars(slots_[static_cast<std::size_t>(i)]);
        chainBeats_ += slotBeats_[static_cast<std::size_t>(i)];
    }
}

void Sequencer::locate(double ppq, int& slot, double& slotStart, double& slotEnd) const noexcept
{
    const double cycle = std::floor(ppq / chainBeats_);
    double start = cycle * chainBeats_;
    slot = 0;
    for (int i = 0; i < chainCount_; ++i)
    {
        const double end = start + slotBeats_[static_cast<std::size_t>(i)];
        if (ppq < end || i == chainCount_ - 1)
        {
            slot = i;
            slotStart = start;
            slotEnd = end;
            return;
        }
        start = end;
    }
}

const Pattern& Sequencer::patternAt(double ppq) const noexcept
{
    if (chainCount_ <= 1)
        return slots_[0];
    int slot = 0;
    double start = 0.0, end = 0.0;
    locate(ppq, slot, start, end);
    return slots_[static_cast<std::size_t>(slot)];
}

double Sequencer::passStartAt(double ppq) const noexcept
{
    if (chainCount_ <= 1)
        return 0.0;
    int slot = 0;
    double start = 0.0, end = 0.0;
    locate(ppq, slot, start, end);
    return start;
}

double Sequencer::stepTimeIn(const Pattern& pattern, int lane, std::int64_t k) noexcept
{
    const Lane& l = pattern.lanes[static_cast<std::size_t>(lane)];
    const Step& s = l.steps[static_cast<std::size_t>(k % l.length)];
    const double base = static_cast<double>(k) * pattern.stepBeats;
    const double swingShift = (k % 2 != 0 && !l.straight) ? (pattern.swing - 0.5) * 2.0 * pattern.stepBeats : 0.0;
    return base + swingShift + static_cast<double>(s.microOffset) * pattern.stepBeats;
}

std::int64_t Sequencer::passIndexAt(double ppq) const noexcept
{
    if (chainCount_ <= 1)
        return 0;
    int slot = 0;
    double start = 0.0, end = 0.0;
    locate(ppq, slot, start, end);
    return static_cast<std::int64_t>(std::floor(ppq / chainBeats_)) * chainCount_ + slot;
}

bool Sequencer::passesProbability(int lane, std::int64_t k, float probability, std::int64_t pass) const noexcept
{
    if (probability >= 1.0f)
        return true;
    if (probability <= 0.0f)
        return false;
    std::uint64_t key = dsp::hash64(seed_) ^ dsp::hash64((static_cast<std::uint64_t>(lane) << 48) ^ static_cast<std::uint64_t>(k));
    if (pass != 0)
        key ^= dsp::hash64(static_cast<std::uint64_t>(pass) * 0x9E3779B97F4A7C15ull);
    return dsp::hashToUnit(key) < static_cast<double>(probability);
}

int Sequencer::collect(const Transport& t, double sampleRate, int numSamples,
                       NoteEvent* out, int capacity) const noexcept
{
    if (!t.playing || t.bpm <= 0.0 || numSamples <= 0)
        return 0;
    if (chainCount_ <= 1)
        return collectPattern(slots_[0], t, sampleRate, numSamples, out, capacity, 0);

    // A chain: cut the block where one slot hands over to the next. Each slot runs on its own
    // clock that starts at zero when its pass begins, so every pass plays from its first step.
    // Boundaries are found on absolute sample indices, so the cut is the same for any block size.
    const double samplesPerBeat = sampleRate * 60.0 / t.bpm;
    const std::int64_t blockStart = std::llround(t.ppqPosition * samplesPerBeat);
    std::int64_t at = blockStart;
    const std::int64_t blockEnd = blockStart + numSamples;
    int count = 0;
    while (at < blockEnd && count < capacity)
    {
        int slot = 0;
        double slotStart = 0.0, slotEnd = 0.0;
        locate(static_cast<double>(at) / samplesPerBeat, slot, slotStart, slotEnd);
        const std::int64_t slotStartSample = std::llround(slotStart * samplesPerBeat);
        std::int64_t segEnd = std::min<std::int64_t>(blockEnd, std::llround(slotEnd * samplesPerBeat));
        if (segEnd <= at)
            segEnd = at + 1; // guard against rounding at the seam
        Transport local = t;
        local.ppqPosition = static_cast<double>(at - slotStartSample) / samplesPerBeat;
        const auto pass = static_cast<std::int64_t>(std::floor(static_cast<double>(at) / samplesPerBeat / chainBeats_)) * chainCount_ + slot;
        const int n = collectPattern(slots_[static_cast<std::size_t>(slot)], local, sampleRate, static_cast<int>(segEnd - at),
                                     out + count, capacity - count, pass);
        for (int i = 0; i < n; ++i)
            out[count + i].offset += static_cast<int>(at - blockStart);
        count += n;
        at = segEnd;
    }
    return count;
}

int Sequencer::collectPattern(const Pattern& pattern, const Transport& t, double sampleRate, int numSamples,
                              NoteEvent* out, int capacity, std::int64_t pass) const noexcept
{

    const double samplesPerBeat = sampleRate * 60.0 / t.bpm;
    const std::int64_t blockStart = std::llround(t.ppqPosition * samplesPerBeat);
    const double beatEnd = t.ppqPosition + static_cast<double>(numSamples) / samplesPerBeat;

    // Swing and micro offsets move a step by less than one step either way, so a margin
    // of two steps on each side catches every step that can land in this block.
    const auto first = static_cast<std::int64_t>(std::floor(t.ppqPosition / pattern.stepBeats)) - 2;
    const auto last = static_cast<std::int64_t>(std::ceil(beatEnd / pattern.stepBeats)) + 2;

    int count = 0;
    for (int lane = 0; lane < kMaxParts; ++lane)
    {
        const Lane& l = pattern.lanes[static_cast<std::size_t>(lane)];
        if (l.length <= 0)
            continue;

        for (std::int64_t k = std::max<std::int64_t>(first, 0); k <= last; ++k)
        {
            const Step& s = l.steps[static_cast<std::size_t>(k % l.length)];
            if (!s.on)
                continue;

            const double time = stepTimeIn(pattern, lane, k);
            if (time < 0.0)
                continue;
            if (!passesProbability(lane, k, s.probability, pass))
                continue;

            // Ratchets repeat the hit evenly inside the step (trap and drill hat rolls).
            const int repeats = std::clamp(s.ratchet, 1, kMaxRatchet);
            const double spacing = pattern.stepBeats / repeats;
            for (int r = 0; r < repeats; ++r)
            {
                const double t_r = time + r * spacing;
                const std::int64_t offset = std::llround(t_r * samplesPerBeat) - blockStart;
                if (offset < 0 || offset >= numSamples)
                    continue;
                if (count >= capacity)
                    return count;
                const float velocity = r == 0 ? s.velocity : s.velocity * 0.8f;
                out[count++] = NoteEvent { static_cast<int>(offset), lane, velocity, s.note };
            }
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

    auto& perc = p.lanes[3];
    perc.steps[3] = { true, 0.8f, 0.7f, 0.0f };
    perc.steps[11] = { true, 0.7f, 0.7f, 0.0f };

    // 808 follows the kick: A1, A1, C2, G1.
    auto& sub = p.lanes[4];
    sub.steps[0] = { true, 1.0f, 1.0f, 0.0f, 33 };
    sub.steps[7] = { true, 0.9f, 1.0f, 0.0f, 33 };
    sub.steps[10] = { true, 0.9f, 1.0f, 0.0f, 36 };
    sub.steps[14] = { true, 0.8f, 1.0f, 0.0f, 31 };
}

} // namespace zd
