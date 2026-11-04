#pragma once

#include "zd/dsp/Math.h"
#include "zd/dsp/Svf.h"

#include <algorithm>
#include <cmath>

namespace zd::dsp {

struct GlueSettings
{
    bool on = false;
    float thresholdDb = -12.0f; ///< -40..0 dBFS
    float ratio = 2.0f;         ///< 1..10
    float attackMs = 10.0f;     ///< 0.1..100; longer lets the transients through
    float releaseMs = 120.0f;   ///< 10..1200
    float makeupDb = 0.0f;      ///< 0..12
    float mix = 1.0f;           ///< 0 = dry, 1 = fully compressed (parallel compression in between)
    float sidechainHpHz = 90.0f; ///< 20..300: the detector ignores the low end, so the 808 doesn't pump the bus
};

/// Stereo-linked bus compressor for the "glue" that holds a kit together.
///
/// Feed-forward: the detector takes the louder of the two (high-passed) channels, a static
/// curve with a 6 dB soft knee gives the target gain reduction in dB, and a branching
/// one-pole smoother (attack when reduction grows, release when it shrinks) moves the gain.
/// Both channels get the same gain, so the stereo image never shifts.
///
/// The gain is a smoothed, slowly varying control signal (attack >= 0.1 ms), not a waveshaper,
/// so it does not need ADAA or oversampling; the aliasing test in test_glue.cpp checks that.
/// Runs per sample: block-size independent. prepare() and set() are realtime safe.
class GlueCompressor
{
public:
    static constexpr double kKneeDb = 6.0;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        set(settings_);
        reset();
    }

    void reset() noexcept
    {
        hpL_.reset();
        hpR_.reset();
        envDb_ = 0.0;
    }

    void set(const GlueSettings& s) noexcept
    {
        settings_ = s;
        settings_.thresholdDb = std::clamp(s.thresholdDb, -40.0f, 0.0f);
        settings_.ratio = std::clamp(s.ratio, 1.0f, 10.0f);
        settings_.attackMs = std::clamp(s.attackMs, 0.1f, 100.0f);
        settings_.releaseMs = std::clamp(s.releaseMs, 10.0f, 1200.0f);
        settings_.makeupDb = std::clamp(s.makeupDb, 0.0f, 12.0f);
        settings_.mix = std::clamp(s.mix, 0.0f, 1.0f);
        settings_.sidechainHpHz = std::clamp(s.sidechainHpHz, 20.0f, 300.0f);
        attackCoef_ = std::exp(-1.0 / (0.001 * settings_.attackMs * sampleRate_));
        releaseCoef_ = std::exp(-1.0 / (0.001 * settings_.releaseMs * sampleRate_));
        slope_ = 1.0 - 1.0 / settings_.ratio;
        makeup_ = dbToGain(settings_.makeupDb);
        hpL_.setCoefficients(settings_.sidechainHpHz, 0.7071, sampleRate_);
        hpR_.setCoefficients(settings_.sidechainHpHz, 0.7071, sampleRate_);
    }

    const GlueSettings& settings() const noexcept { return settings_; }

    /// Gain reduction the compressor is applying right now, in dB (>= 0).
    double gainReductionDb() const noexcept { return envDb_; }

    /// Target gain reduction in dB for a detector level in dBFS (the static curve).
    double staticReductionDb(double levelDb) const noexcept
    {
        const double over = levelDb - settings_.thresholdDb;
        if (over <= -0.5 * kKneeDb)
            return 0.0;
        if (over >= 0.5 * kKneeDb)
            return slope_ * over;
        const double t = over + 0.5 * kKneeDb;
        return slope_ * t * t / (2.0 * kKneeDb);
    }

    void process(float* left, float* right, int numSamples) noexcept
    {
        if (!settings_.on)
            return;
        const double mix = settings_.mix;
        for (int n = 0; n < numSamples; ++n)
        {
            const double l = left[n];
            const double r = right[n];
            const double peak = std::max(std::abs(hpL_.process(l).high), std::abs(hpR_.process(r).high));
            const double levelDb = 20.0 * std::log10(peak + 1e-9);
            const double target = staticReductionDb(levelDb);
            const double coef = target > envDb_ ? attackCoef_ : releaseCoef_;
            envDb_ = flushDenormal(target + coef * (envDb_ - target));
            const double wet = std::pow(10.0, -envDb_ / 20.0) * makeup_;
            const double g = (1.0 - mix) + mix * wet;
            left[n] = static_cast<float>(l * g);
            right[n] = static_cast<float>(r * g);
        }
    }

private:
    GlueSettings settings_ {};
    double sampleRate_ = 48000.0;
    double attackCoef_ = 0.0, releaseCoef_ = 0.0, slope_ = 0.5, makeup_ = 1.0;
    double envDb_ = 0.0;
    Svf hpL_, hpR_;
};

} // namespace zd::dsp
