#include "zd/Strip.h"

namespace zd {

void Strip::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    shaper_.prepare(sampleRate);
    reset();
}

void Strip::reset() noexcept
{
    shaper_.reset();
    filter_.reset();
    drive_.reset();
    lastCutoff_ = lastResonance_ = lastDrive_ = -1.0f;
}

void Strip::updateFilter(const StripParams& p) noexcept
{
    if (p.cutoffHz != lastCutoff_ || p.resonance != lastResonance_)
    {
        filter_.setCoefficients(p.cutoffHz, p.resonance, sampleRate_);
        lastCutoff_ = p.cutoffHz;
        lastResonance_ = p.resonance;
    }
    if (p.drive != lastDrive_)
    {
        drive_.setDrive(p.drive);
        lastDrive_ = p.drive;
    }
}

void Strip::process(float* mono, int numSamples, const StripParams& p) noexcept
{
    shaper_.setAmounts(p.attack, p.sustain);
    updateFilter(p);
    const bool shape = !shaper_.isNeutral();
    const bool drive = p.drive > 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        double x = mono[i];
        if (shape)
            x = shaper_.process(x);
        switch (p.filterMode)
        {
            case FilterMode::LowPass: x = filter_.process(x).low; break;
            case FilterMode::BandPass: x = filter_.process(x).band; break;
            case FilterMode::HighPass: x = filter_.process(x).high; break;
            case FilterMode::Off: break;
        }
        if (drive)
            x = drive_.process(x);
        mono[i] = static_cast<float>(x);
    }
}

} // namespace zd
