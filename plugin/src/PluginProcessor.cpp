#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {

using Range = juce::NormalisableRange<float>;

std::unique_ptr<juce::AudioParameterFloat> floatParam(const char* id, const char* name, Range range,
                                                      float defaultValue, const char* label = "")
{
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, defaultValue, juce::AudioParameterFloatAttributes().withLabel(label));
}

Range skewed(float lo, float hi, float centre)
{
    Range r { lo, hi };
    r.setSkewForCentre(centre);
    return r;
}

} // namespace

ZeroDrumProcessor::ZeroDrumProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "ZeroDrum", createLayout())
{
    // Cache every parameter pointer once, so the audio thread never builds strings.
    const auto& params = getParameters();
    paramIds_.reserve(static_cast<std::size_t>(params.size()));
    for (auto* param : params)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            paramIds_.push_back(withId->paramID.toStdString());
    for (const auto& id : paramIds_)
        paramValues_.emplace(std::string_view(id), apvts_.getRawParameterValue(juce::String(id)));

    for (int i = 0; i < kNumStripParts; ++i)
        for (int k = 0; k < kNumStripSuffixes; ++k)
            stripParams_[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] =
                apvts_.getRawParameterValue("p" + juce::String(i) + kStripSuffixes[k]);
}

juce::AudioProcessorValueTreeState::ParameterLayout ZeroDrumProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Kick
    layout.add(floatParam("kickTune", "Kick Tune", skewed(30.0f, 120.0f, 55.0f), 50.0f, "Hz"));
    layout.add(floatParam("kickSweep", "Kick Sweep", Range { 0.0f, 8.0f }, 4.0f, "x"));
    layout.add(floatParam("kickSweepTime", "Kick Sweep Time", skewed(5.0f, 200.0f, 40.0f), 35.0f, "ms"));
    layout.add(floatParam("kickDecay", "Kick Decay", skewed(50.0f, 3000.0f, 500.0f), 600.0f, "ms"));
    layout.add(floatParam("kickClick", "Kick Click", Range { 0.0f, 1.0f }, 0.4f));
    layout.add(floatParam("kickDrive", "Kick Drive", Range { 0.0f, 1.0f }, 0.25f));
    layout.add(floatParam("kickLevel", "Kick Level", Range { -48.0f, 6.0f }, -2.0f, "dB"));

    // Snare
    layout.add(floatParam("snareTune", "Snare Tune", skewed(100.0f, 400.0f, 190.0f), 190.0f, "Hz"));
    layout.add(floatParam("snareToneDecay", "Snare Body Decay", skewed(20.0f, 800.0f, 150.0f), 120.0f, "ms"));
    layout.add(floatParam("snareNoiseDecay", "Snare Wire Decay", skewed(20.0f, 1200.0f, 200.0f), 220.0f, "ms"));
    layout.add(floatParam("snareNoiseTone", "Snare Wire Tone", skewed(800.0f, 12000.0f, 3500.0f), 3500.0f, "Hz"));
    layout.add(floatParam("snareSnappy", "Snare Snappy", Range { 0.0f, 1.0f }, 0.6f));
    layout.add(floatParam("snareDrive", "Snare Drive", Range { 0.0f, 1.0f }, 0.15f));
    layout.add(floatParam("snareLevel", "Snare Level", Range { -48.0f, 6.0f }, -3.0f, "dB"));

    // Hat (sample part)
    layout.add(floatParam("hatPitch", "Hat Pitch", Range { -24.0f, 24.0f }, 0.0f, "st"));
    layout.add(floatParam("hatDecay", "Hat Decay", skewed(0.0f, 1000.0f, 150.0f), 0.0f, "ms"));
    layout.add(floatParam("hatLevel", "Hat Level", Range { -48.0f, 6.0f }, -8.0f, "dB"));

    // FM percussion
    layout.add(floatParam("fmTune", "Perc Tune", skewed(80.0f, 4000.0f, 500.0f), 420.0f, "Hz"));
    layout.add(floatParam("fmRatio", "Perc Ratio", skewed(0.25f, 8.0f, 1.5f), 1.41f));
    layout.add(floatParam("fmIndex", "Perc FM Amount", Range { 0.0f, 20.0f }, 3.0f));
    layout.add(floatParam("fmIndexDecay", "Perc FM Decay", skewed(2.0f, 1000.0f, 60.0f), 50.0f, "ms"));
    layout.add(floatParam("fmDecay", "Perc Decay", skewed(20.0f, 3000.0f, 300.0f), 250.0f, "ms"));
    layout.add(floatParam("fmFeedback", "Perc Feedback", Range { 0.0f, 1.0f }, 0.2f));
    layout.add(floatParam("fmLevel", "Perc Level", Range { -48.0f, 6.0f }, -12.0f, "dB"));

    // 808
    layout.add(floatParam("subTune", "808 Tune", skewed(25.0f, 120.0f, 55.0f), 55.0f, "Hz"));
    layout.add(floatParam("subDecay", "808 Decay", skewed(100.0f, 6000.0f, 1200.0f), 1400.0f, "ms"));
    layout.add(floatParam("subGlide", "808 Glide", skewed(0.0f, 500.0f, 60.0f), 60.0f, "ms"));
    layout.add(floatParam("subPunch", "808 Punch", Range { 0.0f, 4.0f }, 1.0f, "x"));
    layout.add(floatParam("subDrive", "808 Drive", Range { 0.0f, 1.0f }, 0.35f));
    layout.add(floatParam("subLevel", "808 Level", Range { -48.0f, 6.0f }, -10.0f, "dB"));

    // Channel strips for the five default parts
    for (int i = 0; i < kNumStripParts; ++i)
    {
        const juce::String n(kStripNames[i]);
        const juce::String id = "p" + juce::String(i);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "Attack", 1 }, n + " Attack", Range { -1.0f, 1.0f }, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "Sustain", 1 }, n + " Sustain", Range { -1.0f, 1.0f }, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id + "FilterMode", 1 }, n + " Filter",
                                                                juce::StringArray { "Off", "Low-pass", "Band-pass", "High-pass" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "Cutoff", 1 }, n + " Cutoff", skewed(20.0f, 20000.0f, 1000.0f), 8000.0f,
                                                               juce::AudioParameterFloatAttributes().withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "Resonance", 1 }, n + " Resonance", skewed(0.5f, 12.0f, 1.0f), 0.707f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "StripDrive", 1 }, n + " Strip Drive", Range { 0.0f, 1.0f }, 0.0f));
    }

    // Groove
    layout.add(floatParam("swing", "Swing", Range { 50.0f, 75.0f }, 54.0f, "%"));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "freeRun", 1 }, "Free Run", false));
    layout.add(floatParam("freeRunBpm", "Free Run Tempo", Range { 60.0f, 200.0f, 0.1f }, 120.0f, "BPM"));

    return layout;
}

