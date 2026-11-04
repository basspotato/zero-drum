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
| `engine/include/zd/gen/` | Rule-based beat generator: 17 styles, density, complexity, groove (per-style timing and accents), locks, mutate, fill |
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
- **Groove**: each style has its own pocket on top of its swing. Boom bap pushes the kick and
  lays the snare back, house leans the offbeat hat forward and accents it, garage and amapiano
  drag the 16th pickups, and the EDM styles stay locked to the grid. Hats and perc get a small
  seeded human spread, the 808 stays locked to the kick, and snare ghost notes use the style's
  ghost velocity. **Groove** 0 gives a straight grid with flat accents; 1 (default) is the full feel.
  Groove only moves timing and dynamics, never which steps play.
- **Build**: replaces the beat with a 4-bar build-up into the drop: a snare roll rising from
  8ths to 16ths to 32nds to 64ths with a velocity ramp, the kick dropping out for the last bar,
  then an 8th of silence. Locked lanes stay. Drag it out as MIDI like any beat.
- **808 slides**: in trap, drill and amapiano, about 30% of bars move the last 808 hit to a
  fourth or fifth below the root or the octave above, so the 808 glides into it.
- **Style kits**: with **Load Style Kit** on (the default), Generate also tunes the five parts for
  the style (kick, snare, hat, perc, 808 settings, strips and levels) and sets the 808 root to the
  style's home key. The kick is tuned to a fixed interval above the 808 root, so the two never
  clash. Locked parts keep their sound. The numbers come from the genre tuning guide, and every
  style is level-matched to peak around -1.6 dBFS.
- **Drag MIDI**: drag the **Beat** chip (all lanes) or one lane's chip onto a DAW track to drop
  the pattern as a MIDI clip. Drums use channel 10 with the notes above (36 kick, 38 snare,
  42 hat, 39 perc); the 808 uses channel 2 with its notes, so the clip plays back the same on
  Zero Drum or drives any other drum rack or synth.
- **Master clipper** (on by default): a 4x oversampled soft-knee clipper on the output, with
  **Clipper Drive** for loudness and punch and **Clipper Ceiling** (default -1 dBFS). The plugin
  reports a fixed 64-sample latency (1.3 ms at 48 kHz) whether the clipper is on or off, so the
  host's delay compensation never changes.
- **Clap engine and layers**: the snare lane plays the analog snare or a clap (several hands a
  few ms apart, then the room tail: **Clap Tone**, **Spread**, **Hands**, **Tail**, **Snap**). Any
  part can stack a second engine on every hit: **Snare Layer** puts a clap on the snare (or a
  snare under the clap). Style kits choose: claps for house, techno, trance, Jersey club and
  hardstyle; a snare plus clap stack for trap, drill, dubstep, festival, pop and reggaeton.
- **Crush** (per part, in the strip): bit depth (**Crush Bits**, 4 to 16) and a sample-and-hold
  rate (**Crush Rate**, 4 to 48 kHz; 48 kHz means no rate reduction), with an anti-alias
  **Crush Filter** before the hold. The boom bap kit puts every drum through the classic 12-bit,
  26.04 kHz sampler; UK garage takes 12 bits on snare and hats. The 808 stays clean.
- **Velocity to Tone** (per part): softer hits are shorter and darker (snare wires lower, clap
  body lower, kick click and FM brightness down), so ghost notes sound like ghost notes, not
  quiet copies. Strongest in the boom bap, DnB, garage and hip-hop kits. The 808 never changes.
- **Chokes**: **808 Cuts Kick** stops the kick's tail when the 808 plays on its own (on in the
  trap and drill kits); **Kick Cuts 808** does the reverse. Hits on the same sample never cut
  each other. The engine supports any part cutting any others (open and closed hats).
- **Reverb**: one shared stereo reverb (8-line feedback delay network) with **Reverb Decay**,
  **Size**, **Pre-delay**, **Damping**, a **High-pass** on the return (keeps the low end dry) and
  **Level**. Each part has a post-fader **Reverb Send**; the 808 stays dry. Style kits set it:
  a big 40% wet clap for dubstep, a plate on pop snares, a dark rumble under the techno kick,
  short rooms elsewhere.
- **Sidechain**: every kick ducks the 808 (**808 Sidechain**) and optionally the perc
  (**Perc Sidechain**), with a 3 ms click-free attack and **Sidechain Release**. Style kits set
  it: a short duck for trap-family styles, a full pump for house, trance and festival.
- **Export...**: pick a folder and get a timestamped folder with `mix.wav`, `parts/` (one WAV per
  part), `midi/` (one MIDI file per part plus `all-parts.mid`) and `pattern.zdpattern`.
  The part WAVs are rendered one at a time, before the master clipper, so they sum exactly to
  the unclipped mix (like stems from a mix bus) and there is never any stem separation.
  `mix.wav` goes through the clipper. Files loop seamlessly: decays from the end of the loop carry into its start.
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
