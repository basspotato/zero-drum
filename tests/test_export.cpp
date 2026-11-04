#include "TestUtils.h"

#include <doctest/doctest.h>

#include "zd/Engine.h"
#include "zd/PatternIO.h"
#include "zd/export/Export.h"
#include "zd/gen/Generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <set>
#include <vector>

using namespace zd;
namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> readBytes(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return (static_cast<std::uint32_t>(b[at]) << 24) | (b[at + 1] << 16) | (b[at + 2] << 8) | b[at + 3];
}

std::vector<float> wavSamples(const fs::path& p)
{
    const auto b = readBytes(p);
    REQUIRE(b.size() >= 44);
    REQUIRE(std::string(b.begin(), b.begin() + 4) == "RIFF");
    REQUIRE(std::string(b.begin() + 36, b.begin() + 40) == "data");
    const std::uint32_t bytes = le32(b, 40);
    REQUIRE(b.size() == 44 + bytes);
    std::vector<float> out(bytes / 4);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const std::uint32_t bits = le32(b, 44 + 4 * i);
        std::memcpy(&out[i], &bits, 4);
    }
    return out;
}

/// Parses a standard MIDI file and counts note-ons per track (tempo track included).
struct MidiInfo
{
    int format = -1, tracks = 0, ppq = 0;
    std::vector<int> noteOns, noteOffs;
    std::vector<std::string> names;
};

MidiInfo parseMidi(const fs::path& p)
{
    const auto b = readBytes(p);
    MidiInfo info;
    REQUIRE(b.size() >= 14);
    REQUIRE(std::string(b.begin(), b.begin() + 4) == "MThd");
    REQUIRE(be32(b, 4) == 6);
    info.format = (b[8] << 8) | b[9];
    info.tracks = (b[10] << 8) | b[11];
    info.ppq = (b[12] << 8) | b[13];
    std::size_t pos = 14;
    for (int t = 0; t < info.tracks; ++t)
    {
        REQUIRE(pos + 8 <= b.size());
        REQUIRE(std::string(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos) + 4) == "MTrk");
        const std::size_t end = pos + 8 + be32(b, pos + 4);
        REQUIRE(end <= b.size());
        pos += 8;
        int ons = 0, offs = 0;
        std::string name;
        bool sawEnd = false;
        auto varLen = [&] {
            std::uint32_t v = 0;
            for (;;)
            {
                const std::uint8_t c = b[pos++];
                v = (v << 7) | (c & 0x7f);
                if (!(c & 0x80))
                    return v;
            }
        };
        while (pos < end)
        {
            varLen();
            const std::uint8_t status = b[pos++];
            if (status == 0xFF)
            {
                const std::uint8_t type = b[pos++];
                const std::uint32_t len = varLen();
                if (type == 0x03)
                    name.assign(b.begin() + static_cast<long>(pos), b.begin() + static_cast<long>(pos + len));
                if (type == 0x2F)
                    sawEnd = true;
                pos += len;
            }
            else
            {
                REQUIRE((status & 0x80) != 0); // no running status in our files
                const int kind = status & 0xF0;
                ons += kind == 0x90 && b[pos + 1] > 0;
                offs += kind == 0x80;
                pos += 2;
            }
        }
        REQUIRE(pos == end);
        CHECK(sawEnd);
        info.noteOns.push_back(ons);
        info.noteOffs.push_back(offs);
        info.names.push_back(name);
    }
    REQUIRE(pos == b.size());
    return info;
}

std::unique_ptr<Engine> makeEngine(gen::Style style, std::uint64_t seed)
{
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 512);
    loadDefaultKit(*e);
    e->setMasterGainDb(Engine::kDefaultMasterDb);
    gen::GenerateParams p;
    p.style = style;
    p.seed = seed;
    p.complexity = 0.8f;
    gen::generate(p, e->sequencer().pattern());
    return e;
}

fs::path tempDir(const char* name)
{
    const fs::path d = fs::temp_directory_path() / ("zd-test-" + std::string(name));
    fs::remove_all(d);
    return d;
}

} // namespace

TEST_CASE("Export: part files sum to the mix, have the exact loop length and loop seamlessly")
{
    auto e = makeEngine(gen::Style::Trap, 7);
    exporting::ExportOptions opt;
    opt.bars = 2;
    opt.bpm = 140.0;
    opt.partNames = { "Kick", "Snare", "Hat", "Perc", "808" };
    const fs::path dir = tempDir("sum");
    const auto res = exporting::exportLoop(*e, opt, dir);
    REQUIRE_MESSAGE(res.ok, res.error);

    const auto mix = wavSamples(dir / "mix.wav");
    const double spb = 48000.0 * 60.0 / 140.0;
    const auto frames = static_cast<std::size_t>(std::llround(16.0 * spb) - std::llround(8.0 * spb));
    REQUIRE(mix.size() == frames * 2);
    CHECK(test::peak(mix) > 0.05);

    std::vector<double> sum(mix.size(), 0.0);
    int partFiles = 0;
    for (const auto& entry : fs::directory_iterator(dir / "parts"))
    {
        const auto part = wavSamples(entry.path());
        REQUIRE(part.size() == mix.size());
        for (std::size_t i = 0; i < part.size(); ++i)
            sum[i] += part[i];
        ++partFiles;
    }
    CHECK(partFiles == 5);
    CHECK(fs::exists(dir / "parts" / "01-kick.wav"));
    CHECK(fs::exists(dir / "parts" / "05-808.wav"));
    double worst = 0.0;
    for (std::size_t i = 0; i < mix.size(); ++i)
        worst = std::max(worst, std::abs(sum[i] - mix[i]));
    CHECK(worst < 1.0e-5);

    // Seamless: the loop's first samples carry the tail of the previous pass, so a
    // non-seamless render differs at the start but matches after the tails have died.
    opt.seamlessLoop = false;
    const fs::path dir2 = tempDir("plain");
    REQUIRE(exporting::exportLoop(*e, opt, dir2).ok);
    const auto plain = wavSamples(dir2 / "mix.wav");
    REQUIRE(plain.size() >= 4000);
    double startDiff = 0.0;
    for (std::size_t i = 0; i < 2000; ++i)
        startDiff = std::max(startDiff, static_cast<double>(std::abs(plain[i] - mix[i])));
    CHECK(startDiff > 1.0e-4);

    fs::remove_all(dir);
    fs::remove_all(dir2);
}

