#pragma once

#include "zd/dsp/Adaa.h"
#include "zd/dsp/Crusher.h"
#include "zd/dsp/Svf.h"
#include "zd/dsp/TransientShaper.h"

#include <cstdint>

namespace zd {

enum class FilterMode : std::uint8_t { Off, LowPass, BandPass, HighPass };

/// Settings for a part's channel strip. Order: crusher, transient shaper, filter, drive.
/// The crusher comes first, like sampling the hit into a 12-bit sampler; the filter after it
/// then plays the part of the sampler's output filter.
struct StripParams
{
    float attack = 0.0f;           ///< transient shaper attack amount, -1..1
    float sustain = 0.0f;          ///< transient shaper sustain amount, -1..1
    FilterMode filterMode = FilterMode::Off;
    float cutoffHz = 8000.0f;
    float resonance = 0.707f;      ///< filter Q
    float drive = 0.0f;            ///< ADAA saturation 0..1 (0 = bypassed)
    float crushBits = 0.0f;        ///< bit depth 4..16, 0 = off
    float crushRateHz = 0.0f;      ///< sample-and-hold rate, 0 = off (26040 = the classic 12-bit sampler)
    bool crushFilter = true;       ///< low-pass before the hold, so the input does not fold down
};

/// Per-part processing after the voices and before pan and gain.
/// Runs on every sample of an enabled part, active or not, so its state evolves the same
/// way whatever the block size (keeps the null test exact).
class Strip
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void process(float* mono, int numSamples, const StripParams& params) noexcept;

private:
    void updateFilter(const StripParams& params) noexcept;

    double sampleRate_ = 48000.0;
    dsp::TransientShaper shaper_;
    dsp::Svf filter_;
    dsp::DriveStage drive_;
    dsp::Crusher crusher_;
    float lastCutoff_ = -1.0f, lastResonance_ = -1.0f, lastDrive_ = -1.0f;
};

} // namespace zd
