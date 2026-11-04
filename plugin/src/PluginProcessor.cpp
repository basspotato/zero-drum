#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "zd/PatternIO.h"

#include <string>
#include <thread>

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
    setLatencySamples(zd::dsp::MasterClipper::kLatency); // the master stage is always in
    zd::loadDefaultPattern(uiPattern_);
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
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
        for (int k = 0; k < kNumPadSuffixes; ++k)
            padParams_[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] =
                apvts_.getRawParameterValue("p" + juce::String(i) + kPadSuffixes[k]);
    formats_.registerBasicFormats();
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
    layout.add(floatParam("kickDrive2", "Kick Drive 2", Range { 0.0f, 1.0f }, 0.0f));
    layout.add(floatParam("kickMidFreq", "Kick Mid Freq", skewed(300.0f, 2000.0f, 800.0f), 700.0f, "Hz"));
    layout.add(floatParam("kickMidGain", "Kick Mid Gain", Range { 0.0f, 12.0f }, 6.0f, "dB"));
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
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "snareEngine", 1 }, "Snare Engine",
                                                            juce::StringArray { "Snare", "Clap" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "snareLayer", 1 }, "Snare Layer",
                                                            juce::StringArray { "None", "Clap", "Snare" }, 0));
    layout.add(floatParam("snareLayerLevel", "Snare Layer Level", Range { -24.0f, 6.0f }, -4.0f, "dB"));
    layout.add(floatParam("clapTone", "Clap Tone", skewed(500.0f, 4000.0f, 1300.0f), 1200.0f, "Hz"));
    layout.add(floatParam("clapSpread", "Clap Spread", Range { 3.0f, 25.0f }, 9.0f, "ms"));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "clapHands", 1 }, "Clap Hands", 1, 6, 4));
    layout.add(floatParam("clapDecay", "Clap Tail Decay", skewed(50.0f, 1500.0f, 300.0f), 300.0f, "ms"));
    layout.add(floatParam("clapTail", "Clap Tail", Range { 0.0f, 1.0f }, 0.6f));
    layout.add(floatParam("clapSnap", "Clap Snap", Range { 0.0f, 1.0f }, 0.4f));
    layout.add(floatParam("clapDrive", "Clap Drive", Range { 0.0f, 1.0f }, 0.2f));

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
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id + "Crush", 1 }, n + " Crush", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "CrushBits", 1 }, n + " Crush Bits", Range { 4.0f, 16.0f }, 12.0f,
                                                               juce::AudioParameterFloatAttributes().withLabel("bit")));
        // 48 kHz means no rate reduction; 26.04 kHz is the classic 12-bit sampler.
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "CrushRate", 1 }, n + " Crush Rate", skewed(4000.0f, 48000.0f, 20000.0f), 26040.0f,
                                                               juce::AudioParameterFloatAttributes().withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id + "CrushFilter", 1 }, n + " Crush Filter", true));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "VelTone", 1 }, n + " Velocity to Tone", Range { 0.0f, 1.0f },
                                                               i == 4 ? 0.0f : 0.4f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + "RevSend", 1 }, n + " Reverb Send", Range { 0.0f, 1.0f }, 0.0f));
    }

    // Beat generator
    {
        juce::StringArray styles;
        for (int i = 0; i < static_cast<int>(zd::gen::Style::kCount); ++i)
            styles.add(juce::String(std::string(zd::gen::styleName(static_cast<zd::gen::Style>(i)))));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "genStyle", 1 }, "Style", styles, 0));
    }
    layout.add(floatParam("genDensity", "Density", Range { 0.0f, 1.0f }, 0.5f));
    layout.add(floatParam("genComplexity", "Complexity", Range { 0.0f, 1.0f }, 0.5f));
    layout.add(floatParam("genGroove", "Groove", Range { 0.0f, 1.0f }, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "subCutsKick", 1 }, "808 Cuts Kick", false));
    layout.add(floatParam("revDecay", "Reverb Decay", skewed(0.2f, 6.0f, 1.2f), 1.0f, "s"));
    layout.add(floatParam("revSize", "Reverb Size", Range { 0.0f, 1.0f }, 0.5f));
    layout.add(floatParam("revPreDelay", "Reverb Pre-delay", Range { 0.0f, 100.0f }, 10.0f, "ms"));
    layout.add(floatParam("revDamping", "Reverb Damping", skewed(500.0f, 20000.0f, 5000.0f), 7000.0f, "Hz"));
    layout.add(floatParam("revHighPass", "Reverb High-pass", skewed(20.0f, 2000.0f, 300.0f), 400.0f, "Hz"));
    layout.add(floatParam("revLevel", "Reverb Level", Range { -24.0f, 6.0f }, 0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "glueOn", 1 }, "Glue", true));
    layout.add(floatParam("glueThreshold", "Glue Threshold", Range { -40.0f, 0.0f }, -14.0f, "dB"));
    layout.add(floatParam("glueRatio", "Glue Ratio", skewed(1.0f, 10.0f, 3.0f), 2.0f, ":1"));
    layout.add(floatParam("glueAttack", "Glue Attack", skewed(0.1f, 100.0f, 10.0f), 20.0f, "ms"));
    layout.add(floatParam("glueRelease", "Glue Release", skewed(10.0f, 1200.0f, 150.0f), 120.0f, "ms"));
    layout.add(floatParam("glueMakeup", "Glue Makeup", Range { 0.0f, 12.0f }, 1.0f, "dB"));
    layout.add(floatParam("glueMix", "Glue Mix", Range { 0.0f, 1.0f }, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "kickCutsSub", 1 }, "Kick Cuts 808", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "genRoot", 1 }, "808 Root Note", 24, 47, 33));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "genKit", 1 }, "Load Style Kit", true));
    for (int i = 0; i < kNumStripParts; ++i)
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "lock" + juce::String(i), 1 },
                                                              "Lock " + juce::String(kStripNames[i]), false));

    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
    {
        const juce::String id = "p" + juce::String(i);
        const juce::String name = "Pad " + juce::String(i + 1) + " ";
        auto add = [&](const char* suffix, const char* label, Range range, float def, const char* unit) {
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id + suffix, 1 }, name + label, range, def,
                                                                   juce::AudioParameterFloatAttributes().withLabel(unit)));
        };
        add("SmpPitch", "Pitch", Range { -24.0f, 24.0f, 0.01f }, 0.0f, "st");
        add("SmpDecay", "Decay", skewed(0.0f, 4.0f, 0.6f), 0.0f, "s");
        add("SmpStart", "Start", Range { 0.0f, 0.9f }, 0.0f, "");
        add("Level", "Level", Range { -36.0f, 6.0f }, -6.0f, "dB");
        add("Pan", "Pan", Range { -1.0f, 1.0f }, 0.0f, "");
        add("RevSend", "Reverb", Range { 0.0f, 1.0f }, 0.0f, "");
    }

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "midiOut", 1 }, "MIDI Out", true));
    layout.add(floatParam("midiNoteLength", "MIDI Out Note Length", skewed(10.0f, 2000.0f, 150.0f), 100.0f, "ms"));

    layout.add(floatParam("masterLevel", "Master Level", Range { -24.0f, 6.0f }, zd::Engine::kDefaultMasterDb, "dB"));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "clipOn", 1 }, "Master Clipper", true));
    layout.add(floatParam("subDuck", "808 Sidechain", Range { 0.0f, 1.0f }, 0.5f));
    layout.add(floatParam("percDuck", "Perc Sidechain", Range { 0.0f, 1.0f }, 0.0f));
    layout.add(floatParam("duckRelease", "Sidechain Release", skewed(20.0f, 600.0f, 120.0f), 80.0f, "ms"));
    layout.add(floatParam("clipDrive", "Clipper Drive", Range { 0.0f, 24.0f }, 3.0f, "dB"));
    layout.add(floatParam("clipCeiling", "Clipper Ceiling", Range { -12.0f, 0.0f }, -1.0f, "dB"));

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

