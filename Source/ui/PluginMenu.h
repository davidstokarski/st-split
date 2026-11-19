#pragma once
#include "../host/PluginCatalog.h"

namespace stsplit
{

/** Native menu of the user's effects: Recent, then one submenu per manufacturer, then scan status / Rescan.
    The first call in a process refreshes the catalogue (Audio Units instantly, VST3s in the background). */
void showPluginMenu (PluginCatalog&, juce::Component* target, std::function<void (const juce::PluginDescription&)> onChosen,
                     bool atMouse = false, const juce::String& title = {});

/** Adds the same plug-in tree to an existing menu (e.g. a "Replace" submenu). */
void addPluginItems (juce::PopupMenu&, PluginCatalog&, std::function<void (const juce::PluginDescription&)> onChosen);

} // namespace stsplit
