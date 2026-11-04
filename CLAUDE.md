# Zero Drum: notes for Claude

Closed-source drum machine plugin (CLAP, VST3, AU, standalone), solo developer, desktop only.
Design doc: https://claude.ai/artifact/5MaVGW6f8HcNhZeR3Swyj4. Scope is v1 + v2; moonshots decided later.

## Rules

- `engine/` must never include JUCE. Only `plugin/` talks to JUCE.
- Audio-thread code (anything reachable from `Engine::process`) must not allocate, lock or do I/O.
  No `std::stable_sort`, `std::vector::push_back`, `new`, or `std::function` there.
- Keep output bit-identical for any block size; the null test in `tests/test_engine.cpp` enforces it.
- Randomness is seeded (`zd::dsp::Random`, `zd::dsp::hash64`), never `rand()` or time-based.
- New nonlinear stages use ADAA (`zd/dsp/Adaa.h`) or oversampling, with an aliasing test.
- Dependencies are pinned in `cmake/Dependencies.cmake`; third-party code must be permissively licensed.

## Git and pushing

- Batch a lot of work locally before any push: commit on local branches, many features per push.
- Always ask Basspotato before pushing to the repo, and wait for a yes. That covers `git push`,
  opening or updating a pull request, and re-running CI.
- Until then, keep work safe outside the container (for example as patch files).

## Checks before pushing

```sh
cmake -S . -B build-tests -G Ninja -DZD_BUILD_PLUGIN=OFF && cmake --build build-tests && ./build-tests/tests/zd_tests
```

Full plugin build + pluginval runs in CI (`.github/workflows/ci.yml`) on Linux, macOS and Windows.
