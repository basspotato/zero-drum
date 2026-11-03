#pragma once

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace zd::dsp {

/// Zero-delay-feedback state-variable filter (topology-preserving transform, after Zavalishin).
/// Stable under fast modulation and at high resonance. Produces LP, BP and HP at once.
class Svf
{
public:
    struct Outputs { double low, band, high; };

    void setCoefficients(double cutoffHz, double q, double sampleRate) noexcept
    {
        cutoffHz = std::clamp(cutoffHz, 10.0, 0.49 * sampleRate);
        q = std::max(q, 0.025);
        g_ = std::tan(kPi * cutoffHz / sampleRate);
        k_ = 1.0 / q;
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    void reset() noexcept { ic1_ = 0.0; ic2_ = 0.0; }

    Outputs process(double x) noexcept
    {
        const double v3 = x - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = flushDenormal(2.0 * v1 - ic1_);
        ic2_ = flushDenormal(2.0 * v2 - ic2_);
        return { v2, v1, x - k_ * v1 - v2 };
    }

private:
    double g_ = 0.0, k_ = 1.0, a1_ = 1.0, a2_ = 0.0, a3_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

} // namespace zd::dsp
