#include "SoundPage.h"

#include "LookAndFeel.h"
#include "Theme.h"

#include <algorithm>

namespace {

constexpr int kListWidth = 168;
constexpr int kRowHeight = 30;
constexpr int kCell = 84;
constexpr int kCellHeight = 104;
constexpr int kSectionHeader = 36;

const juce::StringArray& sessionIds()
{
    static const juce::StringArray ids { "genDensity", "genComplexity", "genGroove", "genRoot", "genKit",
                                         "swing",      "freeRun",       "freeRunBpm", "repeatRate", "padFullLevel" };
    return ids;
}

constexpr int kWaveHeight = 132;

/// A sample pad's waveform: the start point (drag it), the decay window and the play direction.
class WaveView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    WaveView(ZeroDrumProcessor& owner, int part, juce::Colour colour) : owner_(owner), part_(part), colour_(colour)
    {
        const juce::String id = "p" + juce::String(part);
        start_ = owner.state().getParameter(id + "SmpStart");
        decay_ = owner.state().getParameter(id + "SmpDecay");
        reverse_ = owner.state().getParameter(id + "SmpReverse");
        pitch_ = owner.state().getParameter(id + "SmpPitch");
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("Drag to set where the sample starts.");
        startTimerHz(15);
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(zdui::kBackground);
        g.fillRoundedRectangle(r, 6.0f);
        const auto data = owner_.padSample(part_);
        if (data == nullptr || data->samples.empty())
            return;
        computePeaks(*data, getWidth());
        const bool reversed = reverse_ != nullptr && reverse_->getValue() > 0.5f;
        const float startFrac = start_ != nullptr ? start_->convertFrom0to1(start_->getValue()) : 0.0f;
        const float startX = reversed ? r.getWidth() * (1.0f - startFrac) : r.getWidth() * startFrac;

        // The decay window: how much of the sample a hit lets through (0 s plays it all).
        float endX = reversed ? 0.0f : r.getWidth();
        if (decay_ != nullptr)
        {
            const double seconds = decay_->convertFrom0to1(decay_->getValue());
            if (seconds > 0.005)
            {
                const double semis = pitch_ != nullptr ? pitch_->convertFrom0to1(pitch_->getValue()) : 0.0;
                const double length = static_cast<double>(data->samples.size()) / data->sampleRate;
                const float span = static_cast<float>(seconds * std::pow(2.0, semis / 12.0) / length) * r.getWidth();
                endX = reversed ? juce::jmax(0.0f, startX - span) : juce::jmin(r.getWidth(), startX + span);
            }
        }
        const float lo = juce::jmin(startX, endX), hi = juce::jmax(startX, endX);
        g.setColour(colour_.withAlpha(0.08f));
        g.fillRect(lo, r.getY(), hi - lo, r.getHeight());

        const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
        for (int x = 0; x < static_cast<int>(peaks_.size()); ++x)
        {
            const float fx = static_cast<float>(x);
            const bool live = fx >= lo && fx <= hi;
            g.setColour(live ? colour_ : zdui::kDim.withAlpha(0.35f));
            const float h = juce::jmax(0.5f, peaks_[static_cast<std::size_t>(x)] * half);
            g.fillRect(fx, mid - h, 1.0f, h * 2.0f);
        }
        // Start marker with a grab handle and the play direction.
        g.setColour(zdui::kText);
        g.fillRect(startX - 1.0f, r.getY() + 4.0f, 2.0f, r.getHeight() - 8.0f);
        juce::Path flag;
        const float dir = reversed ? -1.0f : 1.0f;
        flag.addTriangle(startX, r.getY() + 4.0f, startX + dir * 9.0f, r.getY() + 9.0f, startX, r.getY() + 14.0f);
        g.fillPath(flag);
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(zdui::kCaption));
        const double length = static_cast<double>(data->samples.size()) / data->sampleRate;
        const auto info = juce::String(length, 2) + " s" + (reversed ? "   reversed" : "");
        g.drawText(info, getLocalBounds().reduced(8, 4), juce::Justification::bottomRight);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (start_ != nullptr)
            start_->beginChangeGesture();
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (start_ == nullptr || getWidth() <= 0)
            return;
        float frac = juce::jlimit(0.0f, 1.0f, static_cast<float>(e.x) / static_cast<float>(getWidth()));
        if (reverse_ != nullptr && reverse_->getValue() > 0.5f)
            frac = 1.0f - frac;
        start_->setValueNotifyingHost(start_->convertTo0to1(frac));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (start_ != nullptr)
            start_->endChangeGesture();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (start_ != nullptr)
            start_->setValueNotifyingHost(start_->getDefaultValue());
    }

