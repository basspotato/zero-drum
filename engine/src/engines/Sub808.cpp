#include "zd/engines/Sub808.h"

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace zd {

double noteToHz(int note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

void Sub808Voice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    attackLength_ = std::max(1, static_cast<int>(0.001 * sampleRate));
    reset();
}

void Sub808Voice::trigger(const Sub808Params& p, float velocity, double targetHz, double startHz) noexcept
{
    beginNote();
    phase_ = 0.0;
    targetHz_ = targetHz;
    currentHz_ = startHz;
    glideCoef_ = dsp::timeConstantCoefficient(p.glide, sampleRate_);
    punch_ = p.punch;
    punchEnv_ = 1.0;
    punchCoef_ = dsp::timeConstantCoefficient(p.punchTime, sampleRate_);
    ampEnv_ = 1.0;
    ampCoef_ = dsp::decayCoefficient(p.decay, sampleRate_);
    attackRemaining_ = attackLength_;
    velocity_ = velocity;
    drive_.setDrive(p.drive);
    drive_.reset();
}

void Sub808Voice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    const double invSr = 1.0 / sampleRate_;
    for (int i = 0; i < numSamples; ++i)
    {
        currentHz_ = targetHz_ + (currentHz_ - targetHz_) * glideCoef_;
        phase_ += currentHz_ * (1.0 + punch_ * punchEnv_) * invSr;
        phase_ -= std::floor(phase_);

        double attack = 1.0;
        if (attackRemaining_ > 0)
        {
            attack = 1.0 - static_cast<double>(attackRemaining_) / static_cast<double>(attackLength_);
            --attackRemaining_;
        }

        const double y = drive_.process(std::sin(dsp::kTwoPi * phase_) * ampEnv_ * attack) * velocity_;
        out[i] += static_cast<float>(y) * fade_.next();

        punchEnv_ *= punchCoef_;
        ampEnv_ *= ampCoef_;

        if (fade_.isFinished() || ampEnv_ < 1.0e-4)
        {
            endNote();
            return;
        }
    }
}

} // namespace zd