void ZeroDrumProcessor::applyParameters(zd::Engine& engine) const noexcept
{
    auto& kick = engine.part(0);
    kick.kick.tuneHz = value("kickTune");
    kick.kick.pitchAmount = value("kickSweep");
    kick.kick.pitchDecay = value("kickSweepTime") * 0.001f;
    kick.kick.decay = value("kickDecay") * 0.001f;
    kick.kick.click = value("kickClick");
    kick.kick.drive = value("kickDrive");
    kick.kick.drive2 = value("kickDrive2");
    kick.kick.midHz = value("kickMidFreq");
    kick.kick.midDb = value("kickMidGain");
    kick.mix.gainDb = value("kickLevel");

    auto& snare = engine.part(1);
    snare.snare.tuneHz = value("snareTune");
    snare.snare.toneDecay = value("snareToneDecay") * 0.001f;
    snare.snare.noiseDecay = value("snareNoiseDecay") * 0.001f;
    snare.snare.noiseTone = value("snareNoiseTone");
    snare.snare.snappy = value("snareSnappy");
    snare.snare.drive = value("snareDrive");
    snare.mix.gainDb = value("snareLevel");
    snare.type = value("snareEngine") > 0.5f ? zd::EngineType::Clap : zd::EngineType::AnalogSnare;
    {
        const int layer = static_cast<int>(value("snareLayer"));
        snare.layer = layer == 1 ? zd::EngineType::Clap : layer == 2 ? zd::EngineType::AnalogSnare : zd::EngineType::Off;
    }
    snare.layerGainDb = value("snareLayerLevel");
    snare.clap.toneHz = value("clapTone");
    snare.clap.spread = value("clapSpread") * 0.001f;
    snare.clap.bursts = static_cast<int>(value("clapHands"));
    snare.clap.decay = value("clapDecay") * 0.001f;
    snare.clap.tail = value("clapTail");
    snare.clap.snap = value("clapSnap");
    snare.clap.drive = value("clapDrive");

    auto& hat = engine.part(2);
    hat.sampleParams.pitchSemitones = value("hatPitch");
    hat.sampleParams.decay = value("hatDecay") * 0.001f;
    hat.mix.gainDb = value("hatLevel");

    auto& perc = engine.part(3);
    perc.fm.tuneHz = value("fmTune");
    perc.fm.ratio = value("fmRatio");
    perc.fm.index = value("fmIndex");
    perc.fm.indexDecay = value("fmIndexDecay") * 0.001f;
    perc.fm.decay = value("fmDecay") * 0.001f;
    perc.fm.feedback = value("fmFeedback");
    perc.mix.gainDb = value("fmLevel");

    auto& sub = engine.part(4);
    sub.sub.tuneHz = value("subTune");
    sub.sub.decay = value("subDecay") * 0.001f;
    sub.sub.glide = value("subGlide") * 0.001f;
    sub.sub.punch = value("subPunch");
    sub.sub.drive = value("subDrive");
    sub.mix.gainDb = value("subLevel");

    for (int i = 0; i < kNumStripParts; ++i)
    {
        auto& st = engine.part(i).strip;
        st.attack = stripValue(i, "Attack");
        st.sustain = stripValue(i, "Sustain");
        st.filterMode = static_cast<zd::FilterMode>(static_cast<int>(stripValue(i, "FilterMode")));
        st.cutoffHz = stripValue(i, "Cutoff");
        st.resonance = stripValue(i, "Resonance");
        st.drive = stripValue(i, "StripDrive");
        const bool crush = stripValue(i, "Crush") > 0.5f;
        const float rate = stripValue(i, "CrushRate");
        st.crushBits = crush ? stripValue(i, "CrushBits") : 0.0f;
        st.crushRateHz = crush && rate < 47999.0f ? rate : 0.0f;
        st.crushFilter = stripValue(i, "CrushFilter") > 0.5f;
        engine.part(i).velocityTone = stripValue(i, "VelTone");
        engine.part(i).mix.reverbSend = stripValue(i, "RevSend");
    }
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
    {
        const auto& pp = padParams_[static_cast<std::size_t>(i)];
        auto& part = engine.part(i);
        part.sampleParams.pitchSemitones = pp[0]->load(std::memory_order_relaxed);
        part.sampleParams.decay = pp[1]->load(std::memory_order_relaxed);
        part.sampleParams.start = pp[2]->load(std::memory_order_relaxed);
        part.mix.gainDb = pp[3]->load(std::memory_order_relaxed);
        part.mix.pan = pp[4]->load(std::memory_order_relaxed);
        part.mix.reverbSend = pp[5]->load(std::memory_order_relaxed);
    }
    zd::dsp::ReverbSettings rv;
    rv.decay = value("revDecay");
    rv.size = value("revSize");
    rv.preDelayMs = value("revPreDelay");
    rv.dampingHz = value("revDamping");
    rv.highPassHz = value("revHighPass");
    rv.returnDb = value("revLevel");
    engine.setReverb(rv);
    zd::dsp::GlueSettings glue;
    glue.on = value("glueOn") > 0.5f;
    glue.thresholdDb = value("glueThreshold");
    glue.ratio = value("glueRatio");
    glue.attackMs = value("glueAttack");
    glue.releaseMs = value("glueRelease");
    glue.makeupDb = value("glueMakeup");
    glue.mix = value("glueMix");
    engine.setGlue(glue);
    engine.part(4).chokes = value("subCutsKick") > 0.5f ? 1u : 0u;
    engine.part(0).chokes = value("kickCutsSub") > 0.5f ? (1u << 4) : 0u;

    engine.sequencer().pattern().swing = value("swing") * 0.01;
    engine.part(4).mix.duck = value("subDuck");
    engine.part(3).mix.duck = value("percDuck");
    engine.part(4).mix.duckRelease = engine.part(3).mix.duckRelease = value("duckRelease") * 0.001f;
    engine.setMasterGainDb(value("masterLevel"));
    engine.setMasterClip(value("clipOn") > 0.5f, value("clipDrive"), value("clipCeiling"));
}

