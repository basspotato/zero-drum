#pragma once

#include "zd/dsp/Random.h"
#include "zd/engines/Voice.h"

namespace zd {

/// Two-operator phase-modulation percussion: metallic hats, bells, zaps, plucky percs.
/// The modulator has self-feedback; the modulation index and the carrier pitch each have
/// their own fast envelope. The index is limited per sample (Carson's rule) so sidebands
/// stay below Nyquist instead of folding back as aliasing.
struct FmParams
{
    float tuneHz = 420.0f;        ///< carrier frequency at rest
    float ratio = 1.41f;          ///< modulator frequency / carrier frequency
    float index = 3.0f;           ///< peak modulation index (radians)
    float indexDecay = 0.05f;     ///< seconds, time constant of the index envelope
    float decay = 0.25f;          ///< seconds to -60 dB
    float feedback = 0.2f;        ///< modulator self-feedback 0..1
    float pitchDrop = 0.5f;       ///< carrier starts (1 + pitchDrop) x higher
    float pitchDropTime = 0.01f;  ///< seconds, time constant of the pitch drop
};

class FmPercVoice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    void trigger(const FmParams& params, float velocity) noexcept;
    void render(float* out, int numSamples) noexcept override;

    /// Largest index whose Carson bandwidth, fc + (I + 1) fm, stays under 0.45 x sampleRate.
    static double limitIndex(double index, double carrierHz, double modulatorHz, double sampleRate) noexcept;

private:
    double sampleRate_ = 48000.0;
    double carrierPhase_ = 0.0, modPhase_ = 0.0, prevMod_ = 0.0;
    double tune_ = 420.0, ratio_ = 1.41, index_ = 3.0, feedback_ = 0.2, pitchDrop_ = 0.5;
    double indexEnv_ = 0.0, indexCoef_ = 0.0;
    double pitchEnv_ = 0.0, pitchCoef_ = 0.0;
    double ampEnv_ = 0.0, ampCoef_ = 0.0;
    double velocity_ = 1.0;
};

} // namespace zd
