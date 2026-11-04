#pragma once

#include "zd/dsp/Svf.h"

#include <algorithm>
#include <cmath>

namespace zd::dsp {

/// Sampler-style bit and rate reduction, after the 12-bit drum samplers of classic hip-hop.
///
/// Rate reduction is a sample-and-hold on a phase accumulator, so the held samples land on the
/// same output samples whatever the block size. Before the hold, an optional 4th-order
/// Butterworth low-pass at 0.45 x the new rate keeps the input from folding down (that is the
/// stage's anti-aliasing, and its test). The steps of the hold and the quantizer are the
/// effect itself: their images above the new Nyquist are the grit, and are meant to be heard.
class Crusher
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        lp1_.reset();
        lp2_.reset();
        phase_ = 1.0; // the first input sample is captured immediately
        held_ = 0.0;
        lastRate_ = -1.0f;
        lastBits_ = -1.0f;
    }

    /// bits: 0 = no quantizing, else 2..24 (fractions allowed). rateHz: 0 or >= sampleRate = no hold.
    void set(float bits, float rateHz) noexcept
    {
        if (std::abs(rateHz - lastRate_) > 0.0f)
        {
            lastRate_ = rateHz;
            hold_ = rateHz > 0.0f && rateHz < sampleRate_;
            step_ = hold_ ? static_cast<double>(rateHz) / sampleRate_ : 1.0;
            const double fc = 0.45 * std::max(static_cast<double>(rateHz), 100.0);
            lp1_.setCoefficients(fc, 0.5411961, sampleRate_);
            lp2_.setCoefficients(fc, 1.3065630, sampleRate_);
        }
        if (std::abs(bits - lastBits_) > 0.0f)
        {
            lastBits_ = bits;
            quantize_ = bits > 0.0f && bits < 24.0f;
            levels_ = quantize_ ? std::exp2(static_cast<double>(std::max(bits, 2.0f)) - 1.0) : 1.0;
        }
    }

    bool active() const noexcept { return hold_ || quantize_; }

    double process(double x, bool preFilter) noexcept
    {
        if (!hold_)
            return quantize(x);
        if (preFilter)
            x = lp2_.process(lp1_.process(x).low).low;
        if (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            held_ = quantize(x);
        }
        phase_ += step_;
        return held_;
    }

private:
    double quantize(double x) const noexcept { return quantize_ ? std::round(x * levels_) / levels_ : x; }

    double sampleRate_ = 48000.0;
    Svf lp1_, lp2_;
    double phase_ = 1.0, step_ = 1.0, held_ = 0.0, levels_ = 1.0;
    bool hold_ = false, quantize_ = false;
    float lastRate_ = -1.0f, lastBits_ = -1.0f;
};

} // namespace zd::dsp
