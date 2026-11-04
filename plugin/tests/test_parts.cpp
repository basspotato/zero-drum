// Part and kit files: save, swap, load.
#include <doctest/doctest.h>

#include "PluginProcessor.h"
#include "zd/PatternIO.h"

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

TEST_CASE("Undo: pattern edits, Generate and lane tools step back and forward")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    const auto original = p->currentPattern();
    CHECK_FALSE(p->canUndo());

    p->pushUndo();
    p->editPattern([](zd::Pattern& pat) { pat.lanes[1].steps[2].on = !pat.lanes[1].steps[2].on; });
    p->euclidLane(3, 5);
    p->setLaneLength(2, 12);
    p->generateBeat();
    const auto generated = p->currentPattern();
    REQUIRE(p->undo()); // generate
    REQUIRE(p->undo()); // length
    CHECK(p->currentPattern().lanes[2].length != 12);
    REQUIRE(p->undo()); // euclid
    REQUIRE(p->undo()); // the click
    CHECK(p->currentPattern().lanes[1].steps[2].on == original.lanes[1].steps[2].on);
    CHECK(p->currentPattern().lanes[3].steps[0].on == original.lanes[3].steps[0].on);
    CHECK_FALSE(p->undo());
    for (int i = 0; i < 4; ++i)
        REQUIRE(p->redo());
    CHECK_FALSE(p->redo());
    CHECK(p->currentPattern().lanes[0].steps[0].on == generated.lanes[0].steps[0].on);
    CHECK(zd::serializePattern(p->currentPattern()) == zd::serializePattern(generated));
}

TEST_CASE("Slots: a new slot copies the current one, edits stay in their slot, and the state keeps all four")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    const auto a = zd::serializePattern(p->currentPattern());
    p->selectSlot(1);
    CHECK(p->editSlot() == 1);
    CHECK(zd::serializePattern(p->currentPattern()) == a); // B starts as a copy of A
    p->editPattern([](zd::Pattern& pat) { pat.lanes[1].length = 32; pat.lanes[1].steps[20].on = true; });
    const auto b = zd::serializePattern(p->currentPattern());
    CHECK(b != a);
    p->selectSlot(0);
    CHECK(zd::serializePattern(p->currentPattern()) == a);
    p->setChainCount(2);
    CHECK(p->chainCount() == 2);

    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto q = std::make_unique<ZeroDrumProcessor>();
    q->prepareToPlay(48000.0, 512);
    q->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(q->chainCount() == 2);
    CHECK(q->editSlot() == 0);
    CHECK(zd::serializePattern(q->currentPattern()) == a);
    q->selectSlot(1);
    CHECK(zd::serializePattern(q->currentPattern()) == b);

    // B is two bars long, so a chain plays A for one bar and B for two.
    CHECK(zd::patternBars(q->currentPattern()) == 2);
}

TEST_CASE("Slots: copy, paste, clear and variation; auto fill makes the export longer")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    const auto a = zd::serializePattern(p->currentPattern());
    CHECK_FALSE(p->hasSlotCopy());
    p->copySlot(0);
    REQUIRE(p->hasSlotCopy());
    p->clearSlot(0);
    for (const auto& lane : p->currentPattern().lanes)
        for (const auto& st : lane.steps)
            REQUIRE_FALSE(st.on);
    REQUIRE(p->undo()); // clearing the edited slot is undoable
    CHECK(zd::serializePattern(p->currentPattern()) == a);
    REQUIRE(p->pasteSlot(2));
    p->selectSlot(2);
    CHECK(zd::serializePattern(p->currentPattern()) == a);

    p->selectSlot(0);
    const int target = p->makeVariation();
    CHECK(target == 1);
    CHECK(p->editSlot() == 0);
    p->selectSlot(1);
    const auto variation = zd::serializePattern(p->currentPattern());
    CHECK(variation != a);
    // Seeded: the same session steps give the same variation.
    auto q = std::make_unique<ZeroDrumProcessor>();
    q->prepareToPlay(48000.0, 512);
    q->makeVariation();
    q->selectSlot(1);
    CHECK(zd::serializePattern(q->currentPattern()) == variation);

    // Auto fill on a lone slot: three plain passes and a fill, so the MIDI clip is 4x as long.
    p->selectSlot(0);
    const auto plainClip = p->writeMidiClip(-1);
    REQUIRE(plainClip.existsAsFile());
    const auto plainSize = plainClip.getSize();
    p->setAutoFill(true);
    CHECK(p->autoFill());
    const auto filledClip = p->writeMidiClip(-1);
    REQUIRE(filledClip.existsAsFile());
    CHECK(filledClip.getSize() > 5 * plainSize / 2); // 4x the notes; the track headers stay the same size
}

TEST_CASE("Pads: reverse is a per-pad parameter that travels with a part file")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    set(*p, "p9SmpReverse", 1.0f);
    CHECK(ZeroDrumProcessor::partOfParam("p9SmpReverse") == 9);
    CHECK(plain(*p, "p9SmpReverse") > 0.5f);
}
