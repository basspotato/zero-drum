#include "zd/engines/FmPerc.h"

#include "zd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace zd {

void FmPercVoice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    reset();
}

double FmPercVoice::limitIndex(double index, double carrierHz, double modulatorHz, double sampleRate) noexcept
{
    if (modulatorHz <= 0.0)
        return index;
    const double maxIndex = (0.45 * sampleRate - carrierHz) / modulatorHz - 1.0;
    return std::clamp(index, 0.0, std::max(0.0, maxIndex));
}

void FmPercVoice::trigger(const FmParams& p, float velocity) noexcept
{
    beginNote();
    carrierPhase_ = 0.0;
    modPhase_ = 0.0;
    prevMod_ = 0.0;
    tune_ = p.tuneHz;
    ratio_ = p.ratio;
    index_ = p.index;
    feedback_ = std::clamp(static_cast<double>(p.feedback), 0.0, 1.0);
    pitchDrop_ = p.pitchDrop;
    indexEnv_ = 1.0;
    indexCoef_ = dsp::timeConstantCoefficient(p.indexDecay, sampleRate_);
    pitchEnv_ = 1.0;
    pitchCoef_ = dsp::timeConstantCoefficient(p.pitchDropTime, sampleRate_);
    ampEnv_ = 1.0;
    ampCoef_ = dsp::decayCoefficient(p.decay, sampleRate_);
    velocity_ = velocity;
}

void FmPercVoice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    const double invSr = 1.0 / sampleRate_;
    for (int i = 0; i < numSamples; ++i)
    {
        const double fc = tune_ * (1.0 + pitchDrop_ * pitchEnv_);
        const double fm = fc * ratio_;
        const double index = limitIndex(index_ * indexEnv_, fc, fm, sampleRate_);

        modPhase_ += fm * invSr;
        modPhase_ -= std::floor(modPhase_);
        carrierPhase_ += fc * invSr;
        carrierPhase_ -= std::floor(carrierPhase_);

        // Averaging the feedback path over two samples tames the classic FM feedback hiss.
        const double mod = std::sin(dsp::kTwoPi * modPhase_ + feedback_ * 1.5 * prevMod_);
        prevMod_ = 0.5 * (prevMod_ + mod);
        const double y = std::sin(dsp::kTwoPi * carrierPhase_ + index * mod) * ampEnv_ * velocity_;

        out[i] += static_cast<float>(y) * fade_.next();

        indexEnv_ *= indexCoef_;
        pitchEnv_ *= pitchCoef_;
        ampEnv_ *= ampCoef_;

        if (fade_.isFinished() || ampEnv_ < 1.0e-4)
        {
            endNote();
            return;
        }
    }
}

} // namespace zd
