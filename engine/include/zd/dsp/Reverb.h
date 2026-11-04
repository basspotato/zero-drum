#pragma once

#include "zd/dsp/Math.h"
#include "zd/dsp/Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace zd::dsp {

struct ReverbSettings
{
    float decay = 1.0f;        ///< seconds to -60 dB (RT60), 0.2..6
    float size = 0.5f;         ///< 0 = small room, 1 = big plate/hall (scales the delay lengths)
    float preDelayMs = 10.0f;  ///< 0..100
    float dampingHz = 7000.0f; ///< high frequencies decay faster above this
    float highPassHz = 400.0f; ///< high-pass on the return, keeps the low end dry
    float returnDb = 0.0f;     ///< level of the wet return
};

/// Mono in, stereo out feedback delay network: 8 delay lines mixed by a Householder matrix,
/// each with a one-pole damping filter and a gain set from the RT60. Linear and seeded by
/// nothing, so output is fully deterministic and block-size independent (it runs per sample).
/// prepare() allocates; set() and process() are realtime safe.
class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        const double scale = sampleRate / 48000.0;
        for (int i = 0; i < kLines; ++i)
            lines_[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(std::ceil(kBase[static_cast<std::size_t>(i)] * kMaxSize * scale)) + 2, 0.0f);
        preDelay_.assign(static_cast<std::size_t>(0.1 * sampleRate) + 2, 0.0f);
        lastSettings_ = ReverbSettings { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -100.0f };
        set(settings_);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines_)
            std::fill(l.begin(), l.end(), 0.0f);
        std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
        damp_.fill(0.0);
        hpL_.reset();
        hpR_.reset();
        write_.fill(0);
        prePos_ = 0;
    }

    void set(const ReverbSettings& s) noexcept
    {
        settings_ = s;
        if (lines_[0].empty())
            return;
        const auto& o = lastSettings_;
        auto same = [](float a, float b) { return std::abs(a - b) <= 0.0f; };
        if (same(s.decay, o.decay) && same(s.size, o.size) && same(s.preDelayMs, o.preDelayMs)
            && same(s.dampingHz, o.dampingHz) && same(s.highPassHz, o.highPassHz) && same(s.returnDb, o.returnDb))
            return;
        lastSettings_ = s;
        const double scale = sampleRate_ / 48000.0 * (0.35 + 0.65 * std::clamp(static_cast<double>(s.size), 0.0, 1.0)) * kMaxSize;
        const double rt60 = std::clamp(static_cast<double>(s.decay), 0.05, 20.0);
        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            len_[idx] = std::clamp(static_cast<int>(kBase[idx] * scale), 1, static_cast<int>(lines_[idx].size()) - 1);
            gain_[idx] = std::pow(10.0, -3.0 * len_[idx] / (rt60 * sampleRate_));
        }
        dampCoef_ = std::exp(-2.0 * kPi * std::clamp(static_cast<double>(s.dampingHz), 200.0, 0.45 * sampleRate_) / sampleRate_);
        preLen_ = std::clamp(static_cast<int>(s.preDelayMs * 0.001 * sampleRate_), 0, static_cast<int>(preDelay_.size()) - 1);
        hpL_.setCoefficients(s.highPassHz, 0.707, sampleRate_);
        hpR_.setCoefficients(s.highPassHz, 0.707, sampleRate_);
        wet_ = dbToGain(std::clamp(s.returnDb, -48.0f, 12.0f)) * 0.35; // keeps a full send near unity loudness
    }

    /// Adds the wet return for `in` into left and right.
    void process(const float* in, float* left, float* right, int numSamples) noexcept
    {
        if (lines_[0].empty())
            return;
        const int preSize = static_cast<int>(preDelay_.size());
        for (int n = 0; n < numSamples; ++n)
        {
            // Pre-delay.
            preDelay_[static_cast<std::size_t>(prePos_)] = in[n];
            int read = prePos_ - preLen_;
            if (read < 0)
                read += preSize;
            const double x = preDelay_[static_cast<std::size_t>(read)];
            prePos_ = prePos_ + 1 == preSize ? 0 : prePos_ + 1;

            // Read every line, damp it, then mix with the Householder matrix.
            std::array<double, kLines> y {};
            double sum = 0.0;
            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<std::size_t>(i);
                const auto& line = lines_[idx];
                const int size = static_cast<int>(line.size());
                int r = write_[idx] - len_[idx];
                if (r < 0)
                    r += size;
                double v = line[static_cast<std::size_t>(r)];
                damp_[idx] = flushDenormal(v + dampCoef_ * (damp_[idx] - v));
                y[idx] = damp_[idx] * gain_[idx];
                sum += y[idx];
            }
            const double h = 2.0 / kLines * sum;
            double outL = 0.0, outR = 0.0;
            for (int i = 0; i < kLines; ++i)
            {
                const auto idx = static_cast<std::size_t>(i);
                auto& line = lines_[idx];
                const int size = static_cast<int>(line.size());
                line[static_cast<std::size_t>(write_[idx])] = static_cast<float>(y[idx] - h + x);
                write_[idx] = write_[idx] + 1 == size ? 0 : write_[idx] + 1;
                const double sign = (i & 2) ? -1.0 : 1.0;
                if (i % 2 == 0)
                    outL += sign * y[idx];
                else
                    outR += sign * y[idx];
            }
            left[n] += static_cast<float>(hpL_.process(outL).high * wet_);
            right[n] += static_cast<float>(hpR_.process(outR).high * wet_);
        }
    }

private:
    // Mutually prime lengths at 48 kHz for size 1, about 21 to 58 ms.
    static constexpr std::array<double, kLines> kBase { 1031, 1327, 1523, 1871, 2053, 2311, 2539, 2803 };
    static constexpr double kMaxSize = 1.0;

    double sampleRate_ = 48000.0;
    std::array<std::vector<float>, kLines> lines_ {};
    std::array<int, kLines> len_ {};
    std::array<double, kLines> gain_ {};
    std::array<double, kLines> damp_ {};
    std::vector<float> preDelay_;
    std::array<int, kLines> write_ {};
    int prePos_ = 0, preLen_ = 0;
    double dampCoef_ = 0.0, wet_ = 0.0;
    Svf hpL_, hpR_;
    ReverbSettings settings_ {};
    ReverbSettings lastSettings_ { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -100.0f };
};

} // namespace zd::dsp
