#include "zd/engines/SamplePlayer.h"

#include "zd/dsp/Math.h"
#include "zd/dsp/Svf.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace zd {

void SamplePlayerVoice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    reset();
}

void SamplePlayerVoice::trigger(const SampleData* sample, const SampleParams& p, float velocity) noexcept
{
    if (sample == nullptr || sample->samples.size() < 2)
        return;

    beginNote();
    sample_ = sample;
    const auto length = static_cast<double>(sample->samples.size());
    position_ = std::clamp(static_cast<double>(p.start), 0.0, 1.0) * (length - 1.0);
    increment_ = (sample->sampleRate / sampleRate_) * std::pow(2.0, p.pitchSemitones / 12.0);
    env_ = 1.0;
    envCoef_ = p.decay > 0.0f ? dsp::decayCoefficient(p.decay, sampleRate_) : 1.0;
    velocity_ = velocity;
}

float SamplePlayerVoice::hermite(const float* data, int length, double position) noexcept
{
    const int i = static_cast<int>(position);
    const auto t = static_cast<float>(position - i);
    auto at = [&](int k) { return (k >= 0 && k < length) ? data[k] : 0.0f; };
    const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void SamplePlayerVoice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    const float* data = sample_->samples.data();
    const int length = static_cast<int>(sample_->samples.size());

    for (int i = 0; i < numSamples; ++i)
    {
        if (position_ >= static_cast<double>(length - 1) || fade_.isFinished() || env_ < 1.0e-4)
        {
            endNote();
            return;
        }
        const float s = hermite(data, length, position_);
        out[i] += s * static_cast<float>(env_) * velocity_ * fade_.next();
        position_ += increment_;
        env_ *= envCoef_;
    }
}

SampleDataPtr makeSynthHatSample(double sampleRate)
{
    auto data = std::make_shared<SampleData>();
    data->sampleRate = sampleRate;
    const int length = static_cast<int>(0.25 * sampleRate);
    data->samples.resize(static_cast<std::size_t>(length));

    // Six inharmonic square partials (the classic metallic cluster), naive squares are fine
    // here because the result is high-passed hard and rendered once, offline.
    constexpr std::array<double, 6> freqs { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    std::array<double, 6> phases {};
    dsp::Svf hp, bp;
    hp.setCoefficients(7000.0, 0.7, sampleRate);
    bp.setCoefficients(10000.0, 1.2, sampleRate);
    const double envCoef = dsp::decayCoefficient(0.09, sampleRate);
    double env = 1.0;

    for (int n = 0; n < length; ++n)
    {
        double x = 0.0;
        for (std::size_t k = 0; k < freqs.size(); ++k)
        {
            phases[k] += freqs[k] * 2.0 / sampleRate; // x2 ratio scales the cluster up an octave
            phases[k] -= std::floor(phases[k]);
            x += phases[k] < 0.5 ? 1.0 : -1.0;
        }
        x /= static_cast<double>(freqs.size());
        const double y = bp.process(hp.process(x).high).band;
        data->samples[static_cast<std::size_t>(n)] = static_cast<float>(y * env * 1.6);
        env *= envCoef;
    }
    return data;
}

} // namespace zd
