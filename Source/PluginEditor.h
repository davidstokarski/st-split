#pragma once
#include "PluginProcessor.h"
#include "ui/SplitDisplay.h"
#include "ui/InsertStack.h"
#include "ui/BandPanel.h"
#include "ui/Footer.h"
#include "ui/PluginWindow.h"

/** FabFilter-style layout: the whole window is the split display. Each band's insert stack rides in its column
    (following the split as it moves), the selected band's panel glides underneath it and cross-fades when the
    selection changes, and the footer tray holds the global settings. */
class StSplitEditor : public st::EditorBase, private stsplit::ChainHost::Listener
{
public:
    explicit StSplitEditor (StSplitProcessor&);
    ~StSplitEditor() override;

    static constexpr int designWidth = 1040, designHeight = 690;
    static constexpr int footerHeight = 40;

    /** Opens (or brings to front) the window of a hosted plug-in. */
    void openPluginWindow (const juce::String& uid);
    void closePluginWindow (const juce::String& uid);
    int getNumOpenPluginWindows() const noexcept { return (int) windows.size(); }
    void selectBand (int band);
    stsplit::InsertStack& getStack (int band) noexcept { return *stacks[(size_t) band]; }

private:
    StSplitProcessor& proc;
    stsplit::SplitDisplay display;
    std::array<std::unique_ptr<stsplit::InsertStack>, stsplit::kMaxBands> stacks;
    std::unique_ptr<stsplit::BandPanel> bandPanel, outgoingPanel;
    st::Animated panelX, panelFade, outgoingFade;
    stsplit::FooterBar footer;
    std::map<juce::String, std::unique_ptr<stsplit::PluginWindow>> windows;
    int selectedBand = 0;
    juce::Image backdrop;
    float backdropScale = 0.0f;

    void layout (juce::Rectangle<int> body) override;
    void paintBackground (juce::Graphics&, juce::Rectangle<int>) override;
    void animate (double dt);
    float targetPanelX() const;
    int panelY() const;
    void addPluginTo (int band);
    void updateOpenEditors();
    void slotRemoved (const juce::String& uid) override { closePluginWindow (uid); }
    void chainsChanged() override { updateOpenEditors(); }
};
