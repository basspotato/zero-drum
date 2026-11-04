#include "PluginEditor.h"

#include "Theme.h"

MidiDragChip::MidiDragChip(ZeroDrumProcessor& owner, int lane, juce::String label)
    : owner_(owner), lane_(lane), label_(std::move(label))
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setRepaintsOnMouseActivity(true);
    setTooltip(lane == kAudioLoop ? "Drag the loop into your DAW as audio (WAV, through the bus)"
               : lane < 0         ? "Drag the whole beat into your DAW as a MIDI clip"
                                  : "Drag the " + label_ + " lane into your DAW as a MIDI clip (Alt-drag: as audio)");
}

void MidiDragChip::paint(juce::Graphics& g)
{
    // Flat chips with a colour dot: the lane's colour, white for the whole beat, teal for audio.
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool over = isMouseOver();
    g.setColour(dragging_ ? zdui::kAccent : (over ? zdui::kRaised.brighter(0.08f) : zdui::kRaised));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(over ? zdui::kLine.brighter(0.3f) : zdui::kLine);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    const auto dotColour = lane_ == kAudioLoop ? zdui::kGood : lane_ < 0 ? zdui::kText : zdui::laneColour(lane_);
    auto content = getLocalBounds().reduced(7, 0);
    const auto dot = content.removeFromLeft(7).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
    g.setColour(dotColour);
    if (lane_ == kAudioLoop)
        g.drawEllipse(dot.expanded(0.5f), 1.5f); // a ring for audio, a dot for MIDI
    else
        g.fillEllipse(dot);
    g.setColour(dragging_ ? juce::Colours::black : zdui::kText);
    g.setFont(juce::FontOptions(zdui::kLabel - 1.0f, juce::Font::bold));
    g.drawText(label_, content.withTrimmedLeft(3), juce::Justification::centred);
}

void MidiDragChip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ || e.getDistanceFromDragStart() < 6)
        return;
    // Alt-drag a part chip for its audio instead of its MIDI.
    const auto file = lane_ == kAudioLoop ? owner_.writeLoopWav()
                      : e.mods.isAltDown() ? owner_.writeLoopWav(lane_)
                                           : owner_.writeMidiClip(lane_);
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
        zdui::styleKnob(k.slider, k.caption, kLabels[i], zdui::kAccent);
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
        const int padIndex = kPadKnobMap[i];
        const bool used = knobsOn && (strip ? static_cast<int>(i) < kStripKnobs : padIndex >= 0);
        if (used)
        {
            const juce::String suffix = strip ? juce::String(kSuffixes[i]) : juce::String(ZeroDrumProcessor::kPadSuffixes[padIndex]);
            k.attachment = zdui::attachKnob(owner_.state(), id + suffix, k.slider);
            k.caption.setText(strip ? kLabels[i] : kPadLabels[i], juce::dontSendNotification);
        }
        k.slider.setVisible(used);
        k.caption.setVisible(used);
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
    g.setColour(zdui::kPanel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    // Title: a colour bar, the part's name and what it is.
    auto top = getLocalBounds().removeFromTop(52).reduced(16, 10);
    g.setColour(zdui::laneColour(part_));
    g.fillRoundedRectangle(top.removeFromLeft(4).toFloat(), 2.0f);
    top.removeFromLeft(10);
    g.setColour(zdui::kText);
    g.setFont(juce::FontOptions(zdui::kHeading, juce::Font::bold));
    g.drawText(owner_.padName(part_), top.removeFromTop(20), juce::Justification::centredLeft);
    g.setColour(zdui::kDim);
    g.setFont(juce::FontOptions(zdui::kCaption));
    const juce::String what = part_ < ZeroDrumProcessor::kNumStripParts ? "synth voice" : owner_.padHasSample(part_) ? "sample" : "empty";
    g.drawText("Pad " + juce::String(part_ + 1) + "  /  " + what, top, juce::Justification::centredLeft);
    if (ZeroDrumProcessor::isSamplePad(part_) && !owner_.padHasSample(part_))
    {
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(zdui::kLabel));
        g.drawFittedText("Drop a WAV, AIFF or FLAC on this pad, or load one below. Its lane shows up in the step grid once it has a sound.",
                         getLocalBounds().reduced(16, 0).withTrimmedTop(60).removeFromTop(48), juce::Justification::topLeft, 3);
    }
}

