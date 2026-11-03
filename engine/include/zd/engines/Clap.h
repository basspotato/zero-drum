#pragma once

#include "zd/dsp/Adaa.h"

#include <array>
#include "zd/dsp/Random.h"
#include "zd/dsp/Svf.h"
#include "zd/engines/Voice.h"

namespace zd {

/// Hand clap: several hands hitting a few ms apart (short noise bursts), then the room
/// tail. Noise is band-passed for the body and high-passed for the snap of the skin.
struct ClapParams
{
    float toneHz = 1200.0f; ///< band-pass centre of the body (500..4000)
    float spread = 0.009f;  ///< seconds between bursts (0.003..0.025): tight to loose
    int bursts = 4;         ///< hands before the tail (1..6)
    float decay = 0.3f;     ///< seconds to -60 dB for the tail
    float tail = 0.6f;      ///< tail level against the bursts, 0..1
    float snap = 0.4f;      ///< high-passed "skin" noise, 0..1
    float drive = 0.2f;     ///< saturation 0..1
};

class ClapVoice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    void trigger(const ClapParams& params, float velocity) noexcept;
    void render(float* out, int numSamples) noexcept override;

    static constexpr int kMaxBursts = 6;

private:
    double sampleRate_ = 48000.0;
    long long t_ = 0;                          ///< samples since the trigger
    std::array<long long, kMaxBursts> burstAt_ {}; ///< burst start times (samples)
    int bursts_ = 4;
    double burstEnv_ = 0.0, burstCoef_ = 0.0;
    double tailEnv_ = 0.0, tailCoef_ = 0.0, tailLevel_ = 0.6;
    double snap_ = 0.4;
    double velocity_ = 1.0;
    int nextBurst_ = 0;
    dsp::Svf body_, skin_;
    dsp::DriveStage drive_;
    dsp::Random noise_;
};

} // namespace zd
