#pragma once

#include "zd/dsp/Adaa.h"
#include "zd/dsp/Random.h"
#include "zd/dsp/Svf.h"
#include "zd/engines/Voice.h"

namespace zd {

/// Analog-modelled kick: a sine body with an exponential pitch sweep, a filtered-noise click
/// for the transient, and ADAA saturation (one stage, or two with a mid bell between them). The sine core is alias-free; the drive stage uses
/// first-order antiderivative anti-aliasing.
struct KickParams
{
    float tuneHz = 50.0f;        ///< resting pitch of the body
    float pitchAmount = 4.0f;    ///< sweep start, as a multiple of tune above tune (0 = no sweep)
    float pitchDecay = 0.035f;   ///< seconds, time constant of the pitch sweep
    float decay = 0.6f;          ///< seconds to -60 dB
    float click = 0.4f;          ///< transient level 0..1
    float drive = 0.25f;         ///< saturation 0..1
    /// Second drive stage (0 = off), after a bell EQ that pushes the mids: the hardstyle and
    /// festival "screech". Both stages are ADAA tanh.
    float drive2 = 0.0f;
    float midHz = 700.0f;        ///< bell centre between the stages, 500..1000 Hz typical
    float midDb = 6.0f;          ///< bell boost
};

class AnalogKickVoice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    void trigger(const KickParams& params, float velocity) noexcept;
    void render(float* out, int numSamples) noexcept override;

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double tune_ = 50.0, pitchAmount_ = 4.0;
    double pitchEnv_ = 0.0, pitchCoef_ = 0.0;
    double ampEnv_ = 0.0, ampCoef_ = 0.0;
    double clickEnv_ = 0.0, clickCoef_ = 0.0, clickLevel_ = 0.0;
    double velocity_ = 1.0;
    dsp::Svf clickFilter_;
    dsp::DriveStage drive_, drive2_;
    dsp::Svf mid_;
    double midGain_ = 1.0, midK_ = 1.0;
    bool twoStage_ = false;
    dsp::Random noise_;
};

} // namespace zd