void PartPanel::resized()
{
    // Knobs in a 3-column grid under the title, then the switches, then the sample buttons.
    auto area = getLocalBounds().reduced(12, 0);
    area.removeFromTop(60);
    const int cols = 3;
    const int cell = juce::jmin(area.getWidth() / cols, 88);
    const int rowH = cell + 26;
    int shown = 0;
    for (auto& k : knobs_)
    {
        if (!k.slider.isVisible())
            continue;
        auto r = juce::Rectangle<int>(area.getX() + (shown % cols) * cell, area.getY() + (shown / cols) * rowH, cell, rowH);
        k.caption.setBounds(r.removeFromTop(14));
        k.slider.setBounds(r.reduced(4, 0).withTrimmedBottom(10));
        ++shown;
    }
    const int rows = (shown + cols - 1) / cols;
    auto below = area.withTrimmedTop(rows * rowH + 12);
    auto line = below.removeFromTop(28);
    crush_.setBounds(line.withWidth(120));
    reverse_.setBounds(line.removeFromLeft(110));
    line.removeFromLeft(8);
    choke_.setBounds(line.removeFromLeft(juce::jmin(150, line.getWidth())));
    const bool loaded = owner_.padHasSample(part_);
    auto buttons = loaded ? below.withTrimmedTop(12).removeFromTop(28) : area.withTrimmedTop(64).removeFromTop(28);
    load_.setBounds(buttons.removeFromLeft(130));
    buttons.removeFromLeft(8);
    clear_.setBounds(buttons.removeFromLeft(72));
}

