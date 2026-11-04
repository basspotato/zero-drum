#include "zd/engines/AnalogKick.h"

#include "zd/dsp/Math.h"

#include <cmath>

namespace zd {

void AnalogKickVoice::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    fade_.prepare(sampleRate);
    clickFilter_.setCoefficients(4000.0, 0.7, sampleRate);
    reset();
}

void AnalogKickVoice::trigger(const KickParams& p, float velocity) noexcept
{
    beginNote();
    phase_ = 0.0;
    tune_ = p.tuneHz;
    pitchAmount_ = p.pitchAmount;
    pitchEnv_ = 1.0;
    pitchCoef_ = dsp::timeConstantCoefficient(p.pitchDecay, sampleRate_);
    ampEnv_ = 1.0;
    ampCoef_ = dsp::decayCoefficient(p.decay, sampleRate_);
    clickEnv_ = 1.0;
    clickCoef_ = dsp::timeConstantCoefficient(0.0015, sampleRate_);
    clickLevel_ = p.click;
    velocity_ = velocity;
    drive_.setDrive(p.drive);
    drive_.reset();
    twoStage_ = p.drive2 > 0.0f;
    if (twoStage_)
    {
        drive2_.setDrive(p.drive2);
        drive2_.reset();
        constexpr double q = 1.2;
        mid_.setCoefficients(p.midHz, q, sampleRate_);
        mid_.reset();
        midK_ = 1.0 / q;
        midGain_ = dsp::dbToGain(p.midDb) - 1.0;
    }
    clickFilter_.reset();
    noise_.setSeed(0x5EEDull); // fixed seed: every hit is bit-identical, renders are deterministic
}

void AnalogKickVoice::render(float* out, int numSamples) noexcept
{
    if (!active_)
        return;

    const double invSr = 1.0 / sampleRate_;
    for (int i = 0; i < numSamples; ++i)
    {
        const double freq = tune_ * (1.0 + pitchAmount_ * pitchEnv_);
        phase_ += freq * invSr;
        phase_ -= std::floor(phase_);

        const double body = std::sin(dsp::kTwoPi * phase_) * ampEnv_;
        const double click = clickFilter_.process(noise_.bipolar()).high * clickEnv_ * clickLevel_;
        double y = drive_.process(body + click);
        if (twoStage_)
        {
            // Bell between the stages: x + (G - 1) * k * band has gain G at the centre.
            y += midGain_ * midK_ * mid_.process(y).band;
            y = drive2_.process(y);
        }
        y *= velocity_;

        out[i] += static_cast<float>(y) * fade_.next();

        pitchEnv_ *= pitchCoef_;
        ampEnv_ *= ampCoef_;
        clickEnv_ *= clickCoef_;

        if (fade_.isFinished() || (ampEnv_ < 1.0e-4 && clickEnv_ < 1.0e-4))
        {
            endNote();
            return;
        }
    }
}

} // namespace zd
