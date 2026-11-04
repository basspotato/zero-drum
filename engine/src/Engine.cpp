#include "zd/Engine.h"

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cstring>

namespace zd {

// ---------------------------------------------------------------------------------- Part

void Part::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    duckAttackStep_ = 1.0 / (0.003 * sampleRate); // 3 ms attack ramp
    cachedDuckRelease_ = -1.0f;
    for (auto& v : kickVoices_) v.prepare(sampleRate);
    for (auto& v : snareVoices_) v.prepare(sampleRate);
    for (auto& v : sampleVoices_) v.prepare(sampleRate);
    for (auto& v : fmVoices_) v.prepare(sampleRate);
    for (auto& v : subVoices_) v.prepare(sampleRate);
    for (auto& v : clapVoices_) v.prepare(sampleRate);
    strip_.prepare(sampleRate);
    scratch_.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
}

Voice& Part::voiceOf(EngineType t, int i) noexcept
{
    const auto idx = static_cast<std::size_t>(i);
    switch (t)
    {
        case EngineType::AnalogSnare: return snareVoices_[idx];
        case EngineType::Sample: return sampleVoices_[idx];
        case EngineType::FmPerc: return fmVoices_[idx];
        case EngineType::Sub808: return subVoices_[idx];
        case EngineType::Clap: return clapVoices_[idx];
        case EngineType::AnalogKick:
        case EngineType::Off:
        default: return kickVoices_[idx];
    }
}

template <typename F>
void Part::forEachVoice(F&& f) noexcept
{
    for (auto& v : kickVoices_) f(v);
    for (auto& v : snareVoices_) f(v);
    for (auto& v : sampleVoices_) f(v);
    for (auto& v : fmVoices_) f(v);
    for (auto& v : subVoices_) f(v);
    for (auto& v : clapVoices_) f(v);
}

int Part::allocateVoice(EngineType t) noexcept
{
    int oldest = 0;
    for (int i = 0; i < kVoicesPerPart; ++i)
    {
        if (!voiceOf(t, i).isActive())
            return i;
        if (voiceOf(t, i).age < voiceOf(t, oldest).age)
            oldest = i;
    }
    voiceOf(t, oldest).kill();
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

    // Mono parts choke everything still sounding, layers and voices of a previous type too.
    if (mix.mono)
        forEachVoice([](Voice& v) { v.kill(); });

    // Velocity to tone, from the hit's own velocity (before any layer gain): 0 at full
    // velocity, `velocityTone` at 0.3, up to 1.4x that for the softest hits.
    const float toneDrop = std::clamp(velocityTone, 0.0f, 1.0f) * std::clamp((1.0f - velocity) / 0.7f, 0.0f, 1.4f);

    samplesSinceTrigger_ = 0;
    triggerEngine(type, velocity, toneDrop, note, glideFromPrevious);
    if (layer != EngineType::Off && layer != type)
        triggerEngine(layer, velocity * dsp::dbToGain(std::clamp(layerGainDb, -48.0f, 6.0f)), toneDrop, note, false);
}

void Part::choke() noexcept
{
    if (samplesSinceTrigger_ > 0)
        forEachVoice([](Voice& v) { v.kill(); });
}

void Part::triggerEngine(EngineType t, float velocity, float toneDrop, int note, bool glideFromPrevious) noexcept
{
    const int i = allocateVoice(t);
    const auto idx = static_cast<std::size_t>(i);
    const float decayScale = 1.0f - 0.4f * toneDrop;
    switch (t)
    {
        case EngineType::AnalogKick:
        {
            KickParams k = kick;
            if (note >= 0)
                k.tuneHz = static_cast<float>(noteToHz(note)); // melodic kick
            k.decay *= decayScale;
            k.click *= 1.0f - 0.6f * std::min(toneDrop, 1.0f);
            k.pitchAmount *= 1.0f - 0.3f * std::min(toneDrop, 1.0f);
            kickVoices_[idx].trigger(k, velocity);
            break;
        }
        case EngineType::AnalogSnare:
        {
            SnareParams s = snare;
            s.toneDecay *= decayScale;
            s.noiseDecay *= decayScale;
            s.noiseTone = std::max(800.0f, s.noiseTone - 2000.0f * toneDrop);
            snareVoices_[idx].trigger(s, velocity);
            break;
        }
        case EngineType::Sample:
        {
            SampleParams sp = sampleParams;
            if (sp.decay > 0.0f)
                sp.decay *= decayScale;
            sampleVoices_[idx].trigger(sample.get(), sp, velocity);
            break;
        }
        case EngineType::FmPerc:
        {
            FmParams f = fm;
            f.decay *= decayScale;
            f.index *= 1.0f - 0.4f * std::min(toneDrop, 1.0f);
            fmVoices_[idx].trigger(f, velocity);
            break;
        }
        case EngineType::Clap:
        {
            ClapParams c = clap;
            c.decay *= decayScale;
            c.toneHz = std::max(400.0f, c.toneHz * (1.0f - 0.35f * toneDrop));
            c.snap *= 1.0f - 0.6f * std::min(toneDrop, 1.0f);
            clapVoices_[idx].trigger(c, velocity);
            break;
        }
        case EngineType::Sub808:
        {
            const double target = note >= 0 ? noteToHz(note) : static_cast<double>(sub.tuneHz);
            const double start = (glideFromPrevious && sub.glide > 0.0f && lastSubHz_ > 0.0) ? lastSubHz_ : target;
            subVoices_[idx].trigger(sub, velocity, target, start);
            lastSubHz_ = target;
            break;
        }
        case EngineType::Off: return;
    }
    voiceOf(t, i).age = ++ageCounter_;
}

