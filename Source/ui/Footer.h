#pragma once
#include "../PluginProcessor.h"

namespace stsplit
{

/** Bottom tray: analyser and the total chain latency on the left; In / Mix / Out on the right. (The split's slope
    lives on the split itself, in the display.) */
class FooterBar : public juce::Component, private juce::Timer
{
public:
    explicit FooterBar (StSplitProcessor&);
    std::function<void (int)> onAnalyzerModeChanged;
    void setAnalyzerMode (int);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSplitProcessor& proc;
    st::FlatButton analyzer { "", "Spectrum analyser" };
    st::NumberBox input, mix, output;
    int analyzerMode = 1, shownLatency = -1;
    std::vector<float> dividers;
    juce::Rectangle<int> latencyArea;

    void timerCallback() override;
};

} // namespace stsplit
