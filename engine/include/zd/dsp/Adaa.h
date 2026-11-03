#pragma once

#include <cmath>

namespace zd::dsp {

/// tanh saturation with first-order antiderivative anti-aliasing (ADAA1).
///
/// y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), with F(x) = log(cosh(x)).
/// Cuts aliasing from hard drive substantially at 1x rate, so saturation can
/// run per voice without oversampling. Adds half a sample of delay.
class AdaaTanh
{
public:
    void reset() noexcept { prevX_ = 0.0; prevF_ = 0.0; }

    double process(double x) noexcept
    {
        const double f = logCosh(x);
        const double dx = x - prevX_;
        double y;
        if (std::abs(dx) < kEpsilon)
            y = std::tanh(0.5 * (x + prevX_));
        else
            y = (f - prevF_) / dx;
        prevX_ = x;
        prevF_ = f;
        return y;
    }

    /// Numerically stable log(cosh(x)).
    static double logCosh(double x) noexcept
    {
        const double ax = std::abs(x);
        return ax + std::log1p(std::exp(-2.0 * ax)) - 0.6931471805599453;
    }

private:
    static constexpr double kEpsilon = 1.0e-5;
    double prevX_ = 0.0;
    double prevF_ = 0.0;
};

/// Drive stage: gain into ADAA tanh, then makeup so small signals keep roughly unity gain.
class DriveStage
{
public:
    void setDrive(double drive) noexcept
    {
        // drive in [0, 1] maps to 1x .. 20x input gain.
        gain_ = 1.0 + 19.0 * drive * drive;
        makeup_ = 1.0 / std::tanh(gain_) ;
    }

    void reset() noexcept { shaper_.reset(); }

    double process(double x) noexcept { return shaper_.process(x * gain_) * makeup_; }

private:
    AdaaTanh shaper_;
    double gain_ = 1.0;
    double makeup_ = 1.0 / std::tanh(1.0);
};

} // namespace zd::dsp
