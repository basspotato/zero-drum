#pragma once

#include <cmath>
#include <numbers>
#include <vector>

namespace zd::test {

/// Magnitude of one frequency component (Goertzel). With N = sampleRate samples the bin
/// spacing is exactly 1 Hz, so integer frequencies land on a bin.
inline double goertzel(const std::vector<double>& x, double freq, double sampleRate)
{
    const double w = 2.0 * std::numbers::pi * freq / sampleRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (double v : x)
    {
        const double s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt(std::max(power, 0.0)) / static_cast<double>(x.size());
}

inline double toDb(double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

inline double peak(const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

inline bool allFinite(const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite(v))
            return false;
    return true;
}

} // namespace zd::test
