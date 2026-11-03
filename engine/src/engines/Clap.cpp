#include "zd/engines/Clap.h"

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace zd {

void ClapVoice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    reset();
}

void ClapVoice::trigger(const ClapParams& p, float velocity) noexcept
{
    beginNote();
    t_ = 0;
    bursts_ = std::clamp(p.bursts, 1, kMaxBursts);
    // Hands never land evenly: a fixed, human-ish jitter on the spacing (deterministic).
    constexpr std::array<double, kMaxBursts> jitter { 0.0, 1.0, 0.85, 1.2, 0.95, 1.1 };
    const double spread = std::clamp(static_cast<double>(p.spread), 0.002, 0.03) * sampleRate_;
    double at = 0.0;
    for (int i = 0; i < kMaxBursts; ++i)
    {
        at += jitter[static_cast<std::size_t>(i)] * spread;
        burstAt_[static_cast<std::size_t>(i)] = static_cast<long long>(std::llround(at));
    }
    nextBurst_ = 0;
    burstEnv_ = 0.0;
    // Each burst falls by about 30 dB before the next one starts.
    burstCoef_ = dsp::decayCoefficient(std::max(0.0015, 2.0 * spread / sampleRate_), sampleRate_);
    tailEnv_ = 0.0;
    tailCoef_ = dsp::decayCoefficient(std::max(0.02f, p.decay), sampleRate_);
    tailLevel_ = std::clamp(p.tail, 0.0f, 1.0f);
    snap_ = std::clamp(p.snap, 0.0f, 1.0f);
    velocity_ = velocity;
    body_.setCoefficients(std::clamp(p.toneHz, 300.0f, 6000.0f), 1.4, sampleRate_);
    body_.reset();
    skin_.setCoefficients(std::min(9000.0, 0.4 * sampleRate_), 0.7, sampleRate_);
    skin_.reset();
    drive_.setDrive(p.drive);
    drive_.reset();
    noise_.setSeed(0xC1A9ull);
}

void ClapVoice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    for (int i = 0; i < numSamples; ++i, ++t_)
    {
        if (nextBurst_ < bursts_ && t_ >= burstAt_[static_cast<std::size_t>(nextBurst_)] - burstAt_[0])
        {
            burstEnv_ = 1.0;
            if (++nextBurst_ == bursts_)
                tailEnv_ = tailLevel_; // the room starts with the last hand
        }

        const double n = noise_.bipolar();
        const auto b = body_.process(n);
        const auto s = skin_.process(n);
        const double env = burstEnv_ + tailEnv_;
        const double x = (b.band * 2.2 + snap_ * s.high * 0.8) * env;
        out[i] += static_cast<float>(drive_.process(x) * velocity_) * fade_.next();

        burstEnv_ *= burstCoef_;
        tailEnv_ *= tailCoef_;
        if (fade_.isFinished() || (nextBurst_ == bursts_ && burstEnv_ < 1.0e-4 && tailEnv_ < 1.0e-4))
        {
            endNote();
            return;
        }
    }
}

} // namespace zd