void ZeroDrumProcessor::pullParameters() noexcept
{
    applyParameters(engine_);
}

float ZeroDrumProcessor::stripValue(int part, const char* suffix) const noexcept
{
    return stripParams_[static_cast<std::size_t>(part)][static_cast<std::size_t>(stripSuffixIndex(suffix))]->load(std::memory_order_relaxed);
}

void ZeroDrumProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, std::max(samplesPerBlock, 32));
    numPendingOffs_ = 0;
    // The master stage is always in, so the reported latency never changes; turning the
    // clipper off swaps in a matching delay.
    engine_.setMasterStage(true);
    setLatencySamples(engine_.masterLatency());
    zd::loadDefaultKit(engine_);
    applyPadSamples(engine_);
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        engine_.submitPattern(uiPattern_);
    }
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
        case 37: return 6; // part 6's own note (42) is the hat, so it moves here
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
        if (msg.getChannel() == kKickMidiChannel)
        {
            // Channel 3 plays the kick melodically (hardstyle and festival kick lines).
            midiEvents_[static_cast<std::size_t>(numEvents++)] = { meta.samplePosition, 0, msg.getFloatVelocity(), msg.getNoteNumber() };
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
            hostBpm_.store(transport.bpm, std::memory_order_relaxed);
            transport.ppqPosition = pos->getPpqPosition().orFallback(0.0);
        }
    }

    // Pad auditions from the UI land at the start of this block.
    for (int r = auditionRead_.load(std::memory_order_relaxed); r != auditionWrite_.load(std::memory_order_acquire);
         r = (r + 1) % kAuditionSlots)
    {
        if (numEvents < static_cast<int>(midiEvents_.size()))
            midiEvents_[static_cast<std::size_t>(numEvents++)] = auditions_[static_cast<std::size_t>(r)];
        auditionRead_.store((r + 1) % kAuditionSlots, std::memory_order_release);
    }

    // Note repeat on the 16th grid: the host's grid while it plays, our own clock otherwise.
    const double spb = getSampleRate() * 60.0 / juce::jmax(20.0, transport.bpm);
    const double startBeats = transport.playing ? transport.ppqPosition : repeatClock_;
    const double endBeats = startBeats + numSamples / spb;
    if (const int part = repeatPart_.load(std::memory_order_relaxed); part >= 0)
        for (double t = std::ceil(startBeats / 0.25) * 0.25; t < endBeats && numEvents < static_cast<int>(midiEvents_.size()); t += 0.25)
            midiEvents_[static_cast<std::size_t>(numEvents++)] = {
                juce::jlimit(0, numSamples - 1, static_cast<int>(std::lround((t - startBeats) * spb))), part,
                repeatVelocity_.load(std::memory_order_relaxed), -1 };
    repeatClock_ = transport.playing ? endBeats : (repeatPart_.load(std::memory_order_relaxed) >= 0 ? endBeats : 0.0);
    playheadBeats_.store(transport.ppqPosition, std::memory_order_relaxed);
    transportRunning_.store(transport.playing, std::memory_order_relaxed);

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    engine_.process(transport, midiEvents_.data(), numEvents, left, right, numSamples);
    writeMidiOut(midi, numSamples, transport.playing);
}

