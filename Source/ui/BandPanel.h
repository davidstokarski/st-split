#pragma once
#include "Style.h"

namespace stsplit
{

/** The selected band's controls (FabFilter Saturn / ST Sat style): one small glass bar that glides under whichever
    band is selected (the band's name and S / M live on the display). Band on/off sits on its top edge; Mix and a
    ringed Level in the band's colour hang from it; a readout shows the chain (plug-ins, latency). One instance per
    band selection: the editor rebuilds and cross-fades it when the selection changes. */
class BandPanel : public juce::Component, private juce::Timer
{
public:
    BandPanel (StSplitProcessor&, int band);

    int getBand() const noexcept { return band; }

    static constexpr int width = 306, height = 140, edgeHeight = 12;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSplitProcessor& proc;
    const int band;
    std::unique_ptr<st::ToggleChip> power;
    std::unique_ptr<st::Knob> mix, level;
    juce::Rectangle<float> box;
    juce::Rectangle<int> info;
    juce::String shownInfo;

    juce::String infoText() const;
    void timerCallback() override;
};

} // namespace stsplit
