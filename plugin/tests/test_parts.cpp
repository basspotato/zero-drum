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

TEST_CASE("Lane tools: generate one lane, double, reverse; all undoable")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    const auto before = p->currentPattern();

    p->generateLane(2);
    const auto after = p->currentPattern();
    for (int lane : { 0, 1, 3, 4 })
        for (int s = 0; s < zd::kMaxStepsPerLane; ++s)
            REQUIRE(after.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(s)].on
                    == before.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(s)].on);
    REQUIRE(p->undo());
    CHECK(zd::serializePattern(p->currentPattern()) == zd::serializePattern(before));

    const int n = before.lanes[1].length;
    p->doubleLane(1);
    auto doubled = p->currentPattern().lanes[1];
    CHECK(doubled.length == 2 * n);
    for (int s = 0; s < n; ++s)
        CHECK(doubled.steps[static_cast<std::size_t>(s + n)].on == doubled.steps[static_cast<std::size_t>(s)].on);
    REQUIRE(p->undo());

    p->reverseLane(0);
    const auto rev = p->currentPattern().lanes[0];
    const int k = before.lanes[0].length;
    for (int s = 0; s < k; ++s)
        CHECK(rev.steps[static_cast<std::size_t>(s)].on == before.lanes[0].steps[static_cast<std::size_t>(k - 1 - s)].on);
    REQUIRE(p->undo());
    CHECK(zd::serializePattern(p->currentPattern()) == zd::serializePattern(before));
}

TEST_CASE("Names, tap tempo, choke groups and full level")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);

    // Names: custom, saved with the session, empty resets.
    p->setPartName(1, "Big Clap");
    p->setPartName(9, "Shaker");
    CHECK(p->padName(1) == "Big Clap");
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto q = std::make_unique<ZeroDrumProcessor>();
    q->prepareToPlay(48000.0, 512);
    q->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(q->padName(1) == "Big Clap");
    CHECK(q->padName(9) == "Shaker");
    q->setPartName(1, "  ");
    CHECK(q->padName(1) == "Snare");

    // Tap tempo: taps half a second apart are 120 BPM.
    CHECK(p->tapTempo(10.0) == doctest::Approx(0.0));
    CHECK(p->tapTempo(10.5) == doctest::Approx(120.0));
    CHECK(p->tapTempo(11.0) == doctest::Approx(120.0));
    CHECK(plain(*p, "freeRunBpm") == doctest::Approx(120.0f).epsilon(0.01));
    CHECK(p->tapTempo(20.0) == doctest::Approx(0.0)); // a long gap starts over

    // Choke groups and full level are plain parameters.
    set(*p, "p6Choke", 2.0f);
    CHECK(ZeroDrumProcessor::partOfParam("p6Choke") == 6);
    CHECK(plain(*p, "p6Choke") == doctest::Approx(2.0f));
    CHECK_FALSE(p->fullLevelPads());
    set(*p, "padFullLevel", 1.0f);
    CHECK(p->fullLevelPads());
}

TEST_CASE("Lane copy and paste across slots, ramps, reset sound, one-part audio")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    const auto hat = p->currentPattern().lanes[2];
    p->copyLane(2);
    REQUIRE(p->hasLaneCopy());
    p->selectSlot(1);
    p->clearSlot(1);
    REQUIRE(p->pasteLane(3)); // the hat rhythm onto the perc lane of slot B
    const auto pasted = p->currentPattern().lanes[3];
    CHECK(pasted.length == hat.length);
    for (int s = 0; s < hat.length; ++s)
        CHECK(pasted.steps[static_cast<std::size_t>(s)].on == hat.steps[static_cast<std::size_t>(s)].on);

    p->rampLane(3, true);
    const auto ramp = p->currentPattern().lanes[3];
    CHECK(ramp.steps[0].velocity == doctest::Approx(0.3f));
    CHECK(ramp.steps[static_cast<std::size_t>(ramp.length - 1)].velocity == doctest::Approx(1.0f));

    set(*p, "kickTune", 80.0f);
    set(*p, "p0Cutoff", 900.0f);
    p->resetPartSound(0);
    CHECK(plain(*p, "kickTune") == doctest::Approx(50.0f).epsilon(0.01));
    CHECK(plain(*p, "p0Cutoff") != doctest::Approx(900.0f));

    CHECK(p->defaultNote(4) == static_cast<int>(plain(*p, "genRoot")));
    CHECK(p->defaultNote(9) == 60);
    CHECK(p->defaultNote(2) == -1);

    p->selectSlot(0);
    const auto all = p->writeLoopWav();
    REQUIRE(all.existsAsFile());
    const auto allSize = all.getSize();
    const auto kick = p->writeLoopWav(0);
    REQUIRE(kick.existsAsFile());
    CHECK(kick.getFileName().contains("Kick"));
    CHECK(kick.getSize() == allSize); // same length, just one part in it
}

TEST_CASE("Zoom and tempo readout: the zoom is clamped and saved; tempo follows free run")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = std::make_unique<ZeroDrumProcessor>();
    p->prepareToPlay(48000.0, 512);
    CHECK(p->uiZoom() == doctest::Approx(1.0f));
    p->setUiZoom(3.0f);
    CHECK(p->uiZoom() == doctest::Approx(1.5f));
    p->setUiZoom(0.1f);
    CHECK(p->uiZoom() == doctest::Approx(0.75f));
    p->setUiZoom(1.25f);
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto q = std::make_unique<ZeroDrumProcessor>();
    q->prepareToPlay(48000.0, 512);
    q->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(q->uiZoom() == doctest::Approx(1.25f));

    set(*p, "freeRun", 1.0f);
    set(*p, "freeRunBpm", 140.0f);
    CHECK(p->currentBpm() == doctest::Approx(140.0).epsilon(0.01));
}