ZeroDrumEditor::ZeroDrumEditor(ZeroDrumProcessor& owner)
    : AudioProcessorEditor(owner), owner_(owner), sound_(owner), mix_(owner), genStrip_(owner), steps_(owner), pads_(owner), partPanel_(owner)
{
    setLookAndFeel(&look_.get());
    generate_.setColour(juce::TextButton::buttonColourId, zdui::kAccent);
    generate_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    generate_.setTooltip("A new beat in the chosen style (locked lanes stay)");
    mutate_.setTooltip("Change the beat a little");
    fill_.setTooltip("Busier last quarter-bar on snare and perc");
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

    for (auto* caption : { &dragLabel_, &patternLabel_ })
    {
        caption->setColour(juce::Label::textColourId, zdui::kDim);
        caption->setFont(juce::FontOptions(zdui::kCaption, juce::Font::bold));
        addAndMakeVisible(*caption);
    }
    dragLabel_.setJustificationType(juce::Justification::centredRight);
    hints_.setText("Space play   1-4 slots   Ctrl+Z undo   Shift-drag pitch   Alt-drag chance   Ctrl-drag timing",
                   juce::dontSendNotification);
    hints_.setColour(juce::Label::textColourId, zdui::kDim);
    hints_.setFont(juce::FontOptions(zdui::kCaption));
    hints_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hints_);
    addAndMakeVisible(meter_);
    chips_.push_back(std::make_unique<MidiDragChip>(owner_, -1, "Beat"));
    for (int i = 0; i < ZeroDrumProcessor::kNumStripParts; ++i)
        chips_.push_back(std::make_unique<MidiDragChip>(owner_, i, ZeroDrumProcessor::kStripNames[i]));
    chips_.push_back(std::make_unique<MidiDragChip>(owner_, MidiDragChip::kAudioLoop, "Audio"));
    for (auto& chip : chips_)
        addAndMakeVisible(*chip);

    summary_.setColour(juce::Label::textColourId, zdui::kDim);
    summary_.setFont(juce::FontOptions(zdui::kCaption));

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

    // One selected part everywhere: the pads, the lane headers, the part panel and the Sound tab.
    pads_.onSelect = [this](int part) {
        partPanel_.showPart(part);
        sound_.showPart(part);
        steps_.setSelectedPart(part);
    };
    sound_.onSelect = [this](int part) {
        pads_.select(part);
        partPanel_.showPart(part);
        steps_.setSelectedPart(part);
    };
    steps_.onSelectPart = [this](int part) {
        pads_.select(part);
        partPanel_.showPart(part);
        sound_.showPart(part);
    };
    pads_.onSampleChanged = [this](int part) {
        partPanel_.showPart(part);
        sound_.showPart(part);
        steps_.refreshLanes();
    };
    partPanel_.onLoad = [this](int part) { pads_.chooseSample(part); };
    partPanel_.onClear = [this](int part) {
        owner_.clearPadSample(part);
        pads_.repaint();
        partPanel_.showPart(part);
        sound_.showPart(part);
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

    tabs_.setTabBarDepth(36);
    tabs_.setOutline(0);
    beatPage_.onResize = [this] {
        auto page = beatPage_.getLocalBounds();
        genStrip_.setBounds(page.removeFromTop(GenStrip::kHeight).reduced(8, 0));
        steps_.setBounds(page);
    };
    beatPage_.addAndMakeVisible(genStrip_);
    beatPage_.addAndMakeVisible(steps_);
    tabs_.addTab("Beat", zdui::kBackground, &beatPage_, false);
    tabs_.addTab("Pads", zdui::kBackground, &padsPage_, false);
    tabs_.addTab("Mix", zdui::kBackground, &mix_, false);
    tabs_.addTab("Sound", zdui::kBackground, &sound_, false);
    addAndMakeVisible(tabs_);

    addAndMakeVisible(transport_);
    const float zooms[] { 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };
    for (const float z : zooms)
        zoomBox_.addItem(juce::String(juce::roundToInt(z * 100.0f)) + " %", juce::roundToInt(z * 100.0f));
    zoomBox_.setTooltip("Interface size (Ctrl + and Ctrl -)");
    zoomBox_.setWantsKeyboardFocus(false);
    zoomBox_.onChange = [this] { setZoom(static_cast<float>(zoomBox_.getSelectedId()) / 100.0f); };
    addAndMakeVisible(zoomBox_);

    setResizable(true, true);
    zoom_ = owner_.uiZoom();
    zoomBox_.setSelectedId(juce::roundToInt(zoom_ * 100.0f), juce::dontSendNotification);
    setResizeLimits(juce::roundToInt(880 * zoom_), juce::roundToInt(660 * zoom_), juce::roundToInt(1800 * zoom_), juce::roundToInt(1400 * zoom_));
    setSize(juce::roundToInt(900 * zoom_), juce::roundToInt(800 * zoom_));
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
    // Ctrl + / Ctrl - / Ctrl 0 zoom the interface.
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == '=' || key.getKeyCode() == '+' || key.getKeyCode() == '-' || key.getKeyCode() == '0'))
    {
        const float zooms[] { 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f };
        int index = 2;
        for (int i = 0; i < 6; ++i)
            if (zooms[i] <= zoom_ + 0.001f)
                index = i;
        if (key.getKeyCode() == '0')
            index = 2;
        else
            index = juce::jlimit(0, 5, index + (key.getKeyCode() == '-' ? -1 : 1));
        setZoom(zooms[index]);
        return true;
    }
    // 1..4 pick pattern slots A..D.
    // Space starts and stops the standalone (its free-running clock); in a DAW the host owns play.
    if (key == juce::KeyPress::spaceKey && owner_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        if (auto* run = owner_.state().getParameter("freeRun"))
            run->setValueNotifyingHost(run->getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    }
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
        sound_.showPart(sound_.shownPart());
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

ZeroDrumEditor::~ZeroDrumEditor()
{
    setLookAndFeel(nullptr);
}

void OutputMeter::timerCallback()
{
    // Instant rise, smooth fall; the peak line holds for a second.
    const float peak = owner_.takeOutputPeak();
    level_ = peak > level_ ? peak : level_ * 0.86f;
    if (peak >= hold_)
    {
        hold_ = peak;
        holdFrames_ = 30;
    }
    else if (--holdFrames_ <= 0)
        hold_ *= 0.9f;
    repaint();
}

void OutputMeter::paint(juce::Graphics& g)
{
    // -48..0 dBFS, bottom to top.
    const auto r = getLocalBounds().toFloat();
    g.setColour(zdui::kCellOff);
    g.fillRoundedRectangle(r, 3.0f);
    auto toY = [&r](float gain) {
        const float db = juce::Decibels::gainToDecibels(gain, -48.0f);
        return r.getBottom() - r.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
    };
    const float y = toY(level_);
    const float db = juce::Decibels::gainToDecibels(level_, -100.0f);
    g.setColour(db >= -0.1f ? zdui::kHot : db > -6.0f ? zdui::kWarn : zdui::kGood);
    g.fillRoundedRectangle(r.withTop(y), 3.0f);
    if (hold_ > 1.0e-4f)
    {
        g.setColour(zdui::kText.withAlpha(0.8f));
        g.fillRect(r.getX(), toY(hold_) - 1.0f, r.getWidth(), 2.0f);
    }
}

juce::Rectangle<int> ZeroDrumEditor::unzoomedBounds() const
{
    return { juce::roundToInt(static_cast<float>(getWidth()) / zoom_), juce::roundToInt(static_cast<float>(getHeight()) / zoom_) };
}

void ZeroDrumEditor::setZoom(float zoom)
{
    zoom = juce::jlimit(0.75f, 1.5f, zoom);
    if (juce::approximatelyEqual(zoom, zoom_))
        return;
    const auto content = unzoomedBounds();
    zoom_ = zoom;
    owner_.setUiZoom(zoom);
    zoomBox_.setSelectedId(juce::roundToInt(zoom * 100.0f), juce::dontSendNotification);
    setResizeLimits(juce::roundToInt(880 * zoom), juce::roundToInt(660 * zoom), juce::roundToInt(1800 * zoom), juce::roundToInt(1400 * zoom));
    setSize(juce::roundToInt(static_cast<float>(content.getWidth()) * zoom), juce::roundToInt(static_cast<float>(content.getHeight()) * zoom));
    resized();
    repaint();
}

void ZeroDrumEditor::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    g.addTransform(juce::AffineTransform::scale(zoom_));
    // Header band (generator and pattern rows) and the status line.
    const auto header = unzoomedBounds().removeFromTop(96);
    g.setColour(zdui::kPanel);
    g.fillRect(header);
    g.setColour(zdui::kLine);
    g.fillRect(header.getX(), header.getY() + 56, header.getWidth(), 1);
    g.fillRect(header.getX(), header.getBottom() - 1, header.getWidth(), 1);
    const auto footer = unzoomedBounds().removeFromBottom(28);
    g.setColour(zdui::kPanel);
    g.fillRect(footer);
    g.setColour(zdui::kLine);
    g.fillRect(footer.getX(), footer.getY(), footer.getWidth(), 1);

    // Wordmark: ZERO in the accent, DRUM in white, and a zero-slash mark.
    auto mark = juce::Rectangle<float>(16.0f, 16.0f, 24.0f, 24.0f);
    g.setColour(zdui::kAccent);
    g.drawEllipse(mark.reduced(2.0f), 3.0f);
    g.drawLine(mark.getX() + 6.0f, mark.getBottom() - 6.0f, mark.getRight() - 6.0f, mark.getY() + 6.0f, 3.0f);
    g.setFont(juce::FontOptions(zdui::kHeading + 1.0f, juce::Font::bold));
    g.drawText("ZERO", juce::Rectangle<int>(48, 14, 60, 28), juce::Justification::centredLeft);
    g.setColour(zdui::kText);
    g.drawText("DRUM", juce::Rectangle<int>(103, 14, 60, 28), juce::Justification::centredLeft);
}

