#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace zd;

namespace {

SampleDataPtr click(float level, int length = 2000)
{
    auto s = std::make_shared<SampleData>();
    s->samples.assign(static_cast<std::size_t>(length), level);
    s->sampleRate = 48000.0;
    return s;
}

std::vector<float> hit(Engine& e, int part)
{
    std::vector<float> l(512), r(512);
    NoteEvent ev { 0, part, 1.0f, -1 };
    e.process(Transport {}, &ev, 1, l.data(), r.data(), 512);
    return l;
}

} // namespace

TEST_CASE("Samples: submitSample turns an empty pad into a sample part at the next block")
{
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 512);
    auto& pad = e->part(9);
    pad.mix.gainDb = 0.0f;
    CHECK(pad.type == EngineType::Off);
    CHECK(test::peak(hit(*e, 9)) == 0.0);

    std::weak_ptr<const SampleData> first;
    {
        auto s = click(0.5f);
        first = s;
        e->submitSample(9, std::move(s));
    }
    CHECK(pad.type == EngineType::Off); // nothing changes until the audio thread runs
    CHECK(test::peak(hit(*e, 9)) > 0.2);
    CHECK(pad.type == EngineType::Sample);

    // Replacing keeps the old buffer alive until collectSamples() on this thread, so the
    // audio thread never frees memory.
    e->submitSample(9, click(0.25f));
    CHECK_FALSE(first.expired());
    const double louder = test::peak(hit(*e, 9));
    CHECK(louder < 0.2);
    CHECK_FALSE(first.expired()); // the audio thread dropped its reference, not the last one
    e->collectSamples();
    CHECK(first.expired());

    // nullptr clears the pad.
    e->submitSample(9, nullptr);
    std::vector<float> l(512), r(512);
    e->process(Transport {}, nullptr, 0, l.data(), r.data(), 512);
    CHECK(pad.type == EngineType::Off);
}

TEST_CASE("Samples: a pending sample that never reached the audio thread is replaced")
{
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 512);
    e->part(6).mix.gainDb = 0.0f;
    std::weak_ptr<const SampleData> skipped;
    {
        auto s = click(0.9f);
        skipped = s;
        e->submitSample(6, std::move(s));
    }
    e->submitSample(6, click(0.3f));
    CHECK(skipped.expired()); // freed on this thread, before audio ever saw it
    const double p = test::peak(hit(*e, 6));
    CHECK(p > 0.2);
    CHECK(p < 0.5);
}

TEST_CASE("Samples: hand-offs from another thread while audio runs stay consistent")
{
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 256);
    for (int p = 5; p < kMaxParts; ++p)
        e->part(p).mix.gainDb = -20.0f;
    std::atomic<bool> stop { false };
    std::thread ui([&] {
        for (int k = 0; k < 2000; ++k)
        {
            e->submitSample(5 + k % 11, click(0.1f + 0.0004f * static_cast<float>(k), 300 + k % 700));
            if (k % 7 == 0)
                e->collectSamples();
        }
        stop = true;
    });
    std::vector<float> l(256), r(256);
    std::array<NoteEvent, 11> evs {};
    for (int p = 0; p < 11; ++p)
        evs[static_cast<std::size_t>(p)] = { p * 20, 5 + p, 0.8f, -1 };
    bool finite = true;
    while (!stop.load())
    {
        e->process(Transport {}, evs.data(), 11, l.data(), r.data(), 256);
        finite = finite && test::allFinite(l) && test::allFinite(r);
    }
    ui.join();
    e->process(Transport {}, evs.data(), 11, l.data(), r.data(), 256);
    e->collectSamples();
    CHECK(finite);
    for (int p = 5; p < kMaxParts; ++p)
        CHECK(e->part(p).type == EngineType::Sample);
}
