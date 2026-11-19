#pragma once
#include "PluginProcessor.h"
#include "ui/SplitDisplay.h"
#include "ui/BandRack.h"
#include "ui/Footer.h"
#include "ui/PluginWindow.h"

class StSplitEditor : public st::EditorBase, private stsplit::ChainHost::Listener
{
public:
    explicit StSplitEditor (StSplitProcessor&);
    ~StSplitEditor() override;

    static constexpr int designWidth = 1040, designHeight = 720;
    static constexpr int displayHeight = 372, rackY = 376, rackHeight = 300, footerHeight = 40;

    /** Opens (or brings to front) the window of a hosted plug-in. */
    void openPluginWindow (const juce::String& uid);
    void closePluginWindow (const juce::String& uid);
    int getNumOpenPluginWindows() const noexcept { return (int) windows.size(); }
    void selectBand (int band);
    stsplit::BandRack& getRack (int band) noexcept { return *racks[(size_t) band]; }

private:
    StSplitProcessor& proc;
    stsplit::SplitDisplay display;
    std::array<std::unique_ptr<stsplit::BandRack>, stsplit::kMaxBands> racks;
    stsplit::FooterBar footer;
    std::map<juce::String, std::unique_ptr<stsplit::PluginWindow>> windows;
    int selectedBand = 0;

    void layout (juce::Rectangle<int> body) override;
    void updateOpenEditors();
    void slotRemoved (const juce::String& uid) override { closePluginWindow (uid); }
    void chainsChanged() override { updateOpenEditors(); }
};