void ZeroDrumProcessor::writeMidiOut(juce::MidiBuffer& midi, int numSamples, bool playing) noexcept
{
    // The input notes were consumed above; the buffer now carries what the plugin sends.
    // JUCE's wrappers reserve this buffer up front, so adding a few events does not allocate.
    midi.clear();
    const bool on = value("midiOut") > 0.5f;
    auto removeOff = [this](int k) {
        pendingOffs_[static_cast<std::size_t>(k)] = pendingOffs_[static_cast<std::size_t>(--numPendingOffs_)];
    };

    // Switched off or stopped: end every held note now, so nothing hangs downstream.
    if (!on || !playing)
    {
        while (numPendingOffs_ > 0)
        {
            const auto& off = pendingOffs_[static_cast<std::size_t>(numPendingOffs_ - 1)];
            midi.addEvent(juce::MidiMessage::noteOff(off.channel, off.note), 0);
            --numPendingOffs_;
        }
        if (!on)
            return;
    }

    // Every note-off waits in pendingOffs_ (samplesLeft counted from this block's start).
    const int length = juce::jmax(1, static_cast<int>(std::lround(value("midiNoteLength") * 0.001 * getSampleRate())));
    const int root = static_cast<int>(value("genRoot"));
    const auto* events = engine_.sequencedEvents();
    for (int i = 0; i < engine_.numSequencedEvents(); ++i)
    {
        const auto& e = events[i];
        int channel = 1;
        int note = zd::exporting::defaultMidiNote(e.part);
        if (e.part == 4)
        {
            channel = kSubMidiChannel; // the 808 line, pitched, as in the exported MIDI
            note = e.note >= 0 ? e.note : root;
        }
        else if (e.part == 0 && e.note >= 0)
        {
            channel = kKickMidiChannel; // a melodic kick line (hardstyle, festival)
            note = e.note;
        }
        note = juce::jlimit(0, 127, note);
        // A note still held on the same key and channel ends right here, so pairs never overlap.
        for (int k = 0; k < numPendingOffs_; ++k)
        {
            const auto& off = pendingOffs_[static_cast<std::size_t>(k)];
            if (off.channel == channel && off.note == note)
            {
                midi.addEvent(juce::MidiMessage::noteOff(channel, note), e.offset);
                removeOff(k);
                break;
            }
        }
        if (numPendingOffs_ >= kMaxPendingOffs)
            continue;
        const auto velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(std::lround(e.velocity * 127.0f))));
        midi.addEvent(juce::MidiMessage::noteOn(channel, note, velocity), e.offset);
        pendingOffs_[static_cast<std::size_t>(numPendingOffs_++)] = { e.offset + length, channel, note };
    }

    // Offs due in this block go out; the rest move on to the next block.
    for (int k = 0; k < numPendingOffs_;)
    {
        auto& off = pendingOffs_[static_cast<std::size_t>(k)];
        if (off.samplesLeft < numSamples)
        {
            midi.addEvent(juce::MidiMessage::noteOff(off.channel, off.note), juce::jmax(0, off.samplesLeft));
            removeOff(k);
            continue;
        }
        off.samplesLeft -= numSamples;
        ++k;
    }
}

void ZeroDrumProcessor::auditionPart(int part, float velocity) noexcept
{
    const int w = auditionWrite_.load(std::memory_order_relaxed);
    const int next = (w + 1) % kAuditionSlots;
    if (next == auditionRead_.load(std::memory_order_acquire))
        return; // full: drop the hit rather than block
    auditions_[static_cast<std::size_t>(w)] = { 0, juce::jlimit(0, zd::kMaxParts - 1, part), juce::jlimit(0.05f, 1.0f, velocity), -1 };
    auditionWrite_.store(next, std::memory_order_release);
}

void ZeroDrumProcessor::setNoteRepeat(int part, float velocity) noexcept
{
    repeatVelocity_.store(juce::jlimit(0.05f, 1.0f, velocity), std::memory_order_relaxed);
    repeatPart_.store(part, std::memory_order_relaxed);
}

zd::Pattern ZeroDrumProcessor::currentPattern() const
{
    const std::lock_guard<std::mutex> lock(patternMutex_);
    return uiPattern_;
}

