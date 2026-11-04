#include "PluginEditor.h"

#include "Theme.h"

MidiDragChip::MidiDragChip(ZeroDrumProcessor& owner, int lane, juce::String label)
    : owner_(owner), lane_(lane), label_(std::move(label))
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip(lane == kAudioLoop ? "Drag the loop into your DAW as audio (WAV, through the bus)"
               : lane < 0         ? "Drag the whole beat into your DAW as a MIDI clip"
                                  : "Drag the " + label_ + " lane into your DAW as a MIDI clip");
}

void MidiDragChip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(dragging_ ? juce::Colour(0xffff7a2f) : juce::Colour(lane_ == kAudioLoop ? 0xff1f8f84 : lane_ < 0 ? 0xff3a5bd9 : 0xff2a2d36));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(label_, getLocalBounds(), juce::Justification::centred);
}

void MidiDragChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ || e.getDistanceFromDragStart() < 6)
        return;
    const auto file = lane_ == kAudioLoop ? owner_.writeLoopWav() : owner_.writeMidiClip(lane_);
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
    addChildComponent(reverse_);
    reverse_.setTooltip("Play the sample backwards");
    choke_.addItem("No choke", 1);
    for (int g = 1; g <= ZeroDrumProcessor::kChokeGroups; ++g)
        choke_.addItem("Choke group " + juce::String(g), g + 1);
    choke_.setTooltip("Pads in the same group cut each other off, like an open and a closed hat");
    choke_.setWantsKeyboardFocus(false);
    addChildComponent(choke_);
    addChildComponent(load_);
    addChildComponent(clear_);
    load_.onClick = [this] { if (onLoad) onLoad(part_); };
    clear_.onClick = [this] { if (onClear) onClear(part_); };
    showPart(0);
}

void PartPanel::showPart(int part)
{
    part_ = part;
    const bool strip = part < ZeroDrumProcessor::kNumStripParts;
    const bool samplePad = ZeroDrumProcessor::isSamplePad(part);
    const bool loaded = owner_.padHasSample(part);
    const bool knobsOn = strip || loaded;
    const juce::String id = "p" + juce::String(part);
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        auto& k = knobs_[i];
        k.attachment.reset();
        if (knobsOn)
        {
            const char* suffix = strip ? kSuffixes[i] : ZeroDrumProcessor::kPadSuffixes[i];
            k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner_.state(), id + suffix, k.slider);
            // The attachment installs the parameter's own text function (full precision); show something readable.
            k.slider.textFromValueFunction = [](double v) {
                return std::abs(v) >= 100.0 ? juce::String(juce::roundToInt(v)) : juce::String(v, 2);
            };
            k.slider.updateText();
            k.caption.setText(strip ? kLabels[i] : kPadLabels[i], juce::dontSendNotification);
        }
        k.slider.setVisible(knobsOn);
        k.caption.setVisible(knobsOn);
        k.slider.setColour(juce::Slider::rotarySliderFillColourId, zdui::laneColour(part));
        k.slider.setColour(juce::Slider::thumbColourId, zdui::laneColour(part).brighter(0.4f));
    }
    crushAttachment_.reset();
    if (strip)
        crushAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), id + "Crush", crush_);
    crush_.setVisible(strip);
    reverseAttachment_.reset();
    if (samplePad && loaded)
        reverseAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), id + "SmpReverse", reverse_);
    reverse_.setVisible(samplePad && loaded);
    chokeAttachment_.reset();
    if (samplePad && loaded)
        chokeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner_.state(), id + "Choke", choke_);
    choke_.setVisible(samplePad && loaded);
    load_.setVisible(samplePad);
    load_.setButtonText(loaded ? "Replace..." : "Load sample...");
    clear_.setVisible(samplePad && loaded);
    resized();
    repaint();
}

