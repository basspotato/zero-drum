#pragma once

#include "zd/dsp/Adaa.h"
#include "zd/dsp/Random.h"
#include "zd/dsp/Svf.h"
#include "zd/engines/Voice.h"

namespace zd {

/// Analog-modelled snare: two detuned sine "shell" modes with a short pitch drop, plus
/// band-passed noise for the wires, mixed by `snappy` and lightly saturated.
struct SnareParams
{
    float tuneHz = 190.0f;     ///< first shell mode; the second sits at 1.62x
    float toneDecay = 0.12f;   ///< seconds to -60 dB for the shell
    float noiseDecay = 0.22f;  ///< seconds to -60 dB for the wires
    float noiseTone = 3500.0f; ///< centre frequency of the wire noise
    float snappy = 0.6f;       ///< 0 = all shell, 1 = all wires
    float drive = 0.15f;       ///< saturation 0..1
};

class AnalogSnareVoice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    void trigger(const SnareParams& params, float velocity) noexcept;
    void render(float* out, int numSamples) noexcept override;

private:
    double sampleRate_ = 48000.0;
    double phase1_ = 0.0, phase2_ = 0.0;
    double tune_ = 190.0;
    double pitchEnv_ = 0.0, pitchCoef_ = 0.0;
    double toneEnv_ = 0.0, toneCoef_ = 0.0;
    double noiseEnv_ = 0.0, noiseCoef_ = 0.0;
    double toneMix_ = 0.4, noiseMix_ = 0.6;
    double velocity_ = 1.0;
    dsp::Svf noiseFilter_;
    dsp::DriveStage drive_;
    dsp::Random noise_;
};

} // namespace zd
