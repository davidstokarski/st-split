#include "PluginEditor.h"
#include "ui/PluginMenu.h"
#include "ui/Style.h"

using namespace stsplit;

StSplitEditor::StSplitEditor (StSplitProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      display (p),
      footer (p)
{
    header.setProductName ("Split");
    header.extendSettingsMenu = [this] (juce::PopupMenu& m)
    {
        auto& catalog = proc.getChains().getCatalog();
        using Pref = PluginCatalog::FormatPreference;
        const auto pref = catalog.getPreference();
        juce::PopupMenu formats;
        formats.addItem ("Prefer Audio Units", true, pref == Pref::audioUnit, [] { juce::SharedResourcePointer<PluginCatalog> c; c->setPreference (Pref::audioUnit); });
        formats.addItem ("Prefer VST3", true, pref == Pref::vst3, [] { juce::SharedResourcePointer<PluginCatalog> c; c->setPreference (Pref::vst3); });
        formats.addItem ("Show both", true, pref == Pref::both, [] { juce::SharedResourcePointer<PluginCatalog> c; c->setPreference (Pref::both); });
        m.addSeparator();
        m.addSubMenu ("Plug-in format", formats);
        m.addItem ("Rescan plug-ins", ! catalog.isScanning(), false, [] { juce::SharedResourcePointer<PluginCatalog> c; c->rescanAll(); });
    };

    auto& content = getContent();
    content.addAndMakeVisible (display);

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto rack = std::make_unique<BandRack> (proc, b);
        rack->onOpenEditor = [this] (const juce::String& uid) { openPluginWindow (uid); };
        rack->onSelected = [this] (int band) { selectBand (band); };
        rack->setBufferedToImage (true);
        content.addAndMakeVisible (*rack);
        racks[(size_t) b] = std::move (rack);
    }

    content.addAndMakeVisible (footer);
    footer.setBufferedToImage (true);
    header.setBufferedToImage (true);

    display.onBandSelected = [this] (int b) { selectBand (b); };
    display.onFrame = [this] (double dt) { placeRacks (dt); };
    display.onAddPluginRequested = [this] (int b)
    {
        selectBand (b);
        racks[(size_t) b]->showAddMenu (proc.getChains().getNumSlots (b), nullptr);
    };

    footer.onAnalyzerModeChanged = [this] (int m)
    {
        proc.uiState.setProperty ("analyzer", m, nullptr);
        display.setAnalyzerMode ((SplitDisplay::AnalyzerMode) juce::jlimit (0, 2, m));
    };
    footer.setAnalyzerMode ((int) proc.uiState.getProperty ("analyzer", 1));

    selectBand (juce::jlimit (0, kMaxBands - 1, (int) proc.uiState.getProperty ("selectedBand", 0)));
    proc.getChains().addListener (this);

    header.toFront (false);
    relayout();
}

StSplitEditor::~StSplitEditor()
{
    proc.getChains().removeListener (this);
    windows.clear();
}

void StSplitEditor::selectBand (int band)
{
    selectedBand = juce::jlimit (0, kMaxBands - 1, band);
    proc.uiState.setProperty ("selectedBand", selectedBand, nullptr);
    display.setSelectedBand (selectedBand);
    for (int b = 0; b < kMaxBands; ++b)
        racks[(size_t) b]->setSelected (b == selectedBand);
}

void StSplitEditor::openPluginWindow (const juce::String& uid)
{
    if (auto it = windows.find (uid); it != windows.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }

    auto* instance = proc.getChains().getInstance (uid);
    if (instance == nullptr) return;

    const int band = proc.getChains().findBand (uid);
    const auto title = "ST Split  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  "
                     + getBandName (band) + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + instance->getName();
    auto window = std::make_unique<PluginWindow> (*instance, title, [this, uid]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StSplitEditor> (this), uid]
        {
            if (safe != nullptr) safe->closePluginWindow (uid);
        });
    });

    // Cascade from the top-left of the ST Split window.
    const auto origin = isShowing() ? getScreenPosition() : juce::Point<int> (120, 120);
    const int offset = 28 * ((int) windows.size() % 8);
    window->setTopLeftPosition (origin.x + 40 + offset, origin.y + 60 + offset);
    window->setVisible (true);
    windows[uid] = std::move (window);
    updateOpenEditors();
}

void StSplitEditor::closePluginWindow (const juce::String& uid)
{
    if (windows.erase (uid) > 0)
        updateOpenEditors();
}

void StSplitEditor::updateOpenEditors()
{
    juce::StringArray open;
    for (auto& [uid, w] : windows) open.add (uid);
    for (auto& r : racks) r->setOpenEditors (open);
}

void StSplitEditor::paintBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    // static: render once per physical scale, blit afterwards (it sits behind everything that animates)
    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto full = getContent().getLocalBounds();
    if (backdrop.isNull() || std::abs (pixelScale - backdropScale) > 0.01f)
    {
        backdropScale = pixelScale;
        backdrop = juce::Image (juce::Image::RGB, juce::roundToInt ((float) full.getWidth() * pixelScale),
                                juce::roundToInt ((float) full.getHeight() * pixelScale), false);
        juce::Graphics bg (backdrop);
        bg.addTransform (juce::AffineTransform::scale (pixelScale));
        style::paintBackdrop (bg, full.toFloat());
    }
    g.drawImage (backdrop, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void StSplitEditor::placeRacks (double dt)
{
    // Both band panels sit on a common baseline above the footer and grow upwards with their plug-in count.
    const int w = (designWidth - 2 * panelMargin - (kMaxBands - 1) * panelGap) / kMaxBands;
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& h = rackHeight[(size_t) b];
        h.setTarget ((float) racks[(size_t) b]->getPreferredHeight());
        if (dt <= 0.0) h.snap (h.target);
        else h.tick (dt, 16.0f);
        const int height = juce::roundToInt (h.current);
        racks[(size_t) b]->setBounds (panelMargin + b * (w + panelGap), panelsBottom - height, w, height);
    }
}

void StSplitEditor::layout (juce::Rectangle<int>)
{
    auto all = getContent().getLocalBounds();
    display.setBounds (all.withHeight (displayHeight));
    footer.setBounds (all.removeFromBottom (footerHeight));
    placeRacks (0.0);
}