void PartPanel::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kPanel);
    auto top = getLocalBounds().removeFromTop(36).reduced(12, 6);
    g.setColour(zdui::laneColour(part_));
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText(owner_.padName(part_), top, juce::Justification::centredLeft);
    if (ZeroDrumProcessor::isSamplePad(part_) && !owner_.padHasSample(part_))
    {
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText("Drop a WAV, AIFF or FLAC on this pad, or load one below. Its lane shows up in the step grid once it has a sound.",
                         getLocalBounds().reduced(16, 48).removeFromTop(80), juce::Justification::topLeft, 4);
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
    reverse_.setBounds(crush_.getBounds().withWidth(90));
    choke_.setBounds(crush_.getRight() - (cell + 8), crush_.getY(), cell + 8, 24);
    const bool loaded = owner_.padHasSample(part_);
    const int buttonsY = loaded ? area.getY() + 3 * (cell + 8) + 34 : area.getY() + 96;
    load_.setBounds(area.getX() + 8, buttonsY, cell + 20, 26);
    clear_.setBounds(area.getX() + cell + 36, buttonsY, cell - 44, 26);
}

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), owner_(owner), generic_(owner), mix_(owner), steps_(owner), pads_(owner), partPanel_(owner)
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
    kit_.onClick = [this] { showKitMenu(); };
    undo_.onClick = [this] { owner_.undo(); };
    redo_.onClick = [this] { owner_.redo(); };
    undo_.setTooltip("Undo the last pattern change (Ctrl+Z)");
    redo_.setTooltip("Redo (Ctrl+Shift+Z)");
    undo_.setWantsKeyboardFocus(false);
    redo_.setWantsKeyboardFocus(false);
    kit_.setTooltip("Save or load the whole kit: every sound, the bus and the pattern");

    dragLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    dragLabel_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(dragLabel_);
    chips_.push_back(std::make_unique<MidiDragChip>(owner_, -1, "Beat"));
    for (int i = 0; i < ZeroDrumProcessor::kNumStripParts; ++i)
        chips_.push_back(std::make_unique<MidiDragChip>(owner_, i, ZeroDrumProcessor::kStripNames[i]));
    chips_.push_back(std::make_unique<MidiDragChip>(owner_, MidiDragChip::kAudioLoop, "Audio"));
    for (auto& chip : chips_)
        addAndMakeVisible(*chip);

    summary_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    summary_.setFont(juce::FontOptions(13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &style_, &generate_, &mutate_, &fill_, &build_, &export_, &kit_, &undo_, &redo_, &summary_ })
        addAndMakeVisible(*c);

    // Pattern slots A..D and the chain that plays them in order.
    for (int i = 0; i < zd::kMaxChain; ++i)
    {
        auto& b = slots_[static_cast<std::size_t>(i)];
        b.setButtonText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
        b.setClickingTogglesState(false);
        b.setWantsKeyboardFocus(false);
        b.setColour(juce::TextButton::buttonOnColourId, zdui::kAccent);
        b.setTooltip("Edit pattern " + b.getButtonText() + " (key " + juce::String(i + 1)
                     + "). Right-click to copy, paste, clear or make a variation.");
        b.onClick = [this, i] { selectSlot(i); };
        b.onMenu = [this, i] { showSlotMenu(i); };
        addAndMakeVisible(b);
    }
    chain_.addItem("Loop slot", 1);
    chain_.addItem("Chain A-B", 2);
    chain_.addItem("Chain A-C", 3);
    chain_.addItem("Chain A-D", 4);
    chain_.setTooltip("Play the edited slot on its own, or chain slots A.. one after another");
    chain_.onChange = [this] {
        owner_.setChainCount(chain_.getSelectedId());
        refreshSlots();
    };
    chain_.setWantsKeyboardFocus(false);
    addAndMakeVisible(chain_);
    autoFill_.setClickingTogglesState(true);
    autoFill_.setWantsKeyboardFocus(false);
    autoFill_.setColour(juce::TextButton::buttonOnColourId, zdui::kAccent);
    autoFill_.setTooltip("Play a fill on the last slot of the chain (a lone slot: every 4th time through)");
    autoFill_.setToggleState(owner_.autoFill(), juce::dontSendNotification);
    autoFill_.onClick = [this] { owner_.setAutoFill(autoFill_.getToggleState()); };
    addAndMakeVisible(autoFill_);
    setWantsKeyboardFocus(true);
    refreshSlots();

    pads_.onSelect = [this](int part) { partPanel_.showPart(part); };
    pads_.onSampleChanged = [this](int part) {
        partPanel_.showPart(part);
        steps_.refreshLanes();
    };
    partPanel_.onLoad = [this](int part) { pads_.chooseSample(part); };
    partPanel_.onClear = [this](int part) {
        owner_.clearPadSample(part);
        pads_.repaint();
        partPanel_.showPart(part);
        steps_.refreshLanes();
    };
    padHint_.setText("Click to play, higher is louder. Shift: note repeat. Drag a pad out for its MIDI.", juce::dontSendNotification);
    for (const auto* rate : { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T" })
        repeatRate_.addItem(rate, repeatRate_.getNumItems() + 1);
    repeatAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner_.state(), "repeatRate", repeatRate_);
    repeatRate_.setTooltip("Note repeat rate (hold Shift on a pad)");
    repeatRate_.setWantsKeyboardFocus(false);
    repeatLabel_.setColour(juce::Label::textColourId, zdui::kDim);
    repeatLabel_.setFont(juce::FontOptions(12.0f));
    repeatLabel_.setJustificationType(juce::Justification::centredRight);
    fullLevelAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner_.state(), "padFullLevel", fullLevel_);
    fullLevel_.setTooltip("Pads and MIDI notes play at full velocity");
    fullLevel_.setWantsKeyboardFocus(false);
    tap_.setTooltip("Tap the tempo (used in the standalone and with Free Run on)");
    tap_.setWantsKeyboardFocus(false);
    tap_.onClick = [this] {
        const double bpm = owner_.tapTempo(juce::Time::getMillisecondCounterHiRes() * 0.001);
        tap_.setButtonText(bpm > 0.0 ? juce::String(bpm, 1) : juce::String("Tap"));
    };
    padHint_.setColour(juce::Label::textColourId, zdui::kDim);
    padHint_.setFont(juce::FontOptions(12.0f));
    padsPage_.onResize = [this] {
        // The grid on the left as a square, the selected part's controls on the right.
        auto page = padsPage_.getLocalBounds().reduced(8);
        auto bottom = page.removeFromBottom(24);
        repeatRate_.setBounds(bottom.removeFromRight(76).reduced(0, 1));
        repeatLabel_.setBounds(bottom.removeFromRight(56));
        bottom.removeFromRight(6);
        tap_.setBounds(bottom.removeFromRight(56).reduced(0, 1));
        bottom.removeFromRight(6);
        fullLevel_.setBounds(bottom.removeFromRight(92));
        padHint_.setBounds(bottom);
        auto panel = page.removeFromRight(juce::jmax(200, page.getWidth() / 3));
        partPanel_.setBounds(panel.reduced(4, 0));
        const int side = juce::jmin(page.getWidth(), page.getHeight());
        pads_.setBounds(page.withSizeKeepingCentre(side, side));
    };
    padsPage_.addAndMakeVisible(pads_);
    padsPage_.addAndMakeVisible(partPanel_);
    padsPage_.addAndMakeVisible(padHint_);
    padsPage_.addAndMakeVisible(repeatRate_);
    padsPage_.addAndMakeVisible(repeatLabel_);
    padsPage_.addAndMakeVisible(fullLevel_);
    padsPage_.addAndMakeVisible(tap_);

    tabs_.setTabBarDepth(30);
    tabs_.addTab("Beat", zdui::kBackground, &steps_, false);
    tabs_.addTab("Pads", zdui::kBackground, &padsPage_, false);
    tabs_.addTab("Mix", zdui::kBackground, &mix_, false);
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
    refreshSlots();
    undo_.setEnabled(owner_.canUndo());
    redo_.setEnabled(owner_.canRedo());
}

