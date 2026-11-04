// Part and kit files: save, swap, load.
#include <doctest/doctest.h>

#include "PluginProcessor.h"

#include <memory>

namespace {

float plain(ZeroDrumProcessor& p, const char* id)
{
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    return param->convertFrom0to1(param->getValue());
}

void set(ZeroDrumProcessor& p, const char* id, float v)
{
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(v));
}

juce::File tempFile(const char* name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("zd_plugin_tests").getChildFile(name);
}

} // namespace

TEST_CASE("Parts: every per-part parameter belongs to exactly the part it names")
{
    CHECK(ZeroDrumProcessor::partOfParam("kickTune") == 0);
    CHECK(ZeroDrumProcessor::partOfParam("kickCutsSub") == 0);
    CHECK(ZeroDrumProcessor::partOfParam("clapSnap") == 1);
    CHECK(ZeroDrumProcessor::partOfParam("hatDecay") == 2);
    CHECK(ZeroDrumProcessor::partOfParam("percDuck") == 3);
    CHECK(ZeroDrumProcessor::partOfParam("subDuck") == 4);
    CHECK(ZeroDrumProcessor::partOfParam("p1Cutoff") == 1);
    CHECK(ZeroDrumProcessor::partOfParam("p12Level") == 12);
    CHECK(ZeroDrumProcessor::partOfParam("p15RevSend") == 15);
    CHECK(ZeroDrumProcessor::partOfParam("revDecay") == -1);
    CHECK(ZeroDrumProcessor::partOfParam("glueRatio") == -1);
    CHECK(ZeroDrumProcessor::partOfParam("lock0") == -1);
    CHECK(ZeroDrumProcessor::partOfParam("genStyle") == -1);
    CHECK(ZeroDrumProcessor::partOfParam("swing") == -1);
}

TEST_CASE("Parts: a saved snare comes back with its sound and lane, and nothing else moves")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    set(*p, "snareTune", 260.0f);
    set(*p, "p1Cutoff", 3000.0f);
    set(*p, "revDecay", 2.0f);
    p->editPattern([](zd::Pattern& pat) {
        pat.lanes[1] = zd::Lane {};
        pat.lanes[1].length = 8;
        pat.lanes[1].steps[3] = zd::Step { true, 0.6f, 1.0f, 0.0f, -1, 2 };
    });
    const auto file = tempFile("snare.zdpart");
    file.getParentDirectory().createDirectory();
    REQUIRE(p->savePart(1, file));

    set(*p, "snareTune", 180.0f);
    set(*p, "p1Cutoff", 9000.0f);
    set(*p, "revDecay", 3.0f);
    p->editPattern([](zd::Pattern& pat) { pat.lanes[1] = zd::Lane {}; });

    CHECK_FALSE(p->loadPart(0, file)); // a snare does not fit the kick slot
    REQUIRE(p->loadPart(1, file));
    CHECK(plain(*p, "snareTune") == doctest::Approx(260.0f).epsilon(0.01));
    CHECK(plain(*p, "p1Cutoff") == doctest::Approx(3000.0f).epsilon(0.01));
    CHECK(plain(*p, "revDecay") == doctest::Approx(3.0f).epsilon(0.01)); // the bus is not the part's
    const auto lane = p->currentPattern().lanes[1];
    CHECK(lane.length == 8);
    CHECK(lane.steps[3].on);
    CHECK(lane.steps[3].ratchet == 2);
}

TEST_CASE("Parts: a pad part moves between pads; a kit file restores everything")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    set(*p, "p7SmpPitch", 5.0f);
    set(*p, "p7Level", -12.0f);
    const auto part = tempFile("pad.zdpart");
    part.getParentDirectory().createDirectory();
    REQUIRE(p->savePart(7, part));
    CHECK_FALSE(p->loadPart(2, part)); // pads only go to pads
    REQUIRE(p->loadPart(11, part, false));
    CHECK(plain(*p, "p11SmpPitch") == doctest::Approx(5.0f).epsilon(0.01));
    CHECK(plain(*p, "p11Level") == doctest::Approx(-12.0f).epsilon(0.01));

    set(*p, "glueRatio", 6.0f);
    set(*p, "kickTune", 70.0f);
    const auto kit = tempFile("all.zdkit");
    REQUIRE(p->saveKit(kit));
    set(*p, "glueRatio", 2.0f);
    set(*p, "kickTune", 45.0f);
    REQUIRE(p->loadKit(kit));
    CHECK(plain(*p, "glueRatio") == doctest::Approx(6.0f).epsilon(0.01));
    CHECK(plain(*p, "kickTune") == doctest::Approx(70.0f).epsilon(0.01));
    CHECK(plain(*p, "p11SmpPitch") == doctest::Approx(5.0f).epsilon(0.01));
    CHECK_FALSE(p->loadKit(part)); // a part file is not a kit
}
