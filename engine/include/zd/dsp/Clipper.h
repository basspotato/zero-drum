#pragma once

#include "zd/dsp/Math.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace zd::dsp {

/// Soft-knee clip curve: linear up to `knee`, a quadratic shoulder with continuous slope,
/// flat at `ceiling`. Clean on drums: no change below the knee, no corner at the top.
inline double softKneeClip(double x, double knee, double ceiling) noexcept
{
    const double ax = std::abs(x);
    if (ax <= knee)
        return x;
    const double width = 2.0 * (ceiling - knee);
    const double t = (ax - knee) / width;
    const double y = t >= 1.0 ? ceiling : knee + width * (t - 0.5 * t * t);
    return std::copysign(y, x);
}

/// Stereo master clipper, 4x oversampled with linear-phase windowed-sinc filters, so the
/// harmonics clipping makes above the base Nyquist are filtered out instead of aliasing.
///
/// Constant latency of kLatency samples, also while bypassed (a matching delay), so a host
/// never sees the latency change. Realtime safe after prepare(): fixed arrays, no allocation.
class MasterClipper
{
public:
    static constexpr int kFactor = 4;
    static constexpr int kTaps = 257;                         ///< at the 4x rate, odd: linear phase
    static constexpr int kPhaseTaps = (kTaps + kFactor - 1) / kFactor; ///< 65
    static constexpr int kLatency = (kTaps - 1) / kFactor;    ///< 64 base-rate samples

    MasterClipper() { designFilter(); }

    void reset() noexcept
    {
        for (auto& c : channels_)
            c = Channel {};
    }

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    /// Gain into the clipper in dB (0..24): more drive, louder and more clipped.
    void setDriveDb(float db) noexcept { drive_ = dbToGain(std::clamp(db, 0.0f, 24.0f)); }
    /// Output ceiling in dBFS. The knee starts 3 dB below it.
    void setCeilingDb(float db) noexcept
    {
        ceiling_ = dbToGain(std::clamp(db, -24.0f, 0.0f));
        knee_ = ceiling_ * 0.7079457843841379; // -3 dB
    }

    void process(float* left, float* right, int numSamples) noexcept
    {
        processChannel(channels_[0], left, numSamples);
        processChannel(channels_[1], right, numSamples);
    }

private:
    struct Channel
    {
        // Doubled ring buffers: every sample is written at i and i + N, so a window of N
        // newest samples is always contiguous and no modulo runs in the inner loops.
        std::array<double, 2 * kPhaseTaps> up {};
        std::array<double, 2 * kTaps> down {};
        std::array<double, kLatency> delay {};
        int upPos = 0, downPos = 0, delayPos = 0;
    };

    void designFilter() noexcept
    {
        // Kaiser-windowed sinc, cutoff 0.45 of the base rate's Nyquist band edge (21.6 kHz at
        // 48 kHz), beta 7 (about 70 dB stopband).
        constexpr double beta = 7.0;
        const double fc = 0.45 / kFactor; // cycles per sample at the 4x rate, x2 = 0.1125
        const double centre = 0.5 * (kTaps - 1);
        const double i0Beta = besselI0(beta);
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double m = i - centre;
            const double sinc = 2 * i == kTaps - 1 ? 2.0 * fc : std::sin(kTwoPi * fc * m) / (kPi * m);
            const double r = m / centre;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
            h_[static_cast<std::size_t>(i)] = sinc * w;
            sum += h_[static_cast<std::size_t>(i)];
        }
        for (auto& v : h_)
            v /= sum; // unity DC gain
    }

    static double besselI0(double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    void processChannel(Channel& c, float* io, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const double x = io[n];
            if (!enabled_)
            {
                // Same latency as the clipper path.
                const double out = c.delay[static_cast<std::size_t>(c.delayPos)];
                c.delay[static_cast<std::size_t>(c.delayPos)] = x;
                c.delayPos = (c.delayPos + 1) % kLatency;
                // Keep the oversampling state fed so turning the clipper on is click-free.
                pushUp(c, x);
                for (int p = 0; p < kFactor; ++p)
                    pushDown(c, upsample(c, p));
                io[n] = static_cast<float>(out);
                continue;
            }
            pushUp(c, x * drive_);
            // Output is taken right after phase 0, so the total delay is a whole number of
            // base-rate samples: 2 x 128 taps at 4x = 64 samples.
            pushDown(c, softKneeClip(upsample(c, 0), knee_, ceiling_));
            const double y = downsample(c);
            for (int p = 1; p < kFactor; ++p)
                pushDown(c, softKneeClip(upsample(c, p), knee_, ceiling_));
            // Delay line stays filled with the dry input so turning the clipper off is click-free.
            c.delay[static_cast<std::size_t>(c.delayPos)] = x;
            c.delayPos = (c.delayPos + 1) % kLatency;
            io[n] = static_cast<float>(y);
        }
    }

    static void pushUp(Channel& c, double x) noexcept
    {
        c.upPos = (c.upPos + kPhaseTaps - 1) % kPhaseTaps;
        c.up[static_cast<std::size_t>(c.upPos)] = x;
        c.up[static_cast<std::size_t>(c.upPos + kPhaseTaps)] = x;
    }

    /// One 4x-rate output of the zero-stuffed input: polyphase branch p.
    double upsample(const Channel& c, int p) const noexcept
    {
        double acc = 0.0;
        const double* window = c.up.data() + c.upPos; // newest first
        for (int k = 0; k < kPhaseTaps; ++k)
        {
            const int tap = k * kFactor + p;
            if (tap < kTaps)
                acc += h_[static_cast<std::size_t>(tap)] * window[k];
        }
        return acc * kFactor;
    }

    static void pushDown(Channel& c, double x) noexcept
    {
        c.downPos = (c.downPos + kTaps - 1) % kTaps;
        c.down[static_cast<std::size_t>(c.downPos)] = x;
        c.down[static_cast<std::size_t>(c.downPos + kTaps)] = x;
    }

    double downsample(const Channel& c) const noexcept
    {
        const double* window = c.down.data() + c.downPos; // newest first
        double acc = 0.0;
        for (int k = 0; k < kTaps; ++k)
            acc += h_[static_cast<std::size_t>(k)] * window[k];
        return acc;
    }

    std::array<double, kTaps> h_ {};
    std::array<Channel, 2> channels_ {};
    bool enabled_ = true;
    float drive_ = 1.0f;
    double ceiling_ = 0.8912509381337456; // -1 dBFS
    double knee_ = 0.8912509381337456 * 0.7079457843841379;
};

} // namespace zd::dsp