void ZeroDrumProcessor::editPattern(const std::function<void(zd::Pattern&)>& edit)
{
    zd::Pattern next;
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        edit(uiPattern_);
        next = uiPattern_;
    }
    publishPattern(next);
}

// ------------------------------------------------------------------------------- export

namespace {

/// Keeps non-ASCII paths intact (Windows user folders often have them).
std::filesystem::path toPath(const juce::File& file)
{
#if JUCE_WINDOWS
    return std::filesystem::path(file.getFullPathName().toWideCharPointer());
#else
    return std::filesystem::path(file.getFullPathName().toStdString());
#endif
}

} // namespace

double ZeroDrumProcessor::exportBpm() const noexcept
{
    const double bpm = value("freeRun") > 0.5f ? static_cast<double>(value("freeRunBpm")) : hostBpm_.load(std::memory_order_relaxed);
    return juce::jlimit(20.0, 400.0, bpm);
}

zd::Pattern ZeroDrumProcessor::patternSnapshot() const
{
    std::lock_guard lock(patternMutex_);
    zd::Pattern p = uiPattern_;
    p.swing = value("swing") * 0.01;
    return p;
}

std::array<std::string, zd::kMaxParts> ZeroDrumProcessor::partNames() const
{
    std::array<std::string, zd::kMaxParts> names {};
    for (int i = 0; i < kNumStripParts; ++i)
        names[static_cast<std::size_t>(i)] = kStripNames[i];
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
        if (padHasSample(i))
            names[static_cast<std::size_t>(i)] = padName(i).toStdString();
    return names;
}

// ------------------------------------------------------------------------- pad samples

void ZeroDrumProcessor::applyPadSamples(zd::Engine& engine) const
{
    // Only while that engine is not processing (prepare time or an offline copy).
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
    {
        const auto& s = padSamples_[static_cast<std::size_t>(i)];
        engine.setSample(i, s);
        engine.part(i).type = s ? zd::EngineType::Sample : zd::EngineType::Off;
    }
}

bool ZeroDrumProcessor::isAudioFile(const juce::String& path) const
{
    return formats_.findFormatForFileExtension(juce::File(path).getFileExtension()) != nullptr;
}

bool ZeroDrumProcessor::padHasSample(int part) const noexcept
{
    return isSamplePad(part) && padSamples_[static_cast<std::size_t>(part)] != nullptr;
}

// ------------------------------------------------------------------------ part and kit files

int ZeroDrumProcessor::partOfParam(const juce::String& id) noexcept
{
    // Per-part strip and pad parameters: "p<index><Suffix>", e.g. p3Cutoff, p12Level.
    if (id.startsWithChar('p') && id.length() > 2 && juce::CharacterFunctions::isDigit(id[1]))
    {
        int i = 1, index = 0;
        while (i < id.length() && juce::CharacterFunctions::isDigit(id[i]))
            index = index * 10 + (id[i++] - '0');
        if (i < id.length() && juce::CharacterFunctions::isUpperCase(id[i]) && index < zd::kMaxParts)
            return index;
        return -1;
    }
    if (id.startsWith("lock"))
        return -1; // generator locks are about the session, not the sound
    if (id.startsWith("kick"))
        return 0; // kickCutsSub too: what the kick does to the 808
    if (id.startsWith("snare") || id.startsWith("clap"))
        return 1;
    if (id.startsWith("hat"))
        return 2;
    if (id.startsWith("fm") || id == "percDuck")
        return 3;
    if (id.startsWith("sub"))
        return 4; // subDuck and subCutsKick too
    return -1;
}

juce::ValueTree ZeroDrumProcessor::partToTree(int part) const
{
    juce::ValueTree tree("ZeroDrumPart");
    tree.setProperty("version", 1, nullptr);
    tree.setProperty("part", part, nullptr);
    tree.setProperty("name", padName(part), nullptr);
    for (const auto& idStd : paramIds_)
    {
        const juce::String id(idStd);
        if (partOfParam(id) != part)
            continue;
        if (auto* param = apvts_.getParameter(id))
        {
            juce::ValueTree p("param");
            // Pad parameters are stored without their index, so a pad file fits any pad.
            p.setProperty("id", isSamplePad(part) ? id.substring(1 + juce::String(part).length()) : id, nullptr);
            p.setProperty("value", param->convertFrom0to1(param->getValue()), nullptr);
            tree.appendChild(p, nullptr);
        }
    }
    if (padHasSample(part))
        tree.setProperty("sample", padFiles_[static_cast<std::size_t>(part)].getFullPathName(), nullptr);
    zd::Pattern lane {};
    lane.lanes[0] = currentPattern().lanes[static_cast<std::size_t>(part)];
    tree.setProperty("lane", juce::String(zd::serializePattern(lane)), nullptr);
    return tree;
}

bool ZeroDrumProcessor::partFromTree(int part, const juce::ValueTree& tree, bool withLane)
{
    if (!tree.hasType("ZeroDrumPart") || part < 0 || part >= zd::kMaxParts)
        return false;
    const int from = tree.getProperty("part", -1);
    const bool padFile = isSamplePad(from);
    if (padFile ? !isSamplePad(part) : from != part)
        return false; // a kick file only fits the kick, a pad file any pad
    for (const auto& p : tree)
    {
        if (!p.hasType("param"))
            continue;
        const juce::String stored = p.getProperty("id").toString();
        const juce::String id = padFile ? "p" + juce::String(part) + stored : stored;
        if (partOfParam(id) == part)
            setParam(id.toRawUTF8(), static_cast<float>(p.getProperty("value")));
    }
    if (padFile)
    {
        const juce::String path = tree.getProperty("sample").toString();
        const juce::File file = path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File {};
        if (!file.existsAsFile() || !loadPadSample(part, file))
            clearPadSample(part);
    }
    zd::Pattern lane;
    if (withLane && zd::deserializePattern(tree.getProperty("lane").toString().toStdString(), lane))
        editPattern([part, l = lane.lanes[0]](zd::Pattern& pat) { pat.lanes[static_cast<std::size_t>(part)] = l; });
    return true;
}