float ZeroDrumProcessor::value(const char* id) const noexcept
{
    const auto it = paramValues_.find(std::string_view(id));
    jassert(it != paramValues_.end());
    return it != paramValues_.end() ? it->second->load(std::memory_order_relaxed) : 0.0f;
}

void ZeroDrumProcessor::pullParameters() noexcept
{
    auto& kick = engine_.part(0);
    kick.kick.tuneHz = value("kickTune");
    kick.kick.pitchAmount = value("kickSweep");
    kick.kick.pitchDecay = value("kickSweepTime") * 0.001f;
    kick.kick.decay = value("kickDecay") * 0.001f;
    kick.kick.click = value("kickClick");
    kick.kick.drive = value("kickDrive");
    kick.mix.gainDb = value("kickLevel");

    auto& snare = engine_.part(1);
    snare.snare.tuneHz = value("snareTune");
    snare.snare.toneDecay = value("snareToneDecay") * 0.001f;
    snare.snare.noiseDecay = value("snareNoiseDecay") * 0.001f;
    snare.snare.noiseTone = value("snareNoiseTone");
    snare.snare.snappy = value("snareSnappy");
    snare.snare.drive = value("snareDrive");
    snare.mix.gainDb = value("snareLevel");

    auto& hat = engine_.part(2);
    hat.sampleParams.pitchSemitones = value("hatPitch");
    hat.sampleParams.decay = value("hatDecay") * 0.001f;
    hat.mix.gainDb = value("hatLevel");

    auto& perc = engine_.part(3);
    perc.fm.tuneHz = value("fmTune");
    perc.fm.ratio = value("fmRatio");
    perc.fm.index = value("fmIndex");
    perc.fm.indexDecay = value("fmIndexDecay") * 0.001f;
    perc.fm.decay = value("fmDecay") * 0.001f;
    perc.fm.feedback = value("fmFeedback");
    perc.mix.gainDb = value("fmLevel");

    auto& sub = engine_.part(4);
    sub.sub.tuneHz = value("subTune");
    sub.sub.decay = value("subDecay") * 0.001f;
    sub.sub.glide = value("subGlide") * 0.001f;
    sub.sub.punch = value("subPunch");
    sub.sub.drive = value("subDrive");
    sub.mix.gainDb = value("subLevel");

    for (int i = 0; i < kNumStripParts; ++i)
    {
        auto& st = engine_.part(i).strip;
        st.attack = stripValue(i, "Attack");
        st.sustain = stripValue(i, "Sustain");
        st.filterMode = static_cast<zd::FilterMode>(static_cast<int>(stripValue(i, "FilterMode")));
        st.cutoffHz = stripValue(i, "Cutoff");
        st.resonance = stripValue(i, "Resonance");
        st.drive = stripValue(i, "StripDrive");
    }

    engine_.sequencer().pattern().swing = value("swing") * 0.01;
}

