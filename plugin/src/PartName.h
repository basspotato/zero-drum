#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace zdui {

/// Asks for a part's new name in a small window; an empty name goes back to the default.
/// `done` runs on the message thread after a rename.
inline void askPartName(ZeroDrumProcessor& owner, int part, std::function<void()> done)
{
    auto* window = new juce::AlertWindow("Rename part", "Name for part " + juce::String(part + 1) + " (empty for the default):",
                                         juce::MessageBoxIconType::NoIcon);
    juce::SharedResourcePointer<LookAndFeel> look;
    window->setLookAndFeel(&look.get());
    window->addTextEditor("name", owner.padName(part));
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, look, &owner, part, done = std::move(done)](int result) {
                                window->setLookAndFeel(nullptr);
                                if (result != 1)
                                    return;
                                owner.setPartName(part, window->getTextEditorContents("name"));
                                if (done)
                                    done();
                            }),
                            true);
}

} // namespace zdui