void ZeroDrumEditor::resized()
{
    constexpr int pad = 16, gap = 8, small = 4;
    // Everything is laid out at 100 % and drawn through one scale, so the zoom changes nothing else.
    auto area = unzoomedBounds();

    // Row 1: wordmark | style, Generate, Mutate, Fill, Build | undo, redo | kit, export | meter.
    auto top = area.removeFromTop(56).reduced(pad, 12);
    top.removeFromLeft(156); // wordmark
    meter_.setBounds(top.removeFromRight(8));
    top.removeFromRight(12);
    export_.setBounds(top.removeFromRight(76));
    top.removeFromRight(small);
    kit_.setBounds(top.removeFromRight(52));
    top.removeFromRight(12);
    redo_.setBounds(top.removeFromRight(52));
    top.removeFromRight(small);
    undo_.setBounds(top.removeFromRight(52));
    style_.setBounds(top.removeFromLeft(128));
    top.removeFromLeft(gap);
    generate_.setBounds(top.removeFromLeft(96));
    top.removeFromLeft(gap);
    mutate_.setBounds(top.removeFromLeft(68));
    top.removeFromLeft(small);
    fill_.setBounds(top.removeFromLeft(52));
    top.removeFromLeft(small);
    build_.setBounds(top.removeFromLeft(60));

    // Row 2: PATTERN slots, chain, auto fill | DRAG chips.
    auto row = area.removeFromTop(40).reduced(pad, 6);
    patternLabel_.setBounds(row.removeFromLeft(64));
    for (auto& slot : slots_)
    {
        slot.setBounds(row.removeFromLeft(34));
        row.removeFromLeft(2);
    }
    row.removeFromLeft(gap);
    chain_.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(gap);
    autoFill_.setBounds(row.removeFromLeft(72));
    for (auto it = chips_.rbegin(); it != chips_.rend(); ++it)
    {
        const auto& chip = *it;
        const bool wide = chip.get() == chips_.front().get() || chip.get() == chips_.back().get();
        chip->setBounds(row.removeFromRight(chip.get() == chips_.back().get() ? 60 : wide ? 56 : 58));
        row.removeFromRight(small);
    }
    dragLabel_.setBounds(row.removeFromRight(36));

    // Status line, then the tabs fill the rest.
    auto footer = area.removeFromBottom(28).reduced(pad, 0);
    summary_.setBounds(footer.removeFromLeft(280));
    hints_.setBounds(footer);
    tabs_.setBounds(area.reduced(8, 0));
    // Over the tab bar's empty right end: the transport readout and the zoom.
    auto bar = area.reduced(16, 0).removeFromTop(36).reduced(0, 5);
    zoomBox_.setBounds(bar.removeFromRight(76));
    bar.removeFromRight(gap);
    transport_.setBounds(bar.removeFromRight(196));

    const auto scale = juce::AffineTransform::scale(zoom_);
    for (auto* child : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*>(child) == nullptr && dynamic_cast<juce::ResizableBorderComponent*>(child) == nullptr
            && dynamic_cast<juce::TooltipWindow*>(child) == nullptr)
            child->setTransform(scale);
}

