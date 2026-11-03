#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/engines/AnalogKick.h"
#include "zd/engines/AnalogSnare.h"
#include "zd/engines/SamplePlayer.h"

#include <vector>

using namespace zd;

namespace {

template <typename V>
std::vector<float> renderVoice(V& voice, int numSamples, int blockSize)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; i += blockSize)
        voice.render(out.data() + i, std::min(blockSize, numSamples - i));
    return out;
}

} // namespace

TEST_CASE("Kick: produces sound, ends, and decays to silence")
{
    AnalogKickVoice kick;
    kick.prepare(48000.0);
    KickParams p;
    p.decay = 0.3f;
    kick.trigger(p, 1.0f);
    CHECK(kick.isActive());

    const auto out = renderVoice(kick, 48000, 256);
    CHECK(zd::test::allFinite(out));
    CHECK(zd::test::peak(out) > 0.3);
    CHECK(zd::test::peak(out) < 1.5);
    CHECK_FALSE(kick.isActive());
    // After the voice ends, output is exactly zero (no denormal tails).
    for (std::size_t i = 40000; i < out.size(); ++i)
        REQUIRE(out[i] == 0.0f);
}

TEST_CASE("Kick: output is independent of render block size")
{
    KickParams p;
    AnalogKickVoice a, b;
    a.prepare(44100.0);
    b.prepare(44100.0);
    a.trigger(p, 0.8f);
    b.trigger(p, 0.8f);
    const auto x = renderVoice(a, 30000, 1);
    const auto y = renderVoice(b, 30000, 4096);
    CHECK(x == y);
}

TEST_CASE("Kick: pitch sweep starts high and settles at the tune frequency")
{
    // Count zero crossings early vs late to estimate frequency.
    AnalogKickVoice kick;
    kick.prepare(48000.0);
    KickParams p;
    p.tuneHz = 50.0f;
    p.pitchAmount = 4.0f;
    p.click = 0.0f;
    p.drive = 0.0f;
    p.decay = 2.0f;
    kick.trigger(p, 1.0f);
    const auto out = renderVoice(kick, 48000, 512);

    auto crossings = [&](int from, int to) {
        int c = 0;
        for (int i = from + 1; i < to; ++i)
            if ((out[static_cast<std::size_t>(i - 1)] < 0.0f) != (out[static_cast<std::size_t>(i)] < 0.0f))
                ++c;
        return c;
    };
    const double early = crossings(0, 960) / 2.0 / 0.02;      // first 20 ms
    const double late = crossings(24000, 48000) / 2.0 / 0.5;  // 0.5 s .. 1 s
    CHECK(early > 120.0);
    CHECK(late == doctest::Approx(50.0).epsilon(0.05));
}

TEST_CASE("Kick: kill fades out within a few milliseconds")
{
    AnalogKickVoice kick;
    kick.prepare(48000.0);
    kick.trigger(KickParams {}, 1.0f);
    std::vector<float> tmp(1000, 0.0f);
    kick.render(tmp.data(), 1000);
    kick.kill();
    std::vector<float> out(480, 0.0f);
    kick.render(out.data(), 480); // 10 ms
    CHECK_FALSE(kick.isActive());
}

TEST_CASE("Snare: produces a finite, bounded hit that ends")
{
    AnalogSnareVoice snare;
    snare.prepare(48000.0);
    snare.trigger(SnareParams {}, 1.0f);
    const auto out = renderVoice(snare, 48000, 128);
    CHECK(zd::test::allFinite(out));
    CHECK(zd::test::peak(out) > 0.1);
    CHECK(zd::test::peak(out) < 1.5);
    CHECK_FALSE(snare.isActive());
}

TEST_CASE("Sample player: unity pitch at equal rates reproduces the sample exactly")
{
    SampleData data;
    data.sampleRate = 48000.0;
    for (int i = 0; i < 1000; ++i)
        data.samples.push_back(static_cast<float>(std::sin(i * 0.1)));

    SamplePlayerVoice voice;
    voice.prepare(48000.0);
    voice.trigger(&data, SampleParams {}, 1.0f);
    const auto out = renderVoice(voice, 1200, 64);
    for (int i = 0; i < 999; ++i)
        REQUIRE(out[static_cast<std::size_t>(i)] == doctest::Approx(data.samples[static_cast<std::size_t>(i)]).epsilon(1e-6));
    CHECK_FALSE(voice.isActive());
}

TEST_CASE("Sample player: +12 semitones plays twice as fast")
{
    SampleData data;
    data.sampleRate = 48000.0;
    data.samples.assign(4800, 0.5f);

    SamplePlayerVoice voice;
    voice.prepare(48000.0);
    SampleParams p;
    p.pitchSemitones = 12.0f;
    voice.trigger(&data, p, 1.0f);
    const auto out = renderVoice(voice, 4800, 256);
    int nonZero = 0;
    for (float v : out)
        if (v != 0.0f)
            ++nonZero;
    CHECK(nonZero == doctest::Approx(2400).epsilon(0.01));
}

TEST_CASE("Synth hat sample is short, finite and audible")
{
    const auto hat = makeSynthHatSample(48000.0);
    REQUIRE(hat.get() != nullptr);
    CHECK(hat->samples.size() == 12000);
    CHECK(zd::test::allFinite(hat->samples));
    CHECK(zd::test::peak(hat->samples) > 0.05);
    CHECK(zd::test::peak(hat->samples) < 1.5);
}