bool ZeroDrumProcessor::savePart(int part, const juce::File& file) const
{
    if (auto xml = partToTree(part).createXml())
        return xml->writeTo(file);
    return false;
}

bool ZeroDrumProcessor::loadPart(int part, const juce::File& file, bool withLane)
{
    if (auto xml = juce::parseXML(file))
        return partFromTree(part, juce::ValueTree::fromXml(*xml), withLane);
    return false;
}

bool ZeroDrumProcessor::saveKit(const juce::File& file)
{
    juce::MemoryBlock data;
    getStateInformation(data);
    if (auto xml = getXmlFromBinary(data.getData(), static_cast<int>(data.getSize())))
        return xml->writeTo(file);
    return false;
}

bool ZeroDrumProcessor::loadKit(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (xml == nullptr || !xml->hasTagName(apvts_.state.getType()))
        return false;
    juce::MemoryBlock data;
    copyXmlToBinary(*xml, data);
    setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    return true;
}

juce::String ZeroDrumProcessor::padName(int part) const
{
    if (part >= 0 && part < kNumStripParts)
        return kStripNames[part];
    if (padHasSample(part))
        return padFiles_[static_cast<std::size_t>(part)].getFileNameWithoutExtension();
    return "Part " + juce::String(part + 1);
}

bool ZeroDrumProcessor::loadPadSample(int part, const juce::File& file)
{
    if (!isSamplePad(part))
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return false;
    const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(reader->sampleRate * kMaxSampleSeconds)));
    const int channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    if (!reader->read(&buffer, 0, length, 0, true, true))
        return false;

    auto data = std::make_shared<zd::SampleData>();
    data->sampleRate = reader->sampleRate;
    data->samples.assign(static_cast<std::size_t>(length), 0.0f);
    const float scale = 1.0f / static_cast<float>(channels);
    for (int c = 0; c < channels; ++c)
    {
        const float* in = buffer.getReadPointer(c);
        for (int n = 0; n < length; ++n)
            data->samples[static_cast<std::size_t>(n)] += in[n] * scale;
    }
    padSamples_[static_cast<std::size_t>(part)] = data;
    padFiles_[static_cast<std::size_t>(part)] = file;
    engine_.submitSample(part, std::move(data));
    return true;
}

void ZeroDrumProcessor::clearPadSample(int part)
{
    if (!isSamplePad(part))
        return;
    padSamples_[static_cast<std::size_t>(part)].reset();
    padFiles_[static_cast<std::size_t>(part)] = juce::File {};
    engine_.submitSample(part, nullptr);
}

void ZeroDrumProcessor::exportLoopAsync(const juce::File& directory, ExportCallback done)
{
    if (exporting_.exchange(true))
        return;

    // Everything the job needs is captured now, on the message thread.
    auto engine = std::make_shared<zd::Engine>();
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    engine->prepare(sr, 512);
    zd::loadDefaultKit(*engine);
    engine->setMasterStage(true); // export through the same clipper as playback
    engine->sequencer().pattern() = patternSnapshot();
    applyParameters(*engine);
    applyPadSamples(*engine);

    zd::exporting::ExportOptions options;
    options.bpm = exportBpm();
    options.bars = zd::exporting::loopBars(engine->sequencer().pattern());
    options.partNames = partNames();
    const auto path = toPath(directory);

    exportPool_.addJob([this, engine, options, path, done = std::move(done)] {
        auto result = zd::exporting::exportLoop(*engine, options, path);
        exporting_.store(false);
        juce::MessageManager::callAsync([result = std::move(result), done] {
            if (done)
                done(result);
        });
    });
}

juce::File ZeroDrumProcessor::writeMidiClip(int lane) const
{
    zd::Sequencer sequencer;
    sequencer.pattern() = patternSnapshot();
    const int bars = zd::exporting::loopBars(sequencer.pattern());
    const auto names = partNames();
    const auto tracks = zd::exporting::patternTracks(sequencer, bars, names, 1u << 4, lane);
    if (tracks.empty())
        return {};

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Zero Drum MIDI");
    if (!dir.createDirectory())
        return {};
    const juce::String which = lane >= 0 ? juce::String(names[static_cast<std::size_t>(lane)]) : juce::String("Beat");
    const auto file = dir.getChildFile("Zero Drum " + which + " " + juce::String(juce::roundToInt(exportBpm())) + " BPM.mid");
    if (!zd::exporting::writeMidi(toPath(file), tracks, exportBpm()))
        return {};
    return file;
}

juce::AudioProcessorEditor* ZeroDrumProcessor::createEditor()
{
    return new ZeroDrumEditor(*this);
}

// ------------------------------------------------------------------------- beat generator

