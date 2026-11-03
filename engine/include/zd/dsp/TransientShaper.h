#pragma once

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace zd::dsp {

/// Level-independent transient shaper: compares a fast and a slow envelope follower.
/// Where the fast one leads (an attack) the `attack` amount applies; elsewhere `sustain`.
/// Both amounts are -1..1 and map to up to +/-12 dB.
class TransientShaper
{
public:
    void prepare(double sampleRate) noexcept
    {
        fastAttack_ = timeConstantCoefficient(0.0005, sampleRate);
        slowAttack_ = timeConstantCoefficient(0.020, sampleRate);
        release_ = timeConstantCoefficient(0.060, sampleRate);
        reset();
    }

    void reset() noexcept { fast_ = 0.0; slow_ = 0.0; }

    void setAmounts(float attack, float sustain) noexcept
    {
        attack_ = std::clamp(static_cast<double>(attack), -1.0, 1.0);
        sustain_ = std::clamp(static_cast<double>(sustain), -1.0, 1.0);
    }

    bool isNeutral() const noexcept { return std::abs(attack_) < 1.0e-6 && std::abs(sustain_) < 1.0e-6; }

    double process(double x) noexcept
    {
        const double level = std::abs(x);
        fast_ = follow(fast_, level, fastAttack_);
        slow_ = follow(slow_, level, slowAttack_);
        if (fast_ < 1.0e-6)
            return x;

        const double t = std::clamp((fast_ - slow_) / fast_, 0.0, 1.0); // 1 = pure attack
        const double gainDb = 12.0 * (attack_ * t + sustain_ * (1.0 - t));
        return x * std::exp(gainDb * 0.11512925464970229); // ln(10) / 20
    }

private:
    double follow(double env, double level, double attackCoef) const noexcept
    {
        const double coef = level > env ? attackCoef : release_;
        return flushDenormal(level + (env - level) * coef);
    }

    double fastAttack_ = 0.0, slowAttack_ = 0.0, release_ = 0.0;
    double fast_ = 0.0, slow_ = 0.0;
    double attack_ = 0.0, sustain_ = 0.0;
};

} // namespace zd::dsp