private:
    void timerCallback() override
    {
        const float now[] { start_ ? start_->getValue() : 0.0f, decay_ ? decay_->getValue() : 0.0f,
                            reverse_ ? reverse_->getValue() : 0.0f, pitch_ ? pitch_->getValue() : 0.0f };
        if (!std::equal(std::begin(now), std::end(now), std::begin(last_)))
        {
            std::copy(std::begin(now), std::end(now), std::begin(last_));
            repaint();
        }
    }

    void computePeaks(const zd::SampleData& data, int width)
    {
        if (&data == peaksOf_ && static_cast<int>(peaks_.size()) == width)
            return;
        peaksOf_ = &data;
        peaks_.assign(static_cast<std::size_t>(juce::jmax(0, width)), 0.0f);
        const auto n = data.samples.size();
        float top = 1.0e-6f;
        for (std::size_t x = 0; x < peaks_.size(); ++x)
        {
            const std::size_t a = x * n / peaks_.size(), b = juce::jmax(a + 1, (x + 1) * n / peaks_.size());
            float peak = 0.0f;
            for (std::size_t i = a; i < b && i < n; ++i)
                peak = juce::jmax(peak, std::abs(data.samples[i]));
            peaks_[x] = peak;
            top = juce::jmax(top, peak);
        }
        for (auto& p : peaks_)
            p /= top; // normalised, so quiet samples still read
    }

    ZeroDrumProcessor& owner_;
    const int part_;
    const juce::Colour colour_;
    juce::RangedAudioParameter* start_ = nullptr;
    juce::RangedAudioParameter* decay_ = nullptr;
    juce::RangedAudioParameter* reverse_ = nullptr;
    juce::RangedAudioParameter* pitch_ = nullptr;
    std::vector<float> peaks_;
    const zd::SampleData* peaksOf_ = nullptr;
    float last_[4] {};
};

} // namespace

/// One parameter's control: a knob, a switch or a menu, with its caption.
struct SoundPage::Control
{
    juce::Label caption;
    std::unique_ptr<juce::Slider> knob;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::ComboBox> menu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
    juce::Component* widget() const
    {
        return knob ? static_cast<juce::Component*>(knob.get()) : toggle ? static_cast<juce::Component*>(toggle.get()) : menu.get();
    }
};

struct SoundPage::Section
{
    juce::String title;
    std::vector<std::unique_ptr<Control>> controls;
    juce::Rectangle<int> bounds;
};

/// The scrolling side: titled cards that wrap their controls to the width.
class SoundPage::Content final : public juce::Component
{
public:
    std::vector<std::unique_ptr<Section>> sections;
    juce::Colour colour = zdui::kAccent;
    std::unique_ptr<juce::Component> wave; ///< sample pads: the waveform, in the first card

    int layout(int width)
    {
        int y = 0;
        const int waveSpace = wave ? kWaveHeight + 8 : 0;
        const int perRow = juce::jmax(1, (width - 32) / kCell);
        for (auto& s : sections)
        {
            const int rows = (static_cast<int>(s->controls.size()) + perRow - 1) / perRow;
            const int extra = (wave && &s == &sections.front()) ? waveSpace : 0;
            if (extra > 0)
                wave->setBounds(16, y + kSectionHeader, width - 32, kWaveHeight);
            s->bounds = { 0, y, width, kSectionHeader + extra + rows * kCellHeight + 12 };
            const int top = y + extra;
            for (std::size_t i = 0; i < s->controls.size(); ++i)
            {
                auto& c = *s->controls[i];
                const int col = static_cast<int>(i) % perRow, row = static_cast<int>(i) / perRow;
                auto r = juce::Rectangle<int>(16 + col * kCell, top + kSectionHeader + row * kCellHeight, kCell, kCellHeight);
                c.caption.setBounds(r.removeFromTop(16));
                if (c.knob)
                    c.knob->setBounds(r.reduced(6, 0).withTrimmedBottom(8));
                else if (c.toggle)
                    c.toggle->setBounds(r.withSizeKeepingCentre(kCell - 12, 28).withX(r.getX() + 14));
                else if (c.menu)
                    c.menu->setBounds(r.withSizeKeepingCentre(kCell - 8, 28));
            }
            y = s->bounds.getBottom() + 8;
        }
        return y;
    }

