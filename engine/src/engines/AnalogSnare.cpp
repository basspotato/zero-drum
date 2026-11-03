#include "zd/engines/AnalogSnare.h"

#include "zd/dsp/Math.h"

#include <cmath>

namespace zd {

void AnalogSnareVoice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    reset();
}

void AnalogSnareVoice::trigger(const SnareParams& p, float velocity) noexcept
{
    beginNote();
    phase1_ = 0.0;
    phase2_ = 0.0;
    tune_ = p.tuneHz;
    pitchEnv_ = 1.0;
    pitchCoef_ = dsp::timeConstantCoefficient(0.008, sampleRate_);
    toneEnv_ = 1.0;
    toneCoef_ = dsp::decayCoefficient(p.toneDecay, sampleRate_);
    noiseEnv_ = 1.0;
    noiseCoef_ = dsp::decayCoefficient(p.noiseDecay, sampleRate_);
    toneMix_ = 1.0 - p.snappy;
    noiseMix_ = p.snappy;
    velocity_ = velocity;
    noiseFilter_.setCoefficients(p.noiseTone, 0.9, sampleRate_);
    noiseFilter_.reset();
    drive_.setDrive(p.drive);
    drive_.reset();
    noise_.setSeed(0x5A4E5ull);
}

void AnalogSnareVoice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    const double invSr = 1.0 / sampleRate_;
    for (int i = 0; i < numSamples; ++i)
    {
        const double drop = 1.0 + 0.5 * pitchEnv_;
        phase1_ += tune_ * drop * invSr;
        phase2_ += tune_ * 1.62 * drop * invSr;
        phase1_ -= std::floor(phase1_);
        phase2_ -= std::floor(phase2_);

        const double shell = (std::sin(dsp::kTwoPi * phase1_) + 0.6 * std::sin(dsp::kTwoPi * phase2_)) * 0.625 * toneEnv_;
        const auto f = noiseFilter_.process(noise_.bipolar());
        const double wires = (f.band + 0.5 * f.high) * noiseEnv_;
        const double y = drive_.process(toneMix_ * shell + noiseMix_ * wires) * velocity_;

        out[i] += static_cast<float>(y) * fade_.next();

        pitchEnv_ *= pitchCoef_;
        toneEnv_ *= toneCoef_;
        noiseEnv_ *= noiseCoef_;

        if (fade_.isFinished() || (toneEnv_ < 1.0e-4 && noiseEnv_ < 1.0e-4))
        {
            endNote();
            return;
        }
    }
}

} // namespace zd
