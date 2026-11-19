#include "PluginMenu.h"

namespace stsplit
{

void addPluginItems (juce::PopupMenu& menu, PluginCatalog& catalog, std::function<void (const juce::PluginDescription&)> onChosen)
{
    if (catalog.needsInitialRefresh())
        catalog.refresh();

    const bool showFormat = catalog.getPreference() == PluginCatalog::FormatPreference::both;
    auto itemFor = [&] (const juce::PluginDescription& d, bool withManufacturer)
    {
        auto text = d.name;
        if (withManufacturer) text << "  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << "  " << d.manufacturerName;
        if (showFormat) text << "  (" << PluginCatalog::shortFormatName (d) << ")";
        return juce::PopupMenu::Item (text).setAction ([d, onChosen] { if (onChosen) onChosen (d); });
    };

    const auto recent = catalog.getRecent();
    if (! recent.empty())
    {
        menu.addSectionHeader ("Recent");
        for (auto& d : recent) menu.addItem (itemFor (d, true));
        menu.addSeparator();
    }

    const auto effects = catalog.getEffects();
    juce::String currentMaker;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (currentMaker.isNotEmpty()) menu.addSubMenu (currentMaker, sub);
        sub = {};
    };
    for (auto& d : effects)
    {
        const auto maker = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
        if (! maker.equalsIgnoreCase (currentMaker)) { flush(); currentMaker = maker; }
        sub.addItem (itemFor (d, false));
    }
    flush();

    if (effects.empty())
        menu.addItem (juce::PopupMenu::Item ("No effect plug-ins found").setEnabled (false));

    menu.addSeparator();
    if (catalog.isScanning())
    {
        const auto progress = catalog.getScanProgress();
        menu.addItem (juce::PopupMenu::Item ("Scanning VST3" + (progress.isNotEmpty() ? ": " + progress : juce::String()))
                          .setEnabled (false));
    }
    else if (! catalog.canScanVst3())
        menu.addItem (juce::PopupMenu::Item ("VST3 scanning unavailable (ST Split Scanner missing)").setEnabled (false));
    else
        menu.addItem (juce::PopupMenu::Item ("Rescan plug-ins").setAction ([] { juce::SharedResourcePointer<PluginCatalog> c; c->rescanAll(); }));
}

void showPluginMenu (PluginCatalog& catalog, juce::Component* target, std::function<void (const juce::PluginDescription&)> onChosen,
                     bool atMouse, const juce::String& title)
{
    juce::PopupMenu menu;
    if (title.isNotEmpty()) menu.addSectionHeader (title);
    addPluginItems (menu, catalog, std::move (onChosen));
    st::showMenu (menu, target, {}, atMouse);
}

} // namespace stsplit