    void paint(juce::Graphics& g) override
    {
        for (const auto& s : sections)
        {
            g.setColour(zdui::kPanel);
            g.fillRoundedRectangle(s->bounds.toFloat(), 8.0f);
            auto head = s->bounds.reduced(16, 0).removeFromTop(kSectionHeader);
            g.setColour(colour);
            g.fillEllipse(head.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f));
            head.removeFromLeft(8);
            g.setColour(zdui::kText);
            g.setFont(juce::FontOptions(zdui::kLabel + 1.0f, juce::Font::bold));
            g.drawText(s->title.toUpperCase(), head, juce::Justification::centredLeft);
        }
    }
};

SoundPage::SoundPage(ZeroDrumProcessor& owner) : owner_(owner), content_(std::make_unique<Content>())
{
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);
    refreshList();
    rebuild();
}

SoundPage::~SoundPage()
{
    viewport_.setViewedComponent(nullptr, false);
}

void SoundPage::refreshList()
{
    listParts_.clear();
    for (int p = 0; p < ZeroDrumProcessor::kNumStripParts; ++p)
        listParts_.push_back(p);
    for (int p = ZeroDrumProcessor::kFirstSamplePad; p < zd::kMaxParts; ++p)
        if (owner_.padHasSample(p) || p == part_)
            listParts_.push_back(p); // the shown pad stays listed even while it is empty
    listParts_.push_back(kSession);
    if (std::find(listParts_.begin(), listParts_.end(), part_) == listParts_.end())
        part_ = 0;
    repaint();
}

void SoundPage::showPart(int part)
{
    if (part != kSession && (part < 0 || part >= zd::kMaxParts))
        return;
    part_ = part;
    refreshList();
    rebuild();
}

juce::String SoundPage::shortName(const juce::String& name) const
{
    // "Kick Tune" -> "Tune", "Pad 9 Pitch" -> "Pitch"; other prefixes (Clap on the snare) stay.
    juce::String prefix;
    if (part_ >= 0 && part_ < ZeroDrumProcessor::kNumStripParts)
        prefix = juce::String(ZeroDrumProcessor::kStripNames[part_]) + " ";
    else if (part_ >= 0)
        prefix = "Pad " + juce::String(part_ + 1) + " ";
    return prefix.isNotEmpty() && name.startsWith(prefix) ? name.substring(prefix.length()) : name;
}

void SoundPage::rebuild()
{
    content_->removeAllChildren();
    content_->sections.clear();
    content_->wave.reset();
    content_->colour = part_ == kSession ? zdui::kDim : zdui::laneColour(part_);

    auto section = [this](const juce::String& title) -> Section& {
        for (auto& s : content_->sections)
            if (s->title == title)
                return *s;
        content_->sections.push_back(std::make_unique<Section>());
        content_->sections.back()->title = title;
        return *content_->sections.back();
    };
    // Fixed order, so the cards never jump around.
    if (part_ == kSession)
    {
        section("Generator");
        section("Groove and clock");
        section("Pads");
    }
    else if (part_ < ZeroDrumProcessor::kNumStripParts)
    {
        section("Voice");
        section("Channel strip");
    }
    else
        section("Sample");

    const juce::String stripPrefix = "p" + juce::String(part_);
    for (auto* p : owner_.getParameters())
    {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (withId == nullptr)
            continue;
        const juce::String id = withId->getParameterID();
        if (id.startsWith("lock"))
            continue;
        juce::String title;
        if (part_ == kSession)
        {
            if (!sessionIds().contains(id))
                continue;
            title = id.startsWith("gen") ? "Generator" : (id == "repeatRate" || id == "padFullLevel") ? "Pads" : "Groove and clock";
        }
        else
        {
            if (ZeroDrumProcessor::partOfParam(id) != part_)
                continue;
            const bool strip = id.startsWith(stripPrefix) && id.length() > stripPrefix.length()
                               && juce::CharacterFunctions::isUpperCase(id[stripPrefix.length()]);
            title = part_ >= ZeroDrumProcessor::kNumStripParts ? "Sample" : strip ? "Channel strip" : "Voice";
        }

        auto c = std::make_unique<Control>();
        const auto label = part_ == kSession ? withId->getName(40) : shortName(withId->getName(40));
        const auto colour = content_->colour;
        if (dynamic_cast<juce::AudioParameterBool*>(withId) != nullptr)
        {
            c->toggle = std::make_unique<juce::ToggleButton>("On");
            c->toggle->setColour(juce::ToggleButton::tickColourId, colour);
            c->toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), id, *c->toggle);
            auto* toggle = c->toggle.get();
            toggle->onStateChange = [toggle] { toggle->setButtonText(toggle->getToggleState() ? "On" : "Off"); };
            toggle->onStateChange();
            c->caption.setText(label, juce::dontSendNotification);
        }
        else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(withId))
        {
            c->menu = std::make_unique<juce::ComboBox>();
            c->menu->addItemList(choice->choices, 1);
            c->menu->setWantsKeyboardFocus(false);
            c->menuAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner_.state(), id, *c->menu);
            c->caption.setText(label, juce::dontSendNotification);
        }
        else
        {
            c->knob = std::make_unique<juce::Slider>();
            zdui::styleKnob(*c->knob, c->caption, label, colour);
            c->knobAttachment = zdui::attachKnob(owner_.state(), id, *c->knob);
        }
        c->caption.setJustificationType(juce::Justification::centred);
        c->caption.setColour(juce::Label::textColourId, zdui::kDim);
        c->caption.setFont(juce::FontOptions(zdui::kCaption, juce::Font::bold));
        c->caption.setMinimumHorizontalScale(0.7f);
        content_->addAndMakeVisible(c->caption);
        content_->addAndMakeVisible(*c->widget());
        section(title).controls.push_back(std::move(c));
    }
    if (owner_.padHasSample(part_))
    {
        content_->wave = std::make_unique<WaveView>(owner_, part_, content_->colour);
        content_->addAndMakeVisible(*content_->wave);
    }
    // Drop cards that ended up empty (a pad with no voice section, say).
    auto& s = content_->sections;
    s.erase(std::remove_if(s.begin(), s.end(), [](const auto& sec) { return sec->controls.empty(); }), s.end());
    resized();
    repaint();
}

