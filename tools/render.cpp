// zd_render: renders the default kit and pattern to a WAV file, offline and without JUCE.
// Usage: zd_render [out.wav] [bars=4] [bpm=120] [sampleRate=48000] [seed=1] [style]
// With a style (0..16, see zd::gen::Style) the beat is generated on that style's tuned kit;
// bpm 0 picks the style's tempo.
// If out ends in '/', it is a folder and the loop is exported there instead: mix.wav,
// a WAV per part, MIDI per part and for all parts, and the pattern (see zd/export/Export.h).

#include "zd/Engine.h"
#include "zd/export/Export.h"
#include "zd/gen/Generator.h"
#include "zd/gen/StyleKit.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

void writeU32(std::ofstream& f, std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void writeU16(std::ofstream& f, std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); }

/// Writes 32-bit float stereo WAV (little-endian hosts).
bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sampleRate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    const auto frames = static_cast<std::uint32_t>(l.size());
    const std::uint32_t dataBytes = frames * 2u * 4u;
    f.write("RIFF", 4);
    writeU32(f, 36u + dataBytes);
    f.write("WAVEfmt ", 8);
    writeU32(f, 16);
    writeU16(f, 3); // IEEE float
    writeU16(f, 2);
    writeU32(f, static_cast<std::uint32_t>(sampleRate));
    writeU32(f, static_cast<std::uint32_t>(sampleRate) * 8u);
    writeU16(f, 8);
    writeU16(f, 32);
    f.write("data", 4);
    writeU32(f, dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        f.write(reinterpret_cast<const char*>(&l[i]), 4);
        f.write(reinterpret_cast<const char*>(&r[i]), 4);
    }
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "zero-drum-demo.wav";
    const int bars = argc > 2 ? std::atoi(argv[2]) : 4;
    double bpm = argc > 3 ? std::atof(argv[3]) : 120.0;
    const int sampleRate = argc > 4 ? std::atoi(argv[4]) : 48000;
    const auto seed = static_cast<std::uint64_t>(argc > 5 ? std::atoll(argv[5]) : 1);

    constexpr int block = 512;
    auto engine = std::make_unique<zd::Engine>();
    engine->prepare(sampleRate, block);
    zd::loadDefaultKit(*engine);
    engine->setMasterGainDb(zd::Engine::kDefaultMasterDb);
    engine->setMasterStage(true);
    engine->setMasterClip(true, 3.0f, -1.0f); // the plugin's default master clipper
    engine->sequencer().setSeed(seed);

    if (argc > 6)
    {
        zd::gen::GenerateParams p;
        p.style = static_cast<zd::gen::Style>(std::atoi(argv[6]));
        p.seed = seed;
        p.complexity = 0.6f;
        const auto kit = zd::gen::styleKit(p.style);
        p.rootNote = kit.rootNote;
        zd::gen::applyKit(kit, *engine, kit.rootNote);
        zd::gen::generate(p, engine->sequencer().pattern());
        if (bpm <= 0.0)
        {
            const auto range = zd::gen::styleTempo(p.style);
            bpm = 0.5 * (range.low + range.high);
        }
        std::printf("style %s\n", std::string(zd::gen::styleName(p.style)).c_str());
    }

    if (!out.empty() && out.back() == '/')
    {
        zd::exporting::ExportOptions options;
        options.bars = bars;
        options.bpm = bpm;
        options.partNames = { "Kick", "Snare", "Hat", "Perc", "808" };
        const auto result = zd::exporting::exportLoop(*engine, options, out);
        if (!result.ok)
        {
            std::fprintf(stderr, "export failed: %s\n", result.error.c_str());
            return 1;
        }
        for (const auto& f : result.files)
            std::printf("wrote %s\n", f.string().c_str());
        return 0;
    }

    // Render the master latency extra and drop it, so the file starts on the downbeat.
    const auto latency = static_cast<std::size_t>(engine->masterLatency());
    const auto total = static_cast<std::size_t>(bars * 4 * 60.0 / bpm * sampleRate) + latency;
    std::vector<float> left(total), right(total);
    zd::InternalClock clock(bpm);
    for (std::size_t pos = 0; pos < total; pos += block)
    {
        const int n = static_cast<int>(std::min<std::size_t>(block, total - pos));
        engine->process(clock.next(sampleRate, n), nullptr, 0, left.data() + pos, right.data() + pos, n);
    }
    left.erase(left.begin(), left.begin() + static_cast<long>(latency));
    right.erase(right.begin(), right.begin() + static_cast<long>(latency));

    if (!writeWav(out, left, right, sampleRate))
    {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s (%d bars at %.1f BPM, %d Hz)\n", out.c_str(), bars, bpm, sampleRate);
    return 0;
}
