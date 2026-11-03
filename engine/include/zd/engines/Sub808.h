#pragma once

#include "zd/dsp/Adaa.h"
#include "zd/engines/Voice.h"

namespace zd {

/// Tuned 808 sub: a sine with a short "punch" pitch envelope, glide from the previous note,
/// a 1 ms click-free attack, long decay and ADAA saturation for the distorted 808 sound.
/// Plays melodically: the note comes from MIDI or from the sequencer step.
struct Sub808Params
{
    float tuneHz = 55.0f;      ///< pitch used when a trigger carries no note
    float decay = 1.4f;        ///< seconds to -60 dB
    float glide = 0.06f;       ///< seconds, time constant of the slide from the previous note (0 = off)
    float punch = 1.0f;        ///< attack pitch boost, as a multiple of the note frequency
    float punchTime = 0.015f;  ///< seconds, time constant of the punch envelope
    float drive = 0.35f;       ///< saturation 0..1
};

class Sub808Voice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    /// `startHz` is where the glide starts; pass `targetHz` for no glide.
    void trigger(const Sub808Params& params, float velocity, double targetHz, double startHz) noexcept;
    void render(float* out, int numSamples) noexcept override;

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double currentHz_ = 55.0, targetHz_ = 55.0, glideCoef_ = 0.0;
    double punch_ = 1.0, punchEnv_ = 0.0, punchCoef_ = 0.0;
    double ampEnv_ = 0.0, ampCoef_ = 0.0;
    int attackRemaining_ = 0, attackLength_ = 48;
    double velocity_ = 1.0;
    dsp::DriveStage drive_;
};

/// MIDI note number to frequency (A4 = 440 Hz).
double noteToHz(int note) noexcept;

} // namespace zd