float ZeroDrumProcessor::stripValue(int part, const char* suffix) const noexcept
{
    return stripParams_[static_cast<std::size_t>(part)][static_cast<std::size_t>(stripSuffixIndex(suffix))]->load(std::memory_order_relaxed);
}

void ZeroDrumProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, std::max(samplesPerBlock, 32));
    zd::loadDefaultKit(engine_);
    freeRunClock_ = zd::InternalClock { value("freeRunBpm") };
}

bool ZeroDrumProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

int ZeroDrumProcessor::noteToPart(int note) noexcept
{
    switch (note)
    {
        case 36: return 0; // kick
        case 38: return 1; // snare
        case 42: return 2; // closed hat
        default: break;
    }
    if (note >= 36 && note < 36 + zd::kMaxParts)
        return note - 36;
    return -1;
}

void ZeroDrumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    pullParameters();

    int numEvents = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (!msg.isNoteOn() || numEvents >= static_cast<int>(midiEvents_.size()))
            continue;
        if (msg.getChannel() == kSubMidiChannel)
        {
            // Channel 2 plays the 808 melodically.
            midiEvents_[static_cast<std::size_t>(numEvents++)] = { meta.samplePosition, 4, msg.getFloatVelocity(), msg.getNoteNumber() };
            continue;
        }
        const int part = noteToPart(msg.getNoteNumber());
        if (part >= 0)
            midiEvents_[static_cast<std::size_t>(numEvents++)] = { meta.samplePosition, part, msg.getFloatVelocity(), -1 };
    }

    zd::Transport transport;
    if (value("freeRun") > 0.5f)
    {
        transport = freeRunClock_.next(getSampleRate(), numSamples);
    }
    else if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            transport.playing = pos->getIsPlaying();
            transport.bpm = pos->getBpm().orFallback(120.0);
            transport.ppqPosition = pos->getPpqPosition().orFallback(0.0);
        }
    }

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    engine_.process(transport, midiEvents_.data(), numEvents, left, right, numSamples);
}

juce::AudioProcessorEditor* ZeroDrumProcessor::createEditor()
{
    return new ZeroDrumEditor(*this);
}

void ZeroDrumProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ZeroDrumProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts_.state.getType()))
            apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZeroDrumProcessor();
}