bool Part::isActive() const noexcept
{
    return std::any_of(kickVoices_.begin(), kickVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(snareVoices_.begin(), snareVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(sampleVoices_.begin(), sampleVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(fmVoices_.begin(), fmVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(subVoices_.begin(), subVoices_.end(), [](const auto& v) { return v.isActive(); })
        || std::any_of(clapVoices_.begin(), clapVoices_.end(), [](const auto& v) { return v.isActive(); });
}

void Part::reset() noexcept
{
    for (auto& v : kickVoices_) v.reset();
    for (auto& v : snareVoices_) v.reset();
    for (auto& v : sampleVoices_) v.reset();
    for (auto& v : fmVoices_) v.reset();
    for (auto& v : subVoices_) v.reset();
    for (auto& v : clapVoices_) v.reset();
    strip_.reset();
    lastSubHz_ = 0.0;
    duckLevel_ = 0.0;
    duckStart_ = 0.0;
    duckPhase_ = 0.0;
    duckAttacking_ = false;
}

float Part::duckGain() noexcept
{
    if (duckAttacking_)
    {
        // A raised-cosine ramp to full depth over 3 ms: no step and no corner, so a ducked
        // 808 bends down smoothly instead of clicking.
        duckPhase_ += duckAttackStep_;
        if (duckPhase_ >= 1.0)
        {
            duckLevel_ = 1.0;
            duckAttacking_ = false;
        }
        else
            duckLevel_ = duckStart_ + (1.0 - duckStart_) * (0.5 - 0.5 * std::cos(dsp::kPi * duckPhase_));
    }
    else if (duckLevel_ > 0.0)
    {
        if (std::abs(mix.duckRelease - cachedDuckRelease_) > 1.0e-7f)
        {
            cachedDuckRelease_ = mix.duckRelease;
            duckReleaseCoef_ = dsp::timeConstantCoefficient(std::max(0.005f, mix.duckRelease), sampleRate_);
        }
        duckLevel_ *= duckReleaseCoef_;
        if (duckLevel_ < 1.0e-6)
            duckLevel_ = 0.0;
    }
    return static_cast<float>(1.0 - std::clamp(mix.duck, 0.0f, 1.0f) * duckLevel_);
}

void Part::render(float* left, float* right, float* send, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    samplesSinceTrigger_ = std::min<std::int64_t>(samplesSinceTrigger_ + numSamples, std::int64_t { 1 } << 40);
    if (type == EngineType::Off && !isActive())
        return;

    // The strip runs even when no voice is sounding, so filter and envelope tails evolve
    // identically whatever the block size. A part with a neutral strip and no voices is skipped.
    const bool stripNeutral = strip.attack == 0.0f && strip.sustain == 0.0f
                           && strip.filterMode == FilterMode::Off && strip.drive == 0.0f
                           && strip.crushBits <= 0.0f && strip.crushRateHz <= 0.0f;
    if (stripNeutral && !isActive())
    {
        // Keep the duck envelope in step, so the output does not depend on block size.
        if (duckLevel_ > 0.0 || duckAttacking_)
            for (int n = 0; n < numSamples; ++n)
                duckGain();
        return;
    }

    float* mono = scratch_.data();
    std::fill(mono, mono + numSamples, 0.0f);
    // Every pool renders (inactive voices return at once): layers sound, and voices of a
    // previous engine type ring out after the type changes instead of being cut.
    forEachVoice([&](Voice& v) { v.render(mono, numSamples); });
    strip_.process(mono, numSamples, strip);
    if (mix.duck > 0.0f || duckLevel_ > 0.0 || duckAttacking_)
        for (int n = 0; n < numSamples; ++n)
            mono[n] *= duckGain();

    if (mix.mute)
        return;

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
    if (mix.reverbSend > 0.0f && send != nullptr)
    {
        const float s = gain * std::clamp(mix.reverbSend, 0.0f, 1.0f);
        for (int n = 0; n < numSamples; ++n)
            send[n] += mono[n] * s;
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
    send_.assign(static_cast<std::size_t>(std::max(maxBlockSize, 1)), 0.0f);
    reverb_.prepare(sampleRate);
    reverb_.set(reverbSettings_);
    glue_.prepare(sampleRate);
}

void Engine::reset() noexcept
{
    for (auto& p : parts_)
        p.reset();
    clipper_.reset();
    reverb_.reset();
    glue_.reset();
}

void Engine::setSample(int partIndex, SampleDataPtr sample)
{
    auto& p = parts_[static_cast<std::size_t>(partIndex)];
    p.reset(); // no voice may keep pointing at the old buffer
    p.sample = std::move(sample);
}

bool Engine::submitPattern(const Pattern& pattern) noexcept
{
    int expected = Empty;
    if (!pendingState_.compare_exchange_strong(expected, Writing, std::memory_order_acquire))
    {
        expected = Ready; // replace a pattern the audio thread has not taken yet
        if (!pendingState_.compare_exchange_strong(expected, Writing, std::memory_order_acquire))
            return false;
    }
    pendingPattern_ = pattern;
    pendingState_.store(Ready, std::memory_order_release);
    return true;
}

void Engine::applyPendingPattern() noexcept
{
    int expected = Ready;
    if (!pendingState_.compare_exchange_strong(expected, Reading, std::memory_order_acquire))
        return;
    sequencer_.pattern() = pendingPattern_;
    pendingState_.store(Empty, std::memory_order_release);
}

void Engine::renderParts(float* left, float* right, float* send, int from, int to) noexcept
{
    if (to <= from)
        return;
    for (auto& p : parts_)
        p.render(left + from, right + from, send + from, to - from);
}

void Engine::process(const Transport& transport, const NoteEvent* external, int numExternal,
                     float* left, float* right, int numSamples) noexcept
{
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
    applyPendingPattern();

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

    float* send = send_.data();
    std::fill(send, send + numSamples, 0.0f);
    int position = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& e = events_[static_cast<std::size_t>(i)];
        renderParts(left, right, send, position, e.offset);
        position = e.offset;
        // Chokes first, so a part that cuts itself through its own mask still plays.
        const std::uint32_t cuts = parts_[static_cast<std::size_t>(e.part)].chokes;
        if (cuts != 0)
            for (int p = 0; p < kMaxParts; ++p)
                if (p != e.part && ((cuts >> p) & 1u) != 0)
                    parts_[static_cast<std::size_t>(p)].choke();
        parts_[static_cast<std::size_t>(e.part)].trigger(e.velocity, e.note);
        auto& hits = hitCounts_[static_cast<std::size_t>(e.part)];
        hits.store(hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (e.part == kDuckSourcePart)
            for (int p = 0; p < kMaxParts; ++p)
                if (p != kDuckSourcePart && parts_[static_cast<std::size_t>(p)].mix.duck > 0.0f)
                    parts_[static_cast<std::size_t>(p)].duckTrigger();
    }
    renderParts(left, right, send, position, numSamples);
    reverb_.process(send, left, right, numSamples);
    glue_.process(left, right, numSamples);
    glueReduction_.store(static_cast<float>(glue_.gainReductionDb()), std::memory_order_relaxed);

    if (masterGain_ != 1.0f)
        for (int n = 0; n < numSamples; ++n)
        {
            left[n] *= masterGain_;
            right[n] *= masterGain_;
        }
    if (masterStage_)
        clipper_.process(left, right, numSamples);
}

void Engine::setMasterStage(bool enabled) noexcept
{
    if (enabled != masterStage_)
        clipper_.reset();
    masterStage_ = enabled;
}

void Engine::setMasterClip(bool clipOn, float driveDb, float ceilingDb) noexcept
{
    masterClip_ = { clipOn, driveDb, ceilingDb };
    clipper_.setEnabled(clipOn);
    clipper_.setDriveDb(driveDb);
    clipper_.setCeilingDb(ceilingDb);
}

void Engine::setMasterGainDb(float db) noexcept
{
    masterGain_ = dsp::dbToGain(std::clamp(db, -60.0f, 12.0f));
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