std::uint32_t ZeroDrumProcessor::lockMask() const noexcept
{
    std::uint32_t mask = 0;
    for (int i = 0; i < kNumStripParts; ++i)
        if (value(("lock" + std::to_string(i)).c_str()) > 0.5f)
            mask |= 1u << i;
    return mask;
}

void ZeroDrumProcessor::publishPattern(const zd::Pattern& pattern)
{
    // The audio thread may be mid-copy for a moment; the retry is bounded and cheap.
    for (int attempt = 0; attempt < 1000 && !engine_.submitPattern(pattern); ++attempt)
        std::this_thread::yield();
}

void ZeroDrumProcessor::setParam(const char* id, float plainValue)
{
    if (auto* param = apvts_.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

void ZeroDrumProcessor::loadStyleKit(zd::gen::Style style, int rootNote, std::uint32_t keepMask)
{
    // Writes the kit through the parameters, so it shows in the UI, is saved with the
    // project and can be automated. Locked parts keep their sound as well as their lane.
    const zd::gen::StyleKit kit = zd::gen::styleKit(style);
    auto keep = [&](int part) { return ((keepMask >> part) & 1u) != 0; };
    const auto& lv = kit.levelDb;

    if (!keep(0))
    {
        setParam("kickTune", zd::gen::rootTunedKickHz(rootNote, kit.kickInterval, kit.kick.tuneHz));
        setParam("kickSweep", kit.kick.pitchAmount);
        setParam("kickSweepTime", kit.kick.pitchDecay * 1000.0f);
        setParam("kickDecay", kit.kick.decay * 1000.0f);
        setParam("kickClick", kit.kick.click);
        setParam("kickDrive", kit.kick.drive);
        setParam("kickDrive2", kit.kick.drive2);
        setParam("kickMidFreq", kit.kick.midHz);
        setParam("kickMidGain", kit.kick.midDb);
        setParam("kickLevel", lv[0]);
    }
    if (!keep(1))
    {
        setParam("snareTune", kit.snare.tuneHz);
        setParam("snareToneDecay", kit.snare.toneDecay * 1000.0f);
        setParam("snareNoiseDecay", kit.snare.noiseDecay * 1000.0f);
        setParam("snareNoiseTone", kit.snare.noiseTone);
        setParam("snareSnappy", kit.snare.snappy);
        setParam("snareDrive", kit.snare.drive);
        setParam("snareLevel", lv[1]);
        setParam("snareEngine", kit.snareEngine == zd::EngineType::Clap ? 1.0f : 0.0f);
        setParam("snareLayer", kit.snareLayer == zd::EngineType::Clap ? 1.0f : kit.snareLayer == zd::EngineType::AnalogSnare ? 2.0f : 0.0f);
        setParam("snareLayerLevel", kit.snareLayerDb);
        setParam("clapTone", kit.clap.toneHz);
        setParam("clapSpread", kit.clap.spread * 1000.0f);
        setParam("clapHands", static_cast<float>(kit.clap.bursts));
        setParam("clapDecay", kit.clap.decay * 1000.0f);
        setParam("clapTail", kit.clap.tail);
        setParam("clapSnap", kit.clap.snap);
        setParam("clapDrive", kit.clap.drive);
    }
    if (!keep(2))
    {
        setParam("hatPitch", kit.hat.pitchSemitones);
        setParam("hatDecay", kit.hat.decay * 1000.0f);
        setParam("hatLevel", lv[2]);
    }
    if (!keep(3))
    {
        setParam("fmTune", kit.perc.tuneHz);
        setParam("fmRatio", kit.perc.ratio);
        setParam("fmIndex", kit.perc.index);
        setParam("fmIndexDecay", kit.perc.indexDecay * 1000.0f);
        setParam("fmDecay", kit.perc.decay * 1000.0f);
        setParam("fmFeedback", kit.perc.feedback);
        setParam("fmLevel", lv[3]);
        setParam("percDuck", kit.percDuck);
    }
    if (!keep(4))
    {
        setParam("subTune", static_cast<float>(zd::noteToHz(rootNote)));
        setParam("subDecay", kit.sub.decay * 1000.0f);
        setParam("subGlide", kit.sub.glide * 1000.0f);
        setParam("subPunch", kit.sub.punch);
        setParam("subDrive", kit.sub.drive);
        setParam("subLevel", lv[4]);
        setParam("subDuck", kit.subDuck);
        setParam("duckRelease", kit.duckRelease * 1000.0f);
    }
    for (int i = 0; i < kNumStripParts; ++i)
    {
        if (keep(i))
            continue;
        const auto& st = kit.strips[static_cast<std::size_t>(i)];
        const std::string id = "p" + std::to_string(i);
        setParam((id + "Attack").c_str(), st.attack);
        setParam((id + "Sustain").c_str(), st.sustain);
        setParam((id + "FilterMode").c_str(), static_cast<float>(st.filterMode));
        setParam((id + "Cutoff").c_str(), st.cutoffHz);
        setParam((id + "Resonance").c_str(), st.resonance);
        setParam((id + "StripDrive").c_str(), st.drive);
        const bool crush = st.crushBits > 0.0f || st.crushRateHz > 0.0f;
        setParam((id + "Crush").c_str(), crush ? 1.0f : 0.0f);
        if (crush)
        {
            setParam((id + "CrushBits").c_str(), st.crushBits > 0.0f ? st.crushBits : 16.0f);
            setParam((id + "CrushRate").c_str(), st.crushRateHz > 0.0f ? st.crushRateHz : 48000.0f);
            setParam((id + "CrushFilter").c_str(), st.crushFilter ? 1.0f : 0.0f);
        }
        setParam((id + "VelTone").c_str(), kit.velocityTone[static_cast<std::size_t>(i)]);
        setParam((id + "RevSend").c_str(), kit.reverbSend[static_cast<std::size_t>(i)]);
    }
    setParam("revDecay", kit.reverb.decay);
    setParam("revSize", kit.reverb.size);
    setParam("revPreDelay", kit.reverb.preDelayMs);
    setParam("revDamping", kit.reverb.dampingHz);
    setParam("revHighPass", kit.reverb.highPassHz);
    setParam("revLevel", kit.reverb.returnDb);
    setParam("glueOn", kit.glue.on ? 1.0f : 0.0f);
    setParam("glueThreshold", kit.glue.thresholdDb);
    setParam("glueRatio", kit.glue.ratio);
    setParam("glueAttack", kit.glue.attackMs);
    setParam("glueRelease", kit.glue.releaseMs);
    setParam("glueMakeup", kit.glue.makeupDb);
    setParam("glueMix", kit.glue.mix);
    if (!keep(4))
        setParam("subCutsKick", kit.subCutsKick ? 1.0f : 0.0f);
}

void ZeroDrumProcessor::generateBeat()
{
    zd::gen::GenerateParams p;
    p.style = static_cast<zd::gen::Style>(static_cast<int>(value("genStyle")));
    p.density = value("genDensity");
    p.complexity = value("genComplexity");
    p.groove = value("genGroove");
    p.lockMask = lockMask();
    if (value("genKit") > 0.5f)
    {
        // The style's home key comes with its kit, unless the 808 lane is locked.
        if ((p.lockMask & (1u << 4)) == 0)
            setParam("genRoot", static_cast<float>(zd::gen::styleKit(p.style).rootNote));
        loadStyleKit(p.style, static_cast<int>(value("genRoot")), p.lockMask);
    }
    p.rootNote = static_cast<int>(value("genRoot"));

    zd::Pattern next;
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        seed_ = static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()) & 0x7fffffffffffull;
        p.seed = seed_;
        zd::gen::generate(p, uiPattern_);
        next = uiPattern_;
    }
    // The style's swing becomes the swing parameter, so it is visible and automatable.
    if (auto* swing = apvts_.getParameter("swing"))
        swing->setValueNotifyingHost(swing->convertTo0to1(static_cast<float>(next.swing * 100.0)));
    publishPattern(next);
}

