#include "zd/export/Export.h"

#include "zd/PatternIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace zd::exporting {

namespace fs = std::filesystem;

namespace {

void putU32(std::ofstream& f, std::uint32_t v)
{
    const char b[4] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                        static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff) };
    f.write(b, 4);
}

void putU16(std::ofstream& f, std::uint16_t v)
{
    const char b[2] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff) };
    f.write(b, 2);
}

void putFloat(std::ofstream& f, float v)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(v));
    putU32(f, bits);
}

// ------------------------------------------------------------------------------ MIDI bytes

void putVarLen(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    std::uint8_t buffer[5];
    int n = 0;
    buffer[n++] = static_cast<std::uint8_t>(value & 0x7f);
    while ((value >>= 7) != 0)
        buffer[n++] = static_cast<std::uint8_t>((value & 0x7f) | 0x80);
    while (n > 0)
        out.push_back(buffer[--n]);
}

void putBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
}

void writeChunk(std::ofstream& f, const char* id, const std::vector<std::uint8_t>& data)
{
    f.write(id, 4);
    const auto size = static_cast<std::uint32_t>(data.size());
    const char b[4] = { static_cast<char>(size >> 24), static_cast<char>(size >> 16), static_cast<char>(size >> 8), static_cast<char>(size) };
    f.write(b, 4);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

struct RawEvent
{
    int tick;
    int order; // note-offs before note-ons at the same tick
    std::uint8_t status, data1, data2;
};

std::string safeName(const std::string& name)
{
    std::string s;
    for (char c : name)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
    return s.empty() ? "part" : s;
}

std::string partFileStem(int index, const std::string& name)
{
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%02d-", index + 1);
    return prefix + safeName(name);
}

/// Sample index of a beat, rounded exactly as the sequencer rounds event times.
long long beatToSample(double beats, double samplesPerBeat) noexcept { return std::llround(beats * samplesPerBeat); }

/// Renders [startBeats, endBeats). The length is the difference of the rounded ends, so a
/// hit on endBeats never lands in the last sample and loop files never double a downbeat.
/// The engine's master latency is compensated: the pass runs that many samples longer and
/// the first ones (the delay line's contents) are dropped. A warm-up pass that the main
/// pass continues from must not run longer, or the overlap would trigger hits twice.
void renderPass(Engine& engine, double bpm, double startBeats, double endBeats, std::vector<float>& left,
                std::vector<float>& right, bool compensateLatency = true)
{
    constexpr int kBlock = 512;
    const double samplesPerBeat = engine.sampleRate() * 60.0 / bpm;
    const long long startSample = beatToSample(startBeats, samplesPerBeat);
    const long long total = beatToSample(endBeats, samplesPerBeat) - startSample;
    const long long latency = compensateLatency ? engine.masterLatency() : 0;
    std::vector<float> l(static_cast<std::size_t>(total + latency)), r(l.size());
    for (long long pos = 0; pos < total + latency; pos += kBlock)
    {
        const int n = static_cast<int>(std::min<long long>(kBlock, total + latency - pos));
        const Transport t { true, bpm, static_cast<double>(startSample + pos) / samplesPerBeat };
        engine.process(t, nullptr, 0, l.data() + pos, r.data() + pos, n);
    }
    left.assign(l.begin() + latency, l.end());
    right.assign(r.begin() + latency, r.end());
}

} // namespace

bool writeWav(const fs::path& path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    const auto frames = static_cast<std::uint32_t>(std::min(left.size(), right.size()));
    const std::uint32_t dataBytes = frames * 8u;
    f.write("RIFF", 4);
    putU32(f, 36u + dataBytes);
    f.write("WAVEfmt ", 8);
    putU32(f, 16);
    putU16(f, 3); // IEEE float
    putU16(f, 2);
    putU32(f, static_cast<std::uint32_t>(sampleRate));
    putU32(f, static_cast<std::uint32_t>(sampleRate) * 8u);
    putU16(f, 8);
    putU16(f, 32);
    f.write("data", 4);
    putU32(f, dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        putFloat(f, left[i]);
        putFloat(f, right[i]);
    }
    return static_cast<bool>(f);
}

