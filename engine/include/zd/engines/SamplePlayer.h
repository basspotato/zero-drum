#pragma once

#include "zd/Types.h"
#include "zd/engines/Voice.h"

namespace zd {

/// One-shot sample player with 4-point Hermite interpolation, pitch in semitones and an
/// optional decay envelope. Sample-rate conversion from the file's rate happens on the fly.
/// (A windowed-sinc / polyphase interpolator is planned for the render-quality mode.)
struct SampleParams
{
    float pitchSemitones = 0.0f;
    float decay = 0.0f;  ///< seconds to -60 dB; 0 = play the whole sample at full level
    float start = 0.0f;  ///< start position as a fraction of the sample length, 0..1
    bool reverse = false; ///< play from the end to the start (start then counts from the end)
    float vary = 0.0f;    ///< 0..1: each hit gets a slightly different pitch and level (seeded per hit)
};

class SamplePlayerVoice final : public Voice
{
public:
    void prepare(double sampleRate) override;
    /// `sample` must outlive the voice's playback; the engine guarantees this.
    void trigger(const SampleData* sample, const SampleParams& params, float velocity) noexcept;
    void render(float* out, int numSamples) noexcept override;

    /// Interpolates `data` at fractional `position` with 4-point, 3rd-order Hermite.
    static float hermite(const float* data, int length, double position) noexcept;

private:
    double sampleRate_ = 48000.0;
    const SampleData* sample_ = nullptr;
    double position_ = 0.0, increment_ = 1.0;
    double env_ = 1.0, envCoef_ = 1.0;
    float velocity_ = 1.0f;
    bool reverse_ = false;
};

/// Cleans a loaded one-shot in place: trims silence (below -60 dBFS) from both ends, keeping
/// 1 ms before the first sound and fading the last 2 ms so the cut never clicks, then
/// normalizes the peak to `peakDb` (pass 0 or more to skip normalizing). Returns false if
/// the sample is silent (it is left unchanged). Allocation-free except for the final shrink.
bool prepareOneShot(SampleData& sample, float peakDb = -1.0f);

/// Builds a synthesized closed hi-hat one-shot: six square oscillators at classic
/// inharmonic ratios, high-passed, with a fast decay. Lets the plugin ship a sample part
/// with no external files. Not realtime safe (allocates); call at load time.
SampleDataPtr makeSynthHatSample(double sampleRate);

} // namespace zd
