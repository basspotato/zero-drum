# Zero Drum

A hybrid synth-and-sample drum machine for EDM, hip-hop, electronic and pop. CLAP, VST3, AU and
standalone. Fully local: no cloud, no account, no subscription.

This repository is at **Phase 0 (Foundations)**: the engine core, the first sound engines, the JUCE
plugin shell and the DSP test harness. See the design doc for the full plan.

## Layout

| Path | What it is |
| --- | --- |
| `engine/` | Framework-free C++20 engine (`zd_engine`). Never includes JUCE. |
| `engine/include/zd/dsp/` | DSP building blocks: ADAA saturation, ZDF state-variable filter, seeded RNG |
| `engine/include/zd/engines/` | Voice engines: analog kick, analog snare, sample player, FM percussion, 808 sub |
| `engine/include/zd/Strip.h` | Per-part channel strip: transient shaper, filter, drive |
| `engine/include/zd/gen/` | Rule-based beat generator: 17 styles, density, complexity, locks, mutate, fill |
| `engine/include/zd/PatternIO.h` | Versioned plain-text pattern format (plugin state, future .zdloop) |
| `engine/include/zd/Sequencer.h` | Sample-accurate step sequencer: swing, micro-timing, probability, polymeter |
| `plugin/` | JUCE 9 shell: parameters, MIDI, host transport; builds CLAP, VST3, AU, Standalone |
| `tests/` | doctest suite: block-size null test, aliasing test, determinism, engine behaviour |
| `tools/render.cpp` | `zd_render`: renders the default beat to a WAV, no JUCE needed |

## Build

Requirements: CMake 3.22+, a C++20 compiler (Clang 15+, GCC 12+, MSVC 2022), Ninja recommended.
JUCE, clap-juce-extensions and doctest are fetched automatically at pinned versions.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Plugins land in `build/plugin/ZeroDrum_artefacts/Release/{CLAP,VST3,AU,Standalone}`.

Engine only (fast, no JUCE download): add `-DZD_BUILD_PLUGIN=OFF`.

On Linux, install the JUCE dependencies first:

```sh
sudo apt-get install libasound2-dev libcurl4-openssl-dev libfontconfig1-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libgl1-mesa-dev
```

Render the demo beat: `./build/zd_render demo.wav 8 94` (bars, BPM).
Render a generated beat: `./build/zd_render trap.wav 8 0 48000 7 0` (BPM 0 = style tempo, seed 7, style 0 = trap).
Export a loop with stems and MIDI: `./build/zd_render out/ 2 0 48000 7 0` (a path ending in `/` is a folder).

## Playing it

- Press play in the host: the default pattern follows the host tempo and position.
- In the standalone app (no host transport), turn on **Free Run**.
- MIDI: note 36 = kick, 38 = snare, 42 = hat; notes 36 to 51 map to parts 0 to 15.
- MIDI channel 2 plays the 808 melodically (with glide between overlapping notes).
- Default kit: kick (part 0), snare (1), hat (2), FM perc (3), 808 (4).
- Generator bar: pick a style, then **Generate** (new beat), **Mutate** (small changes), **Fill**.
  Lock parameters keep a lane while you regenerate the rest. The beat is saved with the project.
- **Drag MIDI**: drag the **Beat** chip (all lanes) or one lane's chip onto a DAW track to drop
  the pattern as a MIDI clip. Drums use channel 10 with the notes above (36 kick, 38 snare,
  42 hat, 39 perc); the 808 uses channel 2 with its notes, so the clip plays back the same on
  Zero Drum or drives any other drum rack or synth.
- **Export...**: pick a folder and get a timestamped folder with `mix.wav`, `parts/` (one WAV per
  part), `midi/` (one MIDI file per part plus `all-parts.mid`) and `pattern.zdpattern`.
  The part WAVs are rendered one at a time and sum exactly to the mix, so there is never any
  stem separation. Files loop seamlessly: decays from the end of the loop carry into its start.
  Tempo is the host's (or the Free Run tempo).

## Engineering rules

- The engine never includes JUCE or OS headers; the plugin is a thin shell.
- Nothing on the audio thread allocates, locks, waits or touches disk.
- Output must be bit-identical for any host block size (enforced by the null test).
- All randomness is seeded; the same project renders the same audio every time.
- Every nonlinearity is anti-aliased (ADAA) or oversampled.

## Licence

Proprietary, all rights reserved. See `LICENSE`. JUCE is used under its commercial licence terms;
check the JUCE licence tier before distributing builds.
