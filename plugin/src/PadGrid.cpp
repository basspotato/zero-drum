#include "PadGrid.h"

#include "PartName.h"
#include "Theme.h"

PadGrid::PadGrid(ZeroDrumProcessor& owner) : owner_(owner)
{
    for (int i = 0; i < kPads; ++i)
        lastHits_[static_cast<std::size_t>(i)] = owner_.partHits(i);
    startTimerHz(30);
}

PadGrid::~PadGrid()
{
    if (repeating_)
        owner_.setNoteRepeat(-1, 1.0f);
}

juce::Rectangle<float> PadGrid::padBounds(int part) const
{
    const auto area = getLocalBounds().toFloat().reduced(6.0f);
    const float w = area.getWidth() / 4.0f, h = area.getHeight() / 4.0f;
    const int column = part % 4, rowFromTop = 3 - part / 4;
    return { area.getX() + column * w, area.getY() + rowFromTop * h, w, h };
}

int PadGrid::padAt(juce::Point<float> p) const
{
    for (int i = 0; i < kPads; ++i)
        if (padBounds(i).contains(p))
            return i;
    return -1;
}

float PadGrid::velocityAt(int part, juce::Point<float> p) const
{
    if (owner_.fullLevelPads())
        return 1.0f;
    const auto r = padBounds(part).reduced(4.0f);
    // Top of the pad is full velocity, the bottom edge soft.
    return juce::jlimit(0.15f, 1.0f, 1.0f - 0.85f * (p.y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
}

void PadGrid::paint(juce::Graphics& g)
{
    g.fillAll(zdui::kBackground);
    for (int i = 0; i < kPads; ++i)
    {
        const auto r = padBounds(i).reduced(4.0f);
        const bool emptyPad = ZeroDrumProcessor::isSamplePad(i) && !owner_.padHasSample(i);
        const bool hovered = i == hover_ || i == dropTarget_;
        const bool audible = ((owner_.audibleMask() >> i) & 1u) != 0;
        const auto inner = r.reduced(10.0f, 8.0f).toNearestInt();
        const float glow = glow_[static_cast<std::size_t>(i)];

        if (emptyPad)
        {
            // An open slot: a dashed outline and a plus, inviting a drop.
            g.setColour(zdui::kBackground.brighter(hovered ? 0.07f : 0.03f).interpolatedWith(zdui::kText, 0.25f * glow));
            g.fillRoundedRectangle(r, 8.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle(r.reduced(0.5f), 8.0f);
            const float dashes[] { 5.0f, 4.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(i == selected_ ? zdui::kText : zdui::kLine.brighter(hovered ? 0.5f : 0.15f));
            g.fillPath(dashed);
            const auto c = r.getCentre();
            const float arm = juce::jmin(r.getWidth(), r.getHeight()) * 0.09f;
            g.setColour(hovered ? zdui::kText : zdui::kDim.withAlpha(0.6f));
            g.fillRoundedRectangle(c.x - arm, c.y - 1.0f, arm * 2.0f, 2.0f, 1.0f);
            g.fillRoundedRectangle(c.x - 1.0f, c.y - arm, 2.0f, arm * 2.0f, 1.0f);
            g.setFont(juce::FontOptions(zdui::kCaption));
            g.drawText(i == dropTarget_ ? "drop to load" : hovered ? "drop or right-click" : "", inner.withTrimmedBottom(14),
                       juce::Justification::centredBottom);
            g.setColour(zdui::kDim);
            g.setFont(juce::FontOptions(zdui::kLabel, juce::Font::plain));
            g.drawText(owner_.padName(i), inner, juce::Justification::topLeft);
            g.setFont(juce::FontOptions(zdui::kCaption));
            g.drawText(juce::String(i + 1), inner, juce::Justification::bottomRight);
            continue;
        }

        const auto colour = zdui::laneColour(i);
        const float base = audible ? 0.18f : 0.06f;
        g.setColour(zdui::kCellOff.interpolatedWith(colour, base + (1.0f - base) * glow).brighter(hovered ? 0.08f : 0.0f));
        g.fillRoundedRectangle(r, 8.0f);
        // Velocity cue: a soft band that gets brighter towards the top, where hits are louder.
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(audible ? 0.16f : 0.05f), r.getX(), r.getY(), colour.withAlpha(0.0f),
                                               r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(i == selected_ ? zdui::kText : colour.withAlpha(hovered ? 0.9f : audible ? 0.55f : 0.25f));
        g.drawRoundedRectangle(r, 8.0f, i == selected_ ? 2.0f : 1.0f);
        if (i == dropTarget_)
        {
            g.setColour(zdui::kText.withAlpha(0.2f));
            g.fillRoundedRectangle(r, 8.0f);
        }

        // A loaded sample shows its shape along the bottom.
        if (owner_.padHasSample(i))
        {
            const auto wave = r.reduced(10.0f, 0.0f).withTrimmedBottom(22.0f).removeFromBottom(r.getHeight() * 0.3f);
            const auto& peaks = padPeaks(i, juce::roundToInt(wave.getWidth() / 2.0f));
            g.setColour(colour.withAlpha(audible ? 0.7f : 0.3f));
            for (std::size_t x = 0; x < peaks.size(); ++x)
            {
                const float h = juce::jmax(0.5f, peaks[x] * wave.getHeight() * 0.5f);
                g.fillRect(wave.getX() + static_cast<float>(x) * 2.0f, wave.getCentreY() - h, 1.2f, h * 2.0f);
            }
        }

        g.setColour(audible ? zdui::kText : zdui::kDim);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(owner_.padName(i), inner.withTrimmedRight(30), juce::Justification::topLeft);
        g.setColour(zdui::kDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String(i + 1), inner, juce::Justification::bottomRight);

        // Mute and solo badges, matching the lane headers.
        auto badges = inner.toFloat().removeFromTop(16.0f);
        auto badge = [&](const char* text, juce::Colour c) {
            const auto b = badges.removeFromRight(16.0f);
            badges.removeFromRight(3.0f);
            g.setColour(c);
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour(juce::Colours::black);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(text, b, juce::Justification::centred);
        };
        if (owner_.laneSoloed(i))
            badge("S", juce::Colour(0xff40c0e0));
        if (owner_.laneMuted(i))
            badge("M", juce::Colour(0xffe0b040));
    }
}

const std::vector<float>& PadGrid::padPeaks(int part, int width)
{
    auto& cache = peaks_[static_cast<std::size_t>(part)];
    const auto data = owner_.padSample(part);
    if (data == nullptr || width <= 0)
    {
        cache = {};
        return cache.values;
    }
    if (cache.of == data.get() && static_cast<int>(cache.values.size()) == width)
        return cache.values;
    cache.of = data.get();
    cache.values.assign(static_cast<std::size_t>(width), 0.0f);
    const auto n = data->samples.size();
    float top = 1.0e-6f;
    for (std::size_t x = 0; x < cache.values.size(); ++x)
    {
        const std::size_t a = x * n / cache.values.size(), b = std::max(a + 1, (x + 1) * n / cache.values.size());
        float peak = 0.0f;
        for (std::size_t k = a; k < b && k < n; ++k)
            peak = std::max(peak, std::abs(data->samples[k]));
        cache.values[x] = peak;
        top = std::max(top, peak);
    }
    for (auto& v : cache.values)
        v /= top;
    return cache.values;
}

void PadGrid::mouseMove(const juce::MouseEvent& e)
{
    const int pad = padAt(e.position);
    if (pad != hover_)
    {
        hover_ = pad;
        repaint();
    }
}

void PadGrid::mouseExit(const juce::MouseEvent&)
{
    if (hover_ >= 0)
    {
        hover_ = -1;
        repaint();
    }
}

void PadGrid::timerCallback()
{
    bool changed = false;
    std::uint64_t mixState = owner_.audibleMask();
    for (int i = 0; i < kPads; ++i)
        mixState |= (static_cast<std::uint64_t>(owner_.laneMuted(i)) << (32 + i)) | (static_cast<std::uint64_t>(owner_.laneSoloed(i)) << (48 + i));
    if (mixState != lastMixState_)
    {
        lastMixState_ = mixState;
        changed = true;
    }
    for (int i = 0; i < kPads; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const auto hits = owner_.partHits(i);
        if (hits != lastHits_[idx])
        {
            lastHits_[idx] = hits;
            glow_[idx] = 1.0f;
            changed = true;
        }
        else if (glow_[idx] > 0.0f)
        {
            glow_[idx] = juce::jmax(0.0f, glow_[idx] - 0.12f);
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void PadGrid::mouseDown(const juce::MouseEvent& e)
{
    pressed_ = padAt(e.position);
    if (pressed_ < 0)
        return;
    selected_ = pressed_;
    if (onSelect)
        onSelect(selected_);
    if (e.mods.isPopupMenu())
    {
        const int part = pressed_;
        pressed_ = -1;
        juce::PopupMenu menu;
        if (ZeroDrumProcessor::isSamplePad(part))
        {
            menu.addItem("Load sample...", [this, part] { chooseSample(part); });
            menu.addItem("Clear pad", owner_.padHasSample(part), false, [this, part] {
                owner_.clearPadSample(part);
                if (onSampleChanged)
                    onSampleChanged(part);
                repaint();
            });
            menu.addSeparator();
        }
        menu.addItem("Rename...", [this, part] {
            zdui::askPartName(owner_, part, [this] { repaint(); if (onSampleChanged) onSampleChanged(selected_); });
        });
        menu.addItem("Save part...", [this, part] { choosePartFile(part, true); });
        menu.addItem("Load part...", [this, part] { choosePartFile(part, false); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
        repaint();
        return;
    }
    const float velocity = velocityAt(pressed_, e.position);
    if (e.mods.isShiftDown())
    {
        repeating_ = true;
        owner_.setNoteRepeat(pressed_, velocity); // repeat plays the first hit itself
    }
    else
        owner_.auditionPart(pressed_, velocity);
    repaint();
}

void PadGrid::mouseDrag(const juce::MouseEvent& e)
{
    // Dragging a pad out of the grid drops its lane into the DAW as MIDI.
    if (pressed_ < 0 || dragging_ || getLocalBounds().contains(e.getPosition()))
        return;
    if (pressed_ >= ZeroDrumProcessor::kNumStripParts && !owner_.padHasSample(pressed_))
        return;
    if (repeating_)
    {
        owner_.setNoteRepeat(-1, 1.0f);
        repeating_ = false;
    }
    // Alt-drag for the part's audio (one loop, just this sound); a plain drag gives MIDI.
    const auto file = e.mods.isAltDown() ? owner_.writeLoopWav(pressed_) : owner_.writeMidiClip(pressed_);
    if (!file.existsAsFile())
        return;
    dragging_ = true;
    juce::Component::SafePointer<PadGrid> self(this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this, [self] {
        if (self != nullptr)
            self->dragging_ = false;
    });
}

void PadGrid::mouseUp(const juce::MouseEvent&)
{
    if (repeating_)
        owner_.setNoteRepeat(-1, 1.0f);
    repeating_ = false;
    pressed_ = -1;
}

bool PadGrid::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1
        && (owner_.isAudioFile(files[0]) || juce::File(files[0]).hasFileExtension(ZeroDrumProcessor::kPartExtension));
}

void PadGrid::fileDragMove(const juce::StringArray& files, int x, int y)
{
    const int pad = padAt({ static_cast<float>(x), static_cast<float>(y) });
    const bool partFile = files.size() == 1 && juce::File(files[0]).hasFileExtension(ZeroDrumProcessor::kPartExtension);
    const int target = (partFile && pad >= 0) || ZeroDrumProcessor::isSamplePad(pad) ? pad : -1;
    if (target != dropTarget_)
    {
        dropTarget_ = target;
        repaint();
    }
}

void PadGrid::fileDragExit(const juce::StringArray&)
{
    dropTarget_ = -1;
    repaint();
}

void PadGrid::filesDropped(const juce::StringArray& files, int x, int y)
{
    dropTarget_ = -1;
    const int pad = padAt({ static_cast<float>(x), static_cast<float>(y) });
    const juce::File file(files.size() == 1 ? files[0] : juce::String());
    const bool loaded = pad >= 0
                     && (file.hasFileExtension(ZeroDrumProcessor::kPartExtension) ? owner_.loadPart(pad, file)
                                                                                  : ZeroDrumProcessor::isSamplePad(pad) && owner_.loadPadSample(pad, file));
    if (loaded)
    {
        selected_ = pad;
        if (onSelect)
            onSelect(pad);
        if (onSampleChanged)
            onSampleChanged(pad);
    }
    repaint();
}

void PadGrid::chooseSample(int part)
{
    chooser_ = std::make_unique<juce::FileChooser>("Load a sample onto pad " + juce::String(part + 1),
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<PadGrid> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self, part](const juce::FileChooser& fc) {
                              if (self == nullptr || fc.getResult() == juce::File {})
                                  return;
                              if (self->owner_.loadPadSample(part, fc.getResult()) && self->onSampleChanged)
                                  self->onSampleChanged(part);
                              self->repaint();
                          });
}

void PadGrid::choosePartFile(int part, bool save)
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Zero Drum").getChildFile("Parts");
    dir.createDirectory();
    const juce::String pattern = juce::String("*") + ZeroDrumProcessor::kPartExtension;
    chooser_ = std::make_unique<juce::FileChooser>(save ? "Save " + owner_.padName(part) : "Load a part onto pad " + juce::String(part + 1),
                                                   save ? dir.getChildFile(owner_.padName(part) + ZeroDrumProcessor::kPartExtension) : dir,
                                                   pattern);
    const int mode = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                           : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<PadGrid> self(this);
    chooser_->launchAsync(mode, [self, part, save](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (self == nullptr || file == juce::File {})
            return;
        if (save)
        {
            if (!file.hasFileExtension(ZeroDrumProcessor::kPartExtension))
                file = file.withFileExtension(ZeroDrumProcessor::kPartExtension);
            self->owner_.savePart(part, file);
            return;
        }
        if (!self->owner_.loadPart(part, file))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Zero Drum",
                                                   "That part doesn't fit this pad. Kit parts (kick, snare, hat, perc, 808) load on their own pad; "
                                                   "sample pad parts load on pads 6 to 16.");
            return;
        }
        if (self->onSampleChanged)
            self->onSampleChanged(part);
        self->repaint();
    });
}
