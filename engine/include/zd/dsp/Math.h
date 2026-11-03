#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace zd::dsp {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Coefficient for a one-pole exponential decay that falls to -60 dB after `seconds`.
inline double decayCoefficient(double seconds, double sampleRate) noexcept
{
    if (seconds <= 0.0)
        return 0.0;
    // ln(0.001) = -6.907755...
    return std::exp(-6.907755278982137 / (seconds * sampleRate));
}

/// Coefficient for a one-pole decay with time constant `seconds` (falls to 1/e).
inline double timeConstantCoefficient(double seconds, double sampleRate) noexcept
{
    if (seconds <= 0.0)
        return 0.0;
    return std::exp(-1.0 / (seconds * sampleRate));
}

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

/// Constant-power pan law. pan in [-1, 1].
inline void constantPowerPan(float pan, float& left, float& right) noexcept
{
    const float angle = (pan + 1.0f) * 0.25f * static_cast<float>(kPi);
    left = std::cos(angle);
    right = std::sin(angle);
}

/// Flushes tiny values to zero so decaying recursions never produce denormals.
inline double flushDenormal(double x) noexcept
{
    return std::abs(x) < 1.0e-30 ? 0.0 : x;
}

} // namespace zd::dsp
