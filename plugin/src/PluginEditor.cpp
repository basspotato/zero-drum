#include "PluginEditor.h"

#include "Theme.h"

MidiDragChip::MidiDragChip(ZeroDrumProcessor& owner, int lane, juce::String label)
    : owner_(owner), lane_(lane), label_(std::move(label))
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip(lane < 0 ? "Drag the whole beat into your DAW as a MIDI clip"
                        : "Drag the " + label_ + " lane into your DAW as a MIDI clip");
}

void MidiDragChip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(dragging_ ? juce::Colour(0xffff7a2f) : juce::Colour(lane_ < 0 ? 0xff3a5bd9 : 0xff2a2d36));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(label_, getLocalBounds(), juce::Justification::centred);
}

void MidiDragChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ || e.getDistanceFromDragStart() < 6)
        return;
    const auto file = owner_.writeMidiClip(lane_);
    if (!file.existsAsFile())
        return;
    dragging_ = true;
    repaint();
    juce::Component::SafePointer<MidiDragChip> self(this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this, [self] {
        if (self != nullptr)
        {
            self->dragging_ = false;
            self->repaint();
        }
    });
}

PartPanel::PartPanel(ZeroDrumProcessor& owner) : owner_(owner)
{
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        auto& k = knobs_[i];
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        k.slider.setNumDecimalPlacesToDisplay(2);
        k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setColour(juce::Slider::rotarySliderFillColourId, zdui::kAccent);
        k.caption.setText(kLabels[i], juce::dontSendNotification);
        k.caption.setJustificationType(juce::Justification::centred);
        k.caption.setColour(juce::Label::textColourId, zdui::kDim);
        k.caption.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(k.slider);
        addAndMakeVisible(k.caption);
    }
    addAndMakeVisible(crush_);
    showPart(0);
}

void PartPanel::showPart(int part)
{
    part_ = part;
    const bool hasControls = part < ZeroDrumProcessor::kNumStripParts;
    const juce::String id = "p" + juce::String(part);
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        auto& k = knobs_[i];
        k.attachment.reset();
        if (hasControls)
        {
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner_.state(), id + kSuffixes[i], k.slider);
            // The attachment installs the parameter's own text function (full precision); show something readable.
            k.slider.textFromValueFunction = [](double v) {
                return std::abs(v) >= 100.0 ? juce::String(juce::roundToInt(v)) : juce::String(v, 2);
            };
            k.slider.updateText();
        }
        k.slider.setVisible(hasControls);
        k.caption.setVisible(hasControls);
        k.slider.setColour(juce::Slider::rotarySliderFillColourId, zdui::laneColour(part));
        k.slider.setColour(juce::Slider::thumbColourId, zdui::laneColour(part).brighter(0.4f));
    }
    crushAttachment_.reset();
    if (hasControls)
        crushAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), id + "Crush", crush_);
    crush_.setVisible(hasControls);
    repaint();
}

void PartPanel::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kPanel);
    auto top = getLocalBounds().removeFromTop(36).reduced(12, 6);
    g.setColour(zdui::laneColour(part_));
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    const bool hasControls = part_ < ZeroDrumProcessor::kNumStripParts;
    g.drawText(hasControls ? juce::String(ZeroDrumProcessor::kStripNames[part_]) : "Part " + juce::String(part_ + 1), top, juce::Justification::centredLeft);
    if (!hasControls)
    {
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText("No sound on this pad yet. Parts 6 to 16 get sounds with sample loading.", getLocalBounds().reduced(16, 48),
                         juce::Justification::topLeft, 3);
    }
}

void PartPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(32);
    const int cell = juce::jmin(area.getWidth() / 2, 96);
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        auto r = juce::Rectangle<int>(area.getX() + static_cast<int>(i % 2) * cell, area.getY() + static_cast<int>(i / 2) * (cell + 8), cell, cell + 8);
        knobs_[i].caption.setBounds(r.removeFromTop(16));
        knobs_[i].slider.setBounds(r.reduced(4));
    }
    crush_.setBounds(area.getX() + 8, area.getY() + 3 * (cell + 8) + 4, cell * 2 - 16, 24);
}

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), owner_(owner), generic_(owner), steps_(owner), pads_(owner), partPanel_(owner)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(owner.state().getParameter("genStyle")))
        style_.addItemList(param->choices, 1);
    styleAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.state(), "genStyle", style_);

    generate_.onClick = [this] { owner_.generateBeat(); };
    mutate_.onClick = [this] { owner_.mutateBeat(); };
    fill_.onClick = [this] { owner_.addFillToBeat(); };
    build_.onClick = [this] { owner_.makeBuildBeat(); };
    build_.setTooltip("A 4-bar build-up into the drop: rising snare roll, kick drops out, then a breath");
    export_.onClick = [this] { chooseExportFolder(); };
    export_.setTooltip("Save the loop as a mix WAV, a WAV per part, MIDI per part and the pattern");

    dragLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    dragLabel_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(dragLabel_);
    chips_.push_back(std::make_unique<MidiDragChip>(owner_, -1, "Beat"));
    for (int i = 0; i < ZeroDrumProcessor::kNumStripParts; ++i)
        chips_.push_back(std::make_unique<MidiDragChip>(owner_, i, ZeroDrumProcessor::kStripNames[i]));
    for (auto& chip : chips_)
        addAndMakeVisible(*chip);

    summary_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    summary_.setFont(juce::FontOptions(13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &style_, &generate_, &mutate_, &fill_, &build_, &export_, &summary_ })
        addAndMakeVisible(*c);

    pads_.onSelect = [this](int part) { partPanel_.showPart(part); };
    padHint_.setText("Click to play, higher is louder. Shift: 16th repeat. Drag a pad out for its MIDI.", juce::dontSendNotification);
    padHint_.setColour(juce::Label::textColourId, zdui::kDim);
    padHint_.setFont(juce::FontOptions(12.0f));
    padsPage_.onResize = [this] {
        // The grid on the left as a square, the selected part's controls on the right.
        auto page = padsPage_.getLocalBounds().reduced(8);
        padHint_.setBounds(page.removeFromBottom(22));
        auto panel = page.removeFromRight(juce::jmax(200, page.getWidth() / 3));
        partPanel_.setBounds(panel.reduced(4, 0));
        const int side = juce::jmin(page.getWidth(), page.getHeight());
        pads_.setBounds(page.withSizeKeepingCentre(side, side));
    };
    padsPage_.addAndMakeVisible(pads_);
    padsPage_.addAndMakeVisible(partPanel_);
    padsPage_.addAndMakeVisible(padHint_);

    tabs_.setTabBarDepth(30);
    tabs_.addTab("Beat", zdui::kBackground, &steps_, false);
    tabs_.addTab("Pads", zdui::kBackground, &padsPage_, false);
    tabs_.addTab("Sound", zdui::kBackground, &generic_, false);
    addAndMakeVisible(tabs_);

    setResizable(true, true);
    setSize(720, 760);
    timerCallback();
    startTimerHz(4);
}

void ZeroDrumEditor::timerCallback()
{
    summary_.setText(owner_.describeBeat(), juce::dontSendNotification);
}

void ZeroDrumEditor::chooseExportFolder()
{
    if (owner_.isExporting())
        return;
    const auto start = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Export the loop into a folder", start);
    const auto mode = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    juce::Component::SafePointer<ZeroDrumEditor> self(this);
    chooser_->launchAsync(mode, [self](const juce::FileChooser& fc) {
        if (self == nullptr)
            return;
        const auto parent = fc.getResult();
        if (parent == juce::File {})
            return;
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S");
        const auto dir = parent.getChildFile("Zero Drum " + stamp);
        self->export_.setEnabled(false);
        self->export_.setButtonText("Exporting...");
        self->owner_.exportLoopAsync(dir, [self, dir](const zd::exporting::ExportResult& result) {
            if (self == nullptr)
                return;
            self->export_.setEnabled(true);
            self->export_.setButtonText("Export...");
            if (result.ok)
                dir.revealToUser();
            else
                juce::AlertWindow::showAsync(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon,
                                                                                      "Export failed", juce::String(result.error)),
                                             nullptr);
        });
    });
}

void ZeroDrumEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15161a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("ZERO DRUM", getLocalBounds().removeFromTop(40).reduced(12, 0), juce::Justification::centredLeft);
}

void ZeroDrumEditor::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(40).reduced(8, 6);
    top.removeFromLeft(120); // title
    style_.setBounds(top.removeFromLeft(150));
    top.removeFromLeft(8);
    generate_.setBounds(top.removeFromLeft(90));
    top.removeFromLeft(4);
    mutate_.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(4);
    fill_.setBounds(top.removeFromLeft(50));
    top.removeFromLeft(4);
    build_.setBounds(top.removeFromLeft(56));
    top.removeFromLeft(8);
    export_.setBounds(top.removeFromLeft(80));

    auto drag = area.removeFromTop(30).reduced(8, 3);
    dragLabel_.setBounds(drag.removeFromLeft(72));
    for (auto& chip : chips_)
    {
        chip->setBounds(drag.removeFromLeft(chip.get() == chips_.front().get() ? 64 : 56));
        drag.removeFromLeft(4);
    }
    summary_.setBounds(area.removeFromTop(24).reduced(12, 0));
    tabs_.setBounds(area);
}
