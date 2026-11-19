#pragma once
#include "../Params.h"

namespace stsplit
{

/** A hosted plug-in's own editor in a floating window (always on top, so it stays above the host's plug-in
    windows). Plug-ins without an editor get JUCE's generic parameter editor. The window must be deleted before
    its plug-in instance: the owner closes it from ChainHost::Listener::slotRemoved. */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance&, const juce::String& title, std::function<void()> onCloseRequested);
    ~PluginWindow() override;

    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace stsplit