void ZeroDrumEditor::refreshSlots()
{
    const int edit = owner_.editSlot();
    const int count = owner_.chainCount();
    const int playing = owner_.isTransportRunning() ? owner_.playingSlot() : -1;
    for (int i = 0; i < zd::kMaxChain; ++i)
    {
        auto& b = slots_[static_cast<std::size_t>(i)];
        b.setToggleState(i == edit, juce::dontSendNotification);
        // Slots in the chain read brighter; the one playing gets a marker.
        const bool inChain = count > 1 && i < count;
        b.setColour(juce::TextButton::textColourOffId, inChain ? juce::Colours::white : zdui::kDim);
        const juce::String name = juce::String::charToString(static_cast<juce::juce_wchar>('A' + i));
        const int bars = owner_.slotBars(i);
        const juce::String label = bars > 1 ? name + juce::String::fromUTF8("\xc2\xb7") + juce::String(bars) : name;
        b.setButtonText(i == playing && count > 1 ? juce::String::fromUTF8("\xe2\x96\xb8") + label : label);
    }
    if (chain_.getSelectedId() != count)
        chain_.setSelectedId(count, juce::dontSendNotification);
    autoFill_.setToggleState(owner_.autoFill(), juce::dontSendNotification);
}

void ZeroDrumEditor::selectSlot(int slot)
{
    owner_.selectSlot(slot);
    steps_.refreshLanes();
    refreshSlots();
}