void ZeroDrumProcessor::mutateBeat()
{
    zd::Pattern next;
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        seed_ = zd::dsp::hash64(seed_ + 1) & 0x7fffffffffffull;
        zd::gen::mutate(uiPattern_, 0.6f, seed_, lockMask());
        next = uiPattern_;
    }
    publishPattern(next);
}

void ZeroDrumProcessor::addFillToBeat()
{
    zd::Pattern next;
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        zd::gen::addFill(uiPattern_, value("genComplexity"), seed_ + 7, lockMask());
        next = uiPattern_;
    }
    publishPattern(next);
}

void ZeroDrumProcessor::makeBuildBeat()
{
    zd::Pattern next;
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        zd::gen::makeBuild(uiPattern_, 4, lockMask());
        next = uiPattern_;
    }
    publishPattern(next);
}

juce::String ZeroDrumProcessor::describeBeat() const
{
    const std::lock_guard<std::mutex> lock(patternMutex_);
    juce::String text;
    for (int i = 0; i < kNumStripParts; ++i)
        text << kStripNames[i] << " " << zd::gen::countHits(uiPattern_.lanes[static_cast<std::size_t>(i)]) << "   ";
    return text + "seed " + juce::String(static_cast<juce::int64>(seed_));
}

// --------------------------------------------------------------------------------- state

void ZeroDrumProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    {
        const std::lock_guard<std::mutex> lock(patternMutex_);
        state.setProperty("pattern", juce::String(zd::serializePattern(uiPattern_)), nullptr);
        state.setProperty("seed", juce::String(static_cast<juce::int64>(seed_)), nullptr);
    }
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
    {
        const juce::Identifier key("pad" + juce::String(i) + "Sample");
        if (padHasSample(i))
            state.setProperty(key, padFiles_[static_cast<std::size_t>(i)].getFullPathName(), nullptr);
        else
            state.removeProperty(key, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ZeroDrumProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts_.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    zd::Pattern loaded;
    const bool hasPattern = zd::deserializePattern(tree.getProperty("pattern").toString().toStdString(), loaded);
    const auto seed = static_cast<std::uint64_t>(tree.getProperty("seed", 1).toString().getLargeIntValue());
    apvts_.replaceState(tree);
    // Samples are saved by path; a file that moved leaves its pad empty.
    for (int i = kFirstSamplePad; i < zd::kMaxParts; ++i)
    {
        const auto path = tree.getProperty("pad" + juce::String(i) + "Sample").toString();
        const juce::File file = path.isNotEmpty() && juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File {};
        if (!file.existsAsFile() || !loadPadSample(i, file))
            clearPadSample(i);
    }

    if (hasPattern)
    {
        {
            const std::lock_guard<std::mutex> lock(patternMutex_);
            uiPattern_ = loaded;
            seed_ = seed;
        }
        publishPattern(loaded);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZeroDrumProcessor();
}
