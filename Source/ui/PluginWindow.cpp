#include "PluginWindow.h"

namespace stsplit
{

PluginWindow::PluginWindow (juce::AudioPluginInstance& instance, const juce::String& title, std::function<void()> onCloseRequested)
    : juce::DocumentWindow (title, st::Theme::get().bg1, juce::DocumentWindow::closeButton),
      onClose (std::move (onCloseRequested))
{
    juce::AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
    {
        auto* generic = new juce::GenericAudioProcessorEditor (instance);
        generic->setSize (juce::jmax (360, generic->getWidth()), juce::jmax (240, generic->getHeight()));
        editor = generic;
    }

    setUsingNativeTitleBar (true);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    setAlwaysOnTop (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();   // the editor goes first, while its plug-in is still alive
}

} // namespace stsplit
