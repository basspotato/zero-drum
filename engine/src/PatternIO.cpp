#include "zd/PatternIO.h"

#include <algorithm>
#include <locale>
#include <cstdio>
#include <sstream>

namespace zd {

std::string serializePattern(const Pattern& p)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.precision(9);
    os << "zdpattern 1\n";
    os << "grid " << p.stepBeats << " swing " << p.swing << "\n";
    if (p.sweep.steps > 0)
        os << "sweep " << p.sweep.steps << " " << p.sweep.startHz << " " << p.sweep.endHz << " " << p.sweep.resonance << "\n";
    for (int lane = 0; lane < kMaxParts; ++lane)
    {
        const Lane& l = p.lanes[static_cast<std::size_t>(lane)];
        const bool any = std::any_of(l.steps.begin(), l.steps.begin() + std::clamp(l.length, 0, kMaxStepsPerLane),
                                     [](const Step& s) { return s.on; });
        if (!any && l.length == 16 && !l.straight)
            continue; // default, empty lane
        os << "lane " << lane << " " << l.length << "\n";
        if (l.straight)
            os << "straight\n";
        for (int i = 0; i < std::clamp(l.length, 0, kMaxStepsPerLane); ++i)
        {
            const Step& s = l.steps[static_cast<std::size_t>(i)];
            if (s.on)
                os << "step " << i << " " << s.velocity << " " << s.probability << " " << s.microOffset << " "
                   << s.note << " " << s.ratchet << "\n";
        }
    }
    return os.str();
}

bool deserializePattern(std::string_view text, Pattern& out)
{
    std::istringstream is { std::string(text) };
    is.imbue(std::locale::classic());

    std::string word;
    int version = 0;
    if (!(is >> word >> version) || word != "zdpattern" || version != 1)
        return false;

    Pattern p {};
    Lane* current = nullptr;
    while (is >> word)
    {
        if (word == "grid")
        {
            std::string swingWord;
            if (!(is >> p.stepBeats >> swingWord >> p.swing) || swingWord != "swing" || p.stepBeats <= 0.0)
                return false;
        }
        else if (word == "sweep")
        {
            Sweep& sw = p.sweep;
            if (!(is >> sw.steps >> sw.startHz >> sw.endHz >> sw.resonance) || sw.steps < 0 || sw.steps > 64 * kMaxStepsPerLane)
                return false;
            sw.startHz = std::clamp(sw.startHz, 20.0f, 20000.0f);
            sw.endHz = std::clamp(sw.endHz, sw.startHz, 20000.0f);
            sw.resonance = std::clamp(sw.resonance, 0.0f, 1.0f);
        }
        else if (word == "lane")
        {
            int index = 0, length = 0;
            if (!(is >> index >> length) || index < 0 || index >= kMaxParts || length < 1 || length > kMaxStepsPerLane)
                return false;
            current = &p.lanes[static_cast<std::size_t>(index)];
            current->length = length;
        }
        else if (word == "straight")
        {
            if (current == nullptr)
                return false;
            current->straight = true;
        }
        else if (word == "step")
        {
            int index = 0;
            Step s;
            s.on = true;
            if (current == nullptr || !(is >> index >> s.velocity >> s.probability >> s.microOffset >> s.note >> s.ratchet)
                || index < 0 || index >= current->length)
                return false;
            s.velocity = std::clamp(s.velocity, 0.0f, 1.0f);
            s.probability = std::clamp(s.probability, 0.0f, 1.0f);
            s.microOffset = std::clamp(s.microOffset, -0.5f, 0.5f);
            s.ratchet = std::clamp(s.ratchet, 1, kMaxRatchet);
            current->steps[static_cast<std::size_t>(index)] = s;
        }
        else
        {
            return false;
        }
    }
    out = p;
    return true;
}

} // namespace zd