int defaultMidiNote(int part) noexcept
{
    // GM kick, snare and closed hat; then the notes Zero Drum itself maps to parts
    // (36 + part), so a dragged clip plays back the same on the plugin. Part 4 is the
    // 808 (pitched, on MIDI channel 2): 33 is its default root, used for steps without a note.
    // Part 6's own note (42) is the GM hat, so it takes 37, which the snare left free.
    constexpr std::array<int, 7> notes { 36, 38, 42, 39, 33, 41, 37 };
    return part >= 0 && part < static_cast<int>(notes.size()) ? notes.at(static_cast<std::size_t>(part)) : 36 + part;
}

std::vector<MidiNote> realizeLane(const Sequencer& seq, int lane, long long firstStep, long long numSteps, int defaultNote)
{
    std::vector<MidiNote> notes;
    const Pattern& p = seq.pattern();
    const Lane& l = p.lanes[static_cast<std::size_t>(lane)];
    if (l.length <= 0)
        return notes;

    const double firstBeat = static_cast<double>(firstStep) * p.stepBeats;
    for (long long k = firstStep; k < firstStep + numSteps; ++k)
    {
        const Step& s = l.steps[static_cast<std::size_t>(k % l.length)];
        if (!s.on || !seq.passesProbability(lane, k, s.probability))
            continue;
        const int repeats = std::clamp(s.ratchet, 1, kMaxRatchet);
        const double spacing = p.stepBeats / repeats;
        for (int r = 0; r < repeats; ++r)
        {
            const double beat = seq.stepTime(lane, k) + r * spacing - firstBeat;
            MidiNote n;
            n.tick = std::max(0, static_cast<int>(std::lround(beat * kMidiPpq)));
            n.lengthTicks = std::max(1, static_cast<int>(std::lround(spacing * kMidiPpq * 0.9)));
            n.note = s.note >= 0 ? s.note : defaultNote;
            const float velocity = r == 0 ? s.velocity : s.velocity * 0.8f;
            n.velocity = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
            notes.push_back(n);
        }
    }
    return notes;
}

bool writeMidi(const fs::path& path, const std::vector<MidiTrack>& tracks, double bpm)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    std::vector<std::uint8_t> header;
    putBigEndian(header, 1, 2);                                    // format 1
    putBigEndian(header, static_cast<std::uint32_t>(tracks.size() + 1), 2);
    putBigEndian(header, kMidiPpq, 2);
    writeChunk(f, "MThd", header);

    // Tempo track.
    std::vector<std::uint8_t> tempo;
    const auto usPerBeat = static_cast<std::uint32_t>(std::lround(60000000.0 / bpm));
    putVarLen(tempo, 0);
    tempo.insert(tempo.end(), { 0xFF, 0x51, 0x03 });
    putBigEndian(tempo, usPerBeat, 3);
    putVarLen(tempo, 0);
    tempo.insert(tempo.end(), { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }); // 4/4
    putVarLen(tempo, 0);
    tempo.insert(tempo.end(), { 0xFF, 0x2F, 0x00 });
    writeChunk(f, "MTrk", tempo);

    for (const auto& track : tracks)
    {
        std::vector<RawEvent> events;
        const auto ch = static_cast<std::uint8_t>(std::clamp(track.channel, 0, 15));
        for (const auto& n : track.notes)
        {
            const auto note = static_cast<std::uint8_t>(std::clamp(n.note, 0, 127));
            events.push_back({ n.tick, 1, static_cast<std::uint8_t>(0x90 | ch), note, static_cast<std::uint8_t>(n.velocity) });
            events.push_back({ n.tick + n.lengthTicks, 0, static_cast<std::uint8_t>(0x80 | ch), note, 0 });
        }
        std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });

        std::vector<std::uint8_t> data;
        putVarLen(data, 0);
        data.insert(data.end(), { 0xFF, 0x03 });
        putVarLen(data, static_cast<std::uint32_t>(track.name.size()));
        data.insert(data.end(), track.name.begin(), track.name.end());
        int last = 0;
        for (const auto& e : events)
        {
            putVarLen(data, static_cast<std::uint32_t>(e.tick - last));
            last = e.tick;
            data.insert(data.end(), { e.status, e.data1, e.data2 });
        }
        putVarLen(data, 0);
        data.insert(data.end(), { 0xFF, 0x2F, 0x00 });
        writeChunk(f, "MTrk", data);
    }
    return static_cast<bool>(f);
}

