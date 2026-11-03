#pragma once

#include "zd/dsp/Adaa.h"
#include "zd/dsp/Svf.h"
#include "zd/dsp/TransientShaper.h"

#include <cstdint>

namespace zd {

enum class FilterMode : std::uint8_t { Off, LowPass, BandPass, HighPass };

/// Settings for a part's channel strip. Order: transient shaper, filter, drive.
struct StripParams
{
    float attack = 0.0f;           ///< transient shaper attack amount, -1..1
    float sustain = 0.0f;          ///< transient shaper sustain amount, -1..1
    FilterMode filterMode = FilterMode::Off;
    float cutoffHz = 8000.0f;
    float resonance = 0.707f;      ///< filter Q
    float drive = 0.0f;            ///< ADAA saturation 0..1 (0 = bypassed)
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
    float lastCutoff_ = -1.0f, lastResonance_ = -1.0f, lastDrive_ = -1.0f;
};

} // namespace zd
