#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace zd {

inline constexpr int kMaxParts = 16;
inline constexpr int kVoicesPerPart = 4;
inline constexpr int kMaxStepsPerLane = 64;
inline constexpr int kMaxEventsPerBlock = 1024;

/// A trigger for one part, at a sample offset inside the current block.
struct NoteEvent
{
    int offset = 0;       ///< sample offset in the block, 0 <= offset < numSamples
    int part = 0;         ///< part index, 0 <= part < kMaxParts
    float velocity = 1.0f;///< 0..1
    int note = -1;        ///< MIDI note for pitched parts (808); -1 = the part's own tune
};

/// Host or internal transport state at the start of a block.
struct Transport
{
    bool playing = false;
    double bpm = 120.0;
    double ppqPosition = 0.0; ///< position in quarter notes at the first sample of the block
};

/// Immutable mono sample data. Shared by reference; never modified after creation.
struct SampleData
{
    std::vector<float> samples;
    double sampleRate = 48000.0;
};

using SampleDataPtr = std::shared_ptr<const SampleData>;

enum class EngineType : std::uint8_t
{
    Off,
    AnalogKick,
    AnalogSnare,
    Sample,
    FmPerc,
    Sub808,
};

} // namespace zd
