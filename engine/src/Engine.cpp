#include "zd/Engine.h"

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cstring>

namespace zd {

// ---------------------------------------------------------------------------------- Part

void Part::prepare(double sampleRate, int maxBlockSize)
{
    for (auto& v : kickVoices_) v.prepare(sampleRate);
    for (auto& v : snareVoices_) v.prepare(sampleRate);
    for (auto& v : sampleVoices_) v.prepare(sampleRate);
    for (auto& v : fmVoices_) v.prepare(sampleRate);
    for (auto& v : subVoices_) v.prepare(sampleRate);
    strip_.prepare(sampleRate);
    scratch_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
}

Voice& Part::voice(int i) noexcept
{
    const auto idx = static_cast<std::size_t>(i);
    switch (type)
    {
        case EngineType::AnalogSnare: return snareVoices_[idx];
        case EngineType::Sample: return sampleVoices_[idx];
        case EngineType::FmPerc: return fmVoices_[idx];
        case EngineType::Sub808: return subVoices_[idx];
        case EngineType::AnalogKick:
        case EngineType::Off:
        default: return kickVoices_[idx];
    }
}

int Part::allocateVoice() noexcept
{
    int oldest = 0;
    for (int i = 0; i < kVoicesPerPart; ++i)
    {
        if (!voice(i).isActive())
            return i;
        if (voice(i).age < voice(oldest).age)
            oldest = i;
    }
    voice(oldest).reset();
    return oldest;
}

void Part::trigger(float velocity, int note) noexcept
{
    if (type == EngineType::Off)
        return;

    // 808 glide starts from the previous note only while that note is still sounding.
    bool glideFromPrevious = false;
    if (type == EngineType::Sub808)
        for (int i = 0; i < kVoicesPerPart; ++i)
            glideFromPrevious = glideFromPrevious || subVoices_[static_cast<std::size_t>(i)].isActive();

    if (mix.mono)
        for (int i = 0; i < kVoicesPerPart; ++i)
            voice(i).kill();

    const int i = allocateVoice();
    const auto idx = static_cast<std::size_t>(i);
    switch (type)
    {
        case EngineType::AnalogKick: kickVoices_[idx].trigger(kick, velocity); break;
        case EngineType::AnalogSnare: snareVoices_[idx].trigger(snare, velocity); break;
        case EngineType::Sample: sampleVoices_[idx].trigger(sample.get(), sampleParams, velocity); break;
        case EngineType::FmPerc: fmVoices_[idx].trigger(fm, velocity); break;
        case EngineType::Sub808:
        {
            const double target = note >= 0 ? noteToHz(note) : static_cast<double>(sub.tuneHz);
            const double start = (glideFromPrevious && sub.glide > 0.0f && lastSubHz_ > 0.0) ? lastSubHz_ : target;
            subVoices_[idx].trigger(sub, velocity, target, start);
            lastSubHz_ = target;
            break;
        }
        case EngineType::Off: break;
    }
    voice(i).age = ++ageCounter_;
}

bool Part::isActive() const noexcept
{
    return std::any_of(kickVoices_.begin(), kickVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(snareVoices_.begin(), snareVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(sampleVoices_.begin(), sampleVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(fmVoices_.begin(), fmVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(subVoices_.begin(), subVoices_.end(), [](const auto& v) { return v.isActive(); });
}

void Part::reset() noexcept
{
    for (auto& v : kickVoices_) v.reset();
    for (auto& v : snareVoices_) v.reset();
    for (auto& v : sampleVoices_) v.reset();
    for (auto& v : fmVoices_) v.reset();
    for (auto& v : subVoices_) v.reset();
    strip_.reset();
    lastSubHz_ = 0.0;
}

void Part::render(float* left, float* right, int numSamples) noexcept
{
    if (type == EngineType::Off || numSamples <= 0)
        return;

    // The strip runs even when no voice is sounding, so filter and envelope tails evolve
    // identically whatever the block size. A part with a neutral strip and no voices is skipped.
    const bool stripNeutral = strip.attack == 0.0f && strip.sustain == 0.0f
                           && strip.filterMode == FilterMode::Off && strip.drive == 0.0f;
    if (stripNeutral && !isActive())
        return;

    float* mono = scratch_.data();
    std::fill(mono, mono + numSamples, 0.0f);
    for (int i = 0; i < kVoicesPerPart; ++i)
        voice(i).render(mono, numSamples);
    strip_.process(mono, numSamples, strip);

    float panL = 0.0f, panR = 0.0f;
    dsp::constantPowerPan(std::clamp(mix.pan, -1.0f, 1.0f), panL, panR);
    const float gain = dsp::dbToGain(mix.gainDb);
    panL *= gain;
    panR *= gain;
    for (int n = 0; n < numSamples; ++n)
    {
        left[n] += mono[n] * panL;
        right[n] += mono[n] * panR;
    }
}

// -------------------------------------------------------------------------------- Engine

Engine::Engine()
{
    loadDefaultPattern(sequencer_.pattern());
}

void Engine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    for (auto& p : parts_)
        p.prepare(sampleRate, maxBlockSize);
}

void Engine::reset() noexcept
{
    for (auto& p : parts_)
        p.reset();
}

void Engine::setSample(int partIndex, SampleDataPtr sample)
{
    auto& p = parts_[static_cast<std::size_t>(partIndex)];
    p.reset(); // no voice may keep pointing at the old buffer
    p.sample = std::move(sample);
}

void Engine::renderParts(float* left, float* right, int from, int to) noexcept
{
    if (to <= from)
        return;
    for (auto& p : parts_)
        p.render(left + from, right + from, to - from);
}

void Engine::process(const Transport& transport, const NoteEvent* external, int numExternal,
                     float* left, float* right, int numSamples) noexcept
{
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);

    // Blocks larger than prepare() promised are rendered in chunks so scratch buffers suffice.
    if (numSamples > maxBlockSize_ && maxBlockSize_ > 0)
    {
        Transport t = transport;
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min(maxBlockSize_, numSamples - done);
            std::array<NoteEvent, 64> chunkEvents {};
            int numChunk = 0;
            for (int i = 0; i < numExternal && numChunk < static_cast<int>(chunkEvents.size()); ++i)
                if (external[i].offset >= done && external[i].offset < done + n)
                    chunkEvents[static_cast<std::size_t>(numChunk++)] = { external[i].offset - done, external[i].part, external[i].velocity, external[i].note };
            process(t, chunkEvents.data(), numChunk, left + done, right + done, n);
            t.ppqPosition += static_cast<double>(n) * t.bpm / (60.0 * sampleRate_);
            done += n;
        }
        return;
    }

    int count = sequencer_.collect(transport, sampleRate_, numSamples, events_.data(), kMaxEventsPerBlock);
    for (int i = 0; i < numExternal && count < kMaxEventsPerBlock; ++i)
        if (external[i].part >= 0 && external[i].part < kMaxParts)
            events_[static_cast<std::size_t>(count++)] = NoteEvent { std::clamp(external[i].offset, 0, numSamples - 1), external[i].part, external[i].velocity, external[i].note };

    // Stable insertion sort by offset (std::stable_sort may allocate). Ties keep lane order,
    // so output is deterministic. Event counts per block are small.
    for (int i = 1; i < count; ++i)
    {
        const NoteEvent e = events_[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && events_[static_cast<std::size_t>(j)].offset > e.offset)
        {
            events_[static_cast<std::size_t>(j + 1)] = events_[static_cast<std::size_t>(j)];
            --j;
        }
        events_[static_cast<std::size_t>(j + 1)] = e;
    }

    int position = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& e = events_[static_cast<std::size_t>(i)];
        renderParts(left, right, position, e.offset);
        position = e.offset;
        parts_[static_cast<std::size_t>(e.part)].trigger(e.velocity, e.note);
    }
    renderParts(left, right, position, numSamples);
}

void loadDefaultKit(Engine& engine)
{
    auto& kick = engine.part(0);
    kick.type = EngineType::AnalogKick;
    kick.mix.mono = true;
    kick.mix.gainDb = -2.0f;

    auto& snare = engine.part(1);
    snare.type = EngineType::AnalogSnare;
    snare.mix.gainDb = -3.0f;

    auto& hat = engine.part(2);
    hat.type = EngineType::Sample;
    hat.mix.gainDb = -8.0f;
    hat.mix.pan = 0.15f;
    engine.setSample(2, makeSynthHatSample(engine.sampleRate()));

    auto& perc = engine.part(3);
    perc.type = EngineType::FmPerc;
    perc.mix.gainDb = -12.0f;
    perc.mix.pan = -0.3f;

    auto& sub = engine.part(4);
    sub.type = EngineType::Sub808;
    sub.mix.mono = true;
    sub.mix.gainDb = -10.0f;
}

} // namespace zd
