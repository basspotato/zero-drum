#pragma once

#include "zd/Engine.h"
#include "zd/gen/Generator.h"

#include <array>

namespace zd::gen {

/// The sound of a style: how the five default-kit parts are tuned, shaped and levelled.
/// Values come from the genre tuning guide (Zero Drum: Genre Drum Tuning & Kit Bible),
/// with a per-style trim so every style peaks under 0 dBFS.
struct StyleKit
{
    KickParams kick;
    SnareParams snare;
    SampleParams hat;
    FmParams perc;
    Sub808Params sub;
    ClapParams clap;
    /// The snare lane's engine (snare or clap) and an optional second layer on it.
    EngineType snareEngine = EngineType::AnalogSnare;
    EngineType snareLayer = EngineType::Off;
    float snareLayerDb = -4.0f;
    std::array<StripParams, 5> strips {};
    std::array<float, 5> levelDb {};
    /// The style's home key for the 808 (MIDI note).
    int rootNote = 33;
    /// Kick body pitch in semitones above the 808 root (0 = unison, 7 = fifth, 12 = octave):
    /// the kick follows the key, so kick and 808 never beat against each other.
    int kickInterval = 0;
    /// Sidechain under the kick: 808 and perc duck depth (0..1) and release (seconds).
    float subDuck = 0.0f;
    float percDuck = 0.0f;
    float duckRelease = 0.12f;
};

StyleKit styleKit(Style style) noexcept;

/// The kick pitch for `rootNote`: the note `interval` semitones above it, moved by whole
/// octaves to the one closest to `nominalHz` (so the kick stays in its register).
float rootTunedKickHz(int rootNote, int interval, float nominalHz) noexcept;

/// Writes the kit into parts 0..4 of `engine` (kick, snare, hat, perc, 808), with the
/// 808 tuned to `rootNote`. Parts whose bit is set in `skipMask` keep their sound.
void applyKit(const StyleKit& kit, Engine& engine, int rootNote, std::uint32_t skipMask = 0) noexcept;

} // namespace zd::gen
