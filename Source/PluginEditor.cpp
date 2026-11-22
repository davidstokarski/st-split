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
        auto stack = std::make_unique<InsertStack> (proc, b);
        stack->onOpenEditor = [this] (const juce::String& uid) { openPluginWindow (uid); };
        stack->onSelected = [this] (int band) { selectBand (band); };
        content.addAndMakeVisible (*stack);
        stacks[(size_t) b] = std::move (stack);
    }

    content.addAndMakeVisible (footer);
    footer.setBufferedToImage (true);
    header.setBufferedToImage (true);

    display.onBandSelected = [this] (int b) { selectBand (b); };
    display.onFrame = [this] (double dt) { animate (dt); };
    display.onAddPluginRequested = [this] (int b) { addPluginTo (b); };

    footer.onAnalyzerModeChanged = [this] (int m)
    {
        proc.uiState.setProperty ("analyzer", m, nullptr);
        display.setAnalyzerMode ((SplitDisplay::AnalyzerMode) juce::jlimit (0, 2, m));
    };
    footer.setAnalyzerMode ((int) proc.uiState.getProperty ("analyzer", 1));

    panelFade.snap (1.0f);
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

void StSplitEditor::addPluginTo (int band)
{
    selectBand (band);
    stacks[(size_t) band]->showAddMenu (proc.getChains().getNumSlots (band), nullptr);
}

void StSplitEditor::selectBand (int band)
{
    band = juce::jlimit (0, kMaxBands - 1, band);
    const bool changed = bandPanel == nullptr || bandPanel->getBand() != band;
    selectedBand = band;
    proc.uiState.setProperty ("selectedBand", selectedBand, nullptr);
    display.setSelectedBand (selectedBand);
    if (! changed) return;

    // The band panel glides to the new band; the old one cross-fades out (ST Sat / Saturn).
    const bool animateChange = bandPanel != nullptr;
    if (animateChange)
    {
        outgoingPanel = std::move (bandPanel);
        outgoingPanel->setInterceptsMouseClicks (false, false);
        outgoingFade.snap (1.0f);
    }

    bandPanel = std::make_unique<BandPanel> (proc, band);
    bandPanel->setBufferedToImage (true);
    getContent().addAndMakeVisible (*bandPanel);
    header.toFront (false);

    if (animateChange) panelFade.snap (0.0f);
    else { panelX.snap (targetPanelX()); panelFade.snap (1.0f); }
    animate (0.0);
}

float StSplitEditor::targetPanelX() const
{
    const auto xr = display.getBandXRange (selectedBand);
    const float w = (float) BandPanel::width;
    return juce::jlimit (8.0f, (float) designWidth - 8.0f - w, xr.getStart() + xr.getLength() * 0.5f - w * 0.5f);
}

int StSplitEditor::panelY() const
{
    return juce::roundToInt ((float) display.getBottom() - SplitDisplay::scaleHeight) - BandPanel::height - 4;
}

void StSplitEditor::animate (double dt)
{
    // insert stacks ride in their band columns (following the split as it glides)
    const int hovered = display.getHoveredBand();
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& s = *stacks[(size_t) b];
        s.setBounds (display.getStackArea (b, s.getPreferredHeight()) + display.getPosition());
        s.setRevealed (b == hovered || b == selectedBand || s.isMouseOver (true));
    }

    if (bandPanel == nullptr) return;
    panelX.setTarget (targetPanelX());
    if (dt <= 0.0) panelX.snap (panelX.target);
    else           panelX.tick (dt, 11.0f);
    panelFade.setTarget (1.0f);
    panelFade.tick (dt, 14.0f);
    bandPanel->setBounds (juce::roundToInt (panelX.current), panelY() + juce::roundToInt ((1.0f - panelFade.current) * 8.0f),
                          BandPanel::width, BandPanel::height);
    bandPanel->setAlpha (panelFade.current);

    if (outgoingPanel != nullptr)
    {
        outgoingFade.setTarget (0.0f);
        if (! outgoingFade.tick (dt, 18.0f) || outgoingFade.current < 0.02f) outgoingPanel.reset();
        else outgoingPanel->setAlpha (outgoingFade.current);
    }
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
    for (auto& s : stacks) s->setOpenEditors (open);
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

void StSplitEditor::layout (juce::Rectangle<int>)
{
    auto all = getContent().getLocalBounds();
    footer.setBounds (all.removeFromBottom (footerHeight));
    display.setBounds (all);
    animate (0.0);
}