juce::Rectangle<int> SoundPage::listArea() const
{
    return getLocalBounds().reduced(8, 12).removeFromLeft(kListWidth);
}

juce::Rectangle<int> SoundPage::listRow(int index) const
{
    const auto a = listArea().reduced(8, 12);
    // The session row sits apart at the bottom of the list.
    const bool session = listParts_[static_cast<std::size_t>(index)] == kSession;
    return { a.getX(), a.getY() + index * kRowHeight + (session ? 12 : 0), a.getWidth(), kRowHeight - 2 };
}

void SoundPage::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    g.setColour(zdui::kPanel);
    g.fillRoundedRectangle(listArea().toFloat(), 8.0f);
    for (int i = 0; i < static_cast<int>(listParts_.size()); ++i)
    {
        const int p = listParts_[static_cast<std::size_t>(i)];
        const auto r = listRow(i).toFloat();
        const bool on = p == part_;
        if (on)
        {
            g.setColour(zdui::kRaised);
            g.fillRoundedRectangle(r, 6.0f);
        }
        if (p == kSession)
        {
            g.setColour(zdui::kLine);
            g.fillRect(r.getX(), r.getY() - 7.0f, r.getWidth(), 1.0f);
        }
        const auto colour = p == kSession ? zdui::kDim : zdui::laneColour(p);
        g.setColour(colour);
        g.fillRoundedRectangle(r.withWidth(4.0f).reduced(0.0f, 6.0f).translated(6.0f, 0.0f), 2.0f);
        g.setColour(on ? zdui::kText : zdui::kText.withAlpha(0.75f));
        g.setFont(juce::FontOptions(zdui::kLabel, on ? juce::Font::bold : juce::Font::plain));
        g.drawText(p == kSession ? juce::String("Session") : owner_.padName(p), r.withTrimmedLeft(20.0f).toNearestInt(),
                   juce::Justification::centredLeft, true);
    }
}

void SoundPage::resized()
{
    auto area = getLocalBounds().reduced(8, 12);
    area.removeFromLeft(kListWidth + 8);
    viewport_.setBounds(area);
    const int width = area.getWidth() - 10;
    content_->setSize(width, juce::jmax(area.getHeight(), content_->layout(width)));
}

void SoundPage::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int>(listParts_.size()); ++i)
        if (listRow(i).contains(e.getPosition()))
        {
            const int p = listParts_[static_cast<std::size_t>(i)];
            showPart(p);
            if (onSelect && p != kSession)
                onSelect(p);
            return;
        }
}