int loopBars(const Pattern& pattern) noexcept
{
    double beats = 4.0;
    for (const Lane& l : pattern.lanes)
        if (l.length > 0)
            for (const Step& st : l.steps)
                if (st.on)
                {
                    beats = std::max(beats, l.length * pattern.stepBeats);
                    break;
                }
    return std::clamp(static_cast<int>(std::ceil(beats / 4.0 - 1.0e-9)), 1, 16);
}

bool melodicKick(const Pattern& pattern, int lane) noexcept
{
    if (lane != 0)
        return false;
    const Lane& l = pattern.lanes[0];
    for (int i = 0; i < l.length; ++i)
        if (l.steps[static_cast<std::size_t>(i)].on && l.steps[static_cast<std::size_t>(i)].note >= 0)
            return true;
    return false;
}

std::vector<MidiTrack> patternTracks(const Sequencer& sequencer, int bars, const std::array<std::string, kMaxParts>& names,
                                     std::uint32_t pitchedMask, int lane)
{
    std::vector<MidiTrack> tracks;
    const auto steps = static_cast<long long>(std::llround(bars * 4.0 / sequencer.pattern().stepBeats));
    for (int i = 0; i < kMaxParts; ++i)
    {
        if (lane >= 0 && i != lane)
            continue;
        const bool pitched = ((pitchedMask >> i) & 1u) != 0;
        MidiTrack t;
        t.name = names[static_cast<std::size_t>(i)].empty() ? "Part " + std::to_string(i + 1) : names[static_cast<std::size_t>(i)];
        t.channel = pitched ? 1 : melodicKick(sequencer.pattern(), i) ? kMelodicKickChannel : 9;
        t.notes = realizeLane(sequencer, i, 0, steps, defaultMidiNote(i));
        if (!t.notes.empty())
            tracks.push_back(std::move(t));
    }
    return tracks;
}

