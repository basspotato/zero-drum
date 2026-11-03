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
    layout.add(floatParam("kickLevel", "Kick Level", Range { -48.0f, 6.0f }, 0.0f, "dB"));

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

    // Groove
    layout.add(floatParam("swing", "Swing", Range { 50.0f, 75.0f }, 54.0f, "%"));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "freeRun", 1 }, "Free Run", false));
    layout.add(floatParam("freeRunBpm", "Free Run Tempo", Range { 60.0f, 200.0f, 0.1f }, 120.0f, "BPM"));

    return layout;
}

float ZeroDrumProcessor::value(const char* id) const noexcept
{
    return apvts_.getRawParameterValue(id)->load(std::memory_order_relaxed);
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

    engine_.sequencer().pattern().swing = value("swing") * 0.01;
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
        const int part = noteToPart(msg.getNoteNumber());
        if (part >= 0)
            midiEvents_[static_cast<std::size_t>(numEvents++)] = { meta.samplePosition, part, msg.getFloatVelocity() };
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