void ZeroDrumEditor::showSlotMenu(int slot)
{
    const juce::String name = juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot));
    juce::PopupMenu menu;
    menu.addSectionHeader("Pattern " + name);
    menu.addItem("Copy", [this, slot] { owner_.copySlot(slot); });
    menu.addItem("Paste", owner_.hasSlotCopy(), false, [this, slot] {
        owner_.pasteSlot(slot);
        steps_.refreshLanes();
        refreshSlots();
    });
    menu.addItem("Clear", [this, slot] {
        owner_.clearSlot(slot);
        steps_.refreshLanes();
        refreshSlots();
    });
    menu.addSeparator();
    menu.addItem("Variation into the next slot", slot == owner_.editSlot(), false, [this] {
        selectSlot(owner_.makeVariation());
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slots_[static_cast<std::size_t>(slot)]));
}

bool ZeroDrumEditor::keyPressed(const juce::KeyPress& key)
{
    // Works when the host passes keys to the plugin window (the standalone always does).
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
        return key.getModifiers().isShiftDown() ? owner_.redo() : owner_.undo();
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
        return owner_.redo();
    // 1..4 pick pattern slots A..D.
    if (!key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() >= '1' && key.getKeyCode() < '1' + zd::kMaxChain)
    {
        selectSlot(key.getKeyCode() - '1');
        return true;
    }
    return false;
}

void ZeroDrumEditor::showKitMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Save kit...", [this] { chooseKitFile(true); });
    menu.addItem("Load kit...", [this] { chooseKitFile(false); });
    // Kits saved in the kits folder load in one click.
    auto kits = kitsFolder().findChildFiles(juce::File::findFiles, false, juce::String("*") + ZeroDrumProcessor::kKitExtension);
    kits.sort();
    if (!kits.isEmpty())
    {
        menu.addSectionHeader("Your kits");
        for (int i = 0; i < juce::jmin(kits.size(), 40); ++i)
        {
            const auto file = kits[i];
            menu.addItem(file.getFileNameWithoutExtension(), [this, file] { loadKitFile(file); });
        }
    }
    menu.addSeparator();
    menu.addItem("Show kits folder", [] { kitsFolder().revealToUser(); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&kit_));
}

juce::File ZeroDrumEditor::kitsFolder()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Zero Drum").getChildFile("Kits");
    dir.createDirectory();
    return dir;
}

void ZeroDrumEditor::loadKitFile(const juce::File& file)
{
    if (owner_.loadKit(file))
    {
        steps_.refreshLanes();
        pads_.repaint();
        partPanel_.showPart(pads_.selectedPart());
        refreshSlots();
    }
}

void ZeroDrumEditor::chooseKitFile(bool save)
{
    const auto dir = kitsFolder();
    chooser_ = std::make_unique<juce::FileChooser>(save ? "Save the kit" : "Load a kit",
                                                   save ? dir.getChildFile(juce::String("My kit") + ZeroDrumProcessor::kKitExtension) : dir,
                                                   juce::String("*") + ZeroDrumProcessor::kKitExtension);
    const int mode = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                           : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<ZeroDrumEditor> self(this);
    chooser_->launchAsync(mode, [self, save](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (self == nullptr || file == juce::File {})
            return;
        if (save)
        {
            if (!file.hasFileExtension(ZeroDrumProcessor::kKitExtension))
                file = file.withFileExtension(ZeroDrumProcessor::kKitExtension);
            self->owner_.saveKit(file);
            return;
        }
        self->loadKitFile(file);
    });
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
    top.removeFromLeft(4);
    kit_.setBounds(top.removeFromLeft(44));

    auto drag = area.removeFromTop(30).reduced(8, 3);
    dragLabel_.setBounds(drag.removeFromLeft(72));
    for (auto& chip : chips_)
    {
        chip->setBounds(drag.removeFromLeft(chip.get() == chips_.front().get() ? 64 : 56));
        drag.removeFromLeft(4);
    }
    redo_.setBounds(drag.removeFromRight(52));
    drag.removeFromRight(4);
    undo_.setBounds(drag.removeFromRight(52));
    auto row = area.removeFromTop(26).reduced(8, 2);
    autoFill_.setBounds(row.removeFromRight(70));
    row.removeFromRight(4);
    chain_.setBounds(row.removeFromRight(104));
    row.removeFromRight(4);
    for (int i = zd::kMaxChain - 1; i >= 0; --i)
    {
        slots_[static_cast<std::size_t>(i)].setBounds(row.removeFromRight(38));
        row.removeFromRight(2);
    }
    summary_.setBounds(row.reduced(4, 0));
    tabs_.setBounds(area);
}