ExportResult exportLoop(Engine& engine, const ExportOptions& options, const fs::path& directory)
{
    ExportResult result;
    std::error_code ec;
    fs::create_directories(directory / "parts", ec);
    fs::create_directories(directory / "midi", ec);
    if (ec)
    {
        result.error = "could not create " + directory.string() + ": " + ec.message();
        return result;
    }

    const double sr = engine.sampleRate();
    const double loopBeats = options.bars * 4.0;
    const double startBeats = options.seamlessLoop ? loopBeats : 0.0;
    const Pattern& pattern = engine.sequencer().pattern();
    const auto stepsPerLoop = static_cast<long long>(std::llround(loopBeats / pattern.stepBeats));
    const long long firstStep = options.seamlessLoop ? stepsPerLoop : 0;

    auto nameOf = [&](int i) {
        return options.partNames[static_cast<std::size_t>(i)].empty() ? "part-" + std::to_string(i + 1)
                                                                      : options.partNames[static_cast<std::size_t>(i)];
    };

    // Which parts play anything in this loop?
    std::vector<int> playing;
    for (int i = 0; i < kMaxParts; ++i)
        if (engine.part(i).type != EngineType::Off && !engine.part(i).mix.mute && !realizeLane(engine.sequencer(), i, firstStep, stepsPerLoop, 36).empty())
            playing.push_back(i);

    // Restores the user's mute flags and clears render state on every exit path.
    struct MuteGuard
    {
        Engine& engine;
        std::array<bool, kMaxParts> saved {};
        Engine::MasterClipSettings clipSettings;
        explicit MuteGuard(Engine& e) : engine(e), clipSettings(e.masterClip())
        {
            for (int i = 0; i < kMaxParts; ++i)
                saved[static_cast<std::size_t>(i)] = engine.part(i).mix.mute;
        }
        ~MuteGuard()
        {
            engine.setMasterClip(clipSettings.clipOn, clipSettings.driveDb, clipSettings.ceilingDb);
            for (int i = 0; i < kMaxParts; ++i)
                engine.part(i).mix.mute = saved[static_cast<std::size_t>(i)];
            engine.reset();
        }
        MuteGuard(const MuteGuard&) = delete;
        MuteGuard& operator=(const MuteGuard&) = delete;
    } guard(engine);
    const auto& savedMute = guard.saved;

    // Part files are taken before the master clipper (its delay stays, so timing matches);
    // the mix goes through it. So the parts sum exactly to an unclipped mix, the way stems
    // from a mix bus do.
    const Engine::MasterClipSettings clip = engine.masterClip();
    auto render = [&](int solo, std::vector<float>& l, std::vector<float>& r) {
        engine.setMasterClip(solo < 0 && clip.clipOn, clip.driveDb, clip.ceilingDb);
        for (int i = 0; i < kMaxParts; ++i)
            engine.part(i).mix.mute = solo >= 0 ? (i != solo) : savedMute[static_cast<std::size_t>(i)];
        engine.reset();
        if (options.seamlessLoop)
        {
            std::vector<float> warmL, warmR;
            renderPass(engine, options.bpm, 0.0, startBeats, warmL, warmR, false); // warm-up: no drop, no overlap
        }
        renderPass(engine, options.bpm, startBeats, startBeats + loopBeats, l, r);
    };

    std::vector<float> l, r;
    render(-1, l, r);
    if (!writeWav(directory / "mix.wav", l, r, static_cast<int>(sr)))
    {
        result.error = "could not write mix.wav";
        return result;
    }
    result.files.push_back(directory / "mix.wav");

    std::vector<MidiTrack> allTracks;
    for (int i : playing)
    {
        const std::string stem = partFileStem(i, nameOf(i));
        render(i, l, r);
        const fs::path wav = directory / "parts" / (stem + ".wav");
        if (!writeWav(wav, l, r, static_cast<int>(sr)))
        {
            result.error = "could not write " + wav.string();
            return result;
        }
        result.files.push_back(wav);

        const bool pitched = engine.part(i).type == EngineType::Sub808;
        MidiTrack track { nameOf(i), pitched ? 1 : melodicKick(engine.sequencer().pattern(), i) ? kMelodicKickChannel : 9, realizeLane(engine.sequencer(), i, firstStep, stepsPerLoop, defaultMidiNote(i)) };
        const fs::path mid = directory / "midi" / (stem + ".mid");
        if (!writeMidi(mid, { track }, options.bpm))
        {
            result.error = "could not write " + mid.string();
            return result;
        }
        result.files.push_back(mid);
        allTracks.push_back(std::move(track));
    }

    const fs::path all = directory / "midi" / "all-parts.mid";
    if (!writeMidi(all, allTracks, options.bpm))
    {
        result.error = "could not write all-parts.mid";
        return result;
    }
    result.files.push_back(all);

    const fs::path patternFile = directory / "pattern.zdpattern";
    std::ofstream pf(patternFile, std::ios::binary);
    pf << serializePattern(pattern);
    if (!pf)
    {
        result.error = "could not write pattern.zdpattern";
        return result;
    }
    result.files.push_back(patternFile);

    result.ok = true;
    return result;
}

} // namespace zd::exporting