TransportView::TransportView(ZeroDrumProcessor& owner) : owner_(owner)
{
    if (owner.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        setTooltip("Click or press Space to start and stop");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else
        setTooltip("Follows the host's transport");
    startTimerHz(15);
}

void TransportView::timerCallback()
{
    const bool playing = owner_.isTransportRunning();
    const double bpm = owner_.currentBpm();
    const double beats = playing ? std::floor(owner_.playheadBeats()) : -1.0;
    if (playing != playing_ || !juce::approximatelyEqual(bpm, bpm_) || !juce::approximatelyEqual(beats, beats_))
    {
        playing_ = playing;
        bpm_ = bpm;
        beats_ = beats;
        repaint();
    }
}

void TransportView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(zdui::kPanel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(playing_ ? zdui::kGood.withAlpha(0.6f) : zdui::kLine);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    auto inner = r.reduced(10.0f, 0.0f);
    // Play triangle or stop square.
    const auto icon = inner.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 10.0f);
    g.setColour(playing_ ? zdui::kGood : zdui::kDim);
    if (playing_)
    {
        juce::Path p;
        p.addTriangle(icon.getX(), icon.getY(), icon.getRight(), icon.getCentreY(), icon.getX(), icon.getBottom());
        g.fillPath(p);
    }
    else
        g.fillRoundedRectangle(icon.reduced(1.0f), 1.5f);
    inner.removeFromLeft(8.0f);
    g.setFont(juce::FontOptions(zdui::kLabel, juce::Font::bold));
    g.setColour(zdui::kText);
    const auto tempo = inner.removeFromLeft(78.0f);
    g.drawText(juce::String(bpm_, 1) + " BPM", tempo, juce::Justification::centredLeft);
    g.setColour(playing_ ? zdui::kText : zdui::kDim);
    g.setFont(juce::FontOptions(zdui::kLabel));
    juce::String where = "Stopped";
    if (playing_ && beats_ >= 0.0)
    {
        const auto beat = static_cast<juce::int64>(beats_);
        where = "Bar " + juce::String(beat / 4 + 1) + "." + juce::String(beat % 4 + 1);
    }
    g.drawText(where, inner, juce::Justification::centredRight);
}

void TransportView::mouseUp(const juce::MouseEvent& e)
{
    if (owner_.wrapperType != juce::AudioProcessor::wrapperType_Standalone || !getLocalBounds().contains(e.getPosition()))
        return;
    if (auto* run = owner_.state().getParameter("freeRun"))
        run->setValueNotifyingHost(run->getValue() > 0.5f ? 0.0f : 1.0f);
}
