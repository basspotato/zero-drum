#include "PluginEditor.h"

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

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), owner_(owner), generic_(owner)
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

    for (auto* c : std::initializer_list<juce::Component*> { &style_, &generate_, &mutate_, &fill_, &build_, &export_, &summary_, &generic_ })
        addAndMakeVisible(*c);

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
    generic_.setBounds(area);
}