TEST_CASE("Export: deterministic, restores mute flags and writes a loadable pattern")
{
    auto e = makeEngine(gen::Style::House, 3);
    e->part(3).mix.mute = true;
    exporting::ExportOptions opt;
    opt.bpm = 124.0;
    const fs::path a = tempDir("det-a"), b = tempDir("det-b");
    REQUIRE(exporting::exportLoop(*e, opt, a).ok);
    REQUIRE(exporting::exportLoop(*e, opt, b).ok);
    CHECK(readBytes(a / "mix.wav") == readBytes(b / "mix.wav"));
    CHECK(readBytes(a / "midi" / "all-parts.mid") == readBytes(b / "midi" / "all-parts.mid"));
    CHECK(e->part(3).mix.mute);
    CHECK_FALSE(e->part(0).mix.mute);
    CHECK_FALSE(fs::exists(a / "parts" / "04-part-4.wav")); // muted parts are not exported

    std::string text;
    {
        std::ifstream pf(a / "pattern.zdpattern"); // closed before cleanup: Windows cannot delete open files
        text.assign(std::istreambuf_iterator<char>(pf), std::istreambuf_iterator<char>());
    }
    Pattern q;
    REQUIRE(deserializePattern(text, q));
    CHECK(serializePattern(q) == serializePattern(e->sequencer().pattern()));
    fs::remove_all(a);
    fs::remove_all(b);
}

TEST_CASE("Export: MIDI files are valid and match the hits the sequencer plays")
{
    auto e = makeEngine(gen::Style::Drill, 11);
    exporting::ExportOptions opt;
    opt.bpm = 142.0;
    opt.partNames = { "Kick", "Snare", "Hat", "Perc", "808" };
    const fs::path dir = tempDir("midi");
    REQUIRE(exporting::exportLoop(*e, opt, dir).ok);

    const auto all = parseMidi(dir / "midi" / "all-parts.mid");
    CHECK(all.format == 1);
    CHECK(all.ppq == exporting::kMidiPpq);
    REQUIRE(all.tracks == 6); // tempo + 5 parts
    CHECK(all.noteOns[0] == 0);
    CHECK(all.names[1] == "Kick");
    CHECK(all.names[5] == "808");

    // Count the events the engine's own sequencer emits over the same (second) loop.
    const double loopBeats = 8.0;
    const double spb = 48000.0 * 60.0 / opt.bpm;
    const auto first = std::llround(loopBeats * spb);
    const auto frames = static_cast<int>(std::llround(2.0 * loopBeats * spb) - first);
    std::array<int, kMaxParts> expected {};
    std::vector<NoteEvent> ev(kMaxEventsPerBlock);
    for (int pos = 0; pos < frames; pos += 256)
    {
        const int n = std::min(256, frames - pos);
        const int count = e->sequencer().collect(Transport { true, opt.bpm, static_cast<double>(first + pos) / spb }, 48000.0, n, ev.data(),
                                                 static_cast<int>(ev.size()));
        for (int i = 0; i < count; ++i)
            ++expected[static_cast<std::size_t>(ev[static_cast<std::size_t>(i)].part)];
    }
    for (int t = 1; t < all.tracks; ++t)
    {
        CHECK(all.noteOns[static_cast<std::size_t>(t)] == expected[static_cast<std::size_t>(t - 1)]);
        CHECK(all.noteOns[static_cast<std::size_t>(t)] == all.noteOffs[static_cast<std::size_t>(t)]);
    }

    const auto kick = parseMidi(dir / "midi" / "01-kick.mid");
    CHECK(kick.tracks == 2);
    CHECK(kick.noteOns[1] == expected[0]);

    // 808 notes keep their pitch: realizeLane uses the step's note.
    const auto sub = exporting::realizeLane(e->sequencer(), 4, 32, 32, 35);
    REQUIRE_FALSE(sub.empty());
    for (const auto& n : sub)
    {
        CHECK(n.note >= 24);
        CHECK(n.tick >= 0);
        CHECK(n.tick < static_cast<int>(loopBeats * exporting::kMidiPpq));
    }
    fs::remove_all(dir);
}

TEST_CASE("Export: every drum part gets its own MIDI note in 36..51")
{
    std::set<int> seen;
    for (int part = 0; part < kMaxParts; ++part)
    {
        if (part == 4)
            continue; // the 808 is pitched, on its own MIDI channel
        const int note = exporting::defaultMidiNote(part);
        CHECK(note >= 36);
        CHECK(note <= 51);
        CHECK(seen.insert(note).second);
    }
}
