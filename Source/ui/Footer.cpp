#include "Footer.h"
#include "Style.h"

namespace stsplit
{

FooterBar::FooterBar (StSplitProcessor& p)
    : proc (p),
      slope (p.param (ids::slope), &p.undoManager),
      input (p.param (ids::inputGain), &p.undoManager, "In"),
      mix (p.param (ids::mix), &p.undoManager, "Mix"),
      output (p.param (ids::outputGain), &p.undoManager, "Out")
{
    slope.setFlatStyle (true);
    slope.setCaption ("Slope");
    for (auto* n : { &input, &mix, &output }) n->setFlatStyle (true);

    slope.setTooltip ("Steepness of the split (Linkwitz-Riley: the bands always sum back flat)");
    input.setTooltip ("Input gain (drag)");
    mix.setTooltip ("Global dry/wet of every band's plug-ins, latency compensated (drag)");
    output.setTooltip ("Output gain (drag)");

    analyzer.setDropdownArrow (true);
    analyzer.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Off", true, analyzerMode == 0);
        m.addItem (2, "Post", true, analyzerMode == 1);
        m.addItem (3, "Pre + Post", true, analyzerMode == 2);
        st::showMenu (m, &analyzer, [safe = juce::Component::SafePointer<FooterBar> (this)] (int r) { if (safe != nullptr && r > 0) safe->setAnalyzerMode (r - 1); });
    };

    for (juce::Component* c : { (juce::Component*) &slope, (juce::Component*) &analyzer,
                                (juce::Component*) &input, (juce::Component*) &mix, (juce::Component*) &output })
        addAndMakeVisible (c);

    startTimerHz (4);
}

void FooterBar::setAnalyzerMode (int m)
{
    analyzerMode = juce::jlimit (0, 2, m);
    analyzer.setButtonText (analyzerMode == 0 ? "Off" : analyzerMode == 1 ? "Post" : "Pre + Post");
    analyzer.setDimmedText ("Analyzer  ");
    if (onAnalyzerModeChanged) onAnalyzerModeChanged (analyzerMode);
}

void FooterBar::timerCallback()
{
    const int l = proc.getLatencySamples();
    if (l != shownLatency)
    {
        shownLatency = l;
        repaint (latencyArea);
    }
}

void FooterBar::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    style::strip (g, r, false);
    for (float x : dividers)
    {
        g.setColour (t.strokeSoft);
        g.fillRect (x, r.getY() + 9.0f, 1.0f, r.getHeight() - 18.0f);
        g.setColour (t.highlight);
        g.fillRect (x + 1.0f, r.getY() + 9.0f, 1.0f, r.getHeight() - 18.0f);
    }

    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    const double ms = 1000.0 * juce::jmax (0, shownLatency) / sr;
    g.setFont (st::fonts::medium (13.0f));
    g.setColour (t.textDim.withAlpha (0.7f));
    const auto label = juce::String ("Latency  ");
    const float w = st::fonts::stringWidth (g.getCurrentFont(), label);
    auto a = latencyArea.toFloat();
    g.drawText (label, a.removeFromLeft (w), juce::Justification::centredLeft, false);
    g.setColour (t.text.withAlpha (0.9f));
    g.drawText (shownLatency <= 0 ? juce::String ("None") : juce::String (ms, ms < 10.0 ? 1 : 0) + " ms", a, juce::Justification::centredLeft, false);
}

void FooterBar::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    dividers.clear();
    auto divide = [this, &r] { dividers.push_back ((float) r.getX() + 10.0f); r.removeFromLeft (20); };

    slope.setBounds (r.removeFromLeft (150));
    divide();
    analyzer.setBounds (r.removeFromLeft (128));
    divide();
    latencyArea = r.removeFromLeft (140);

    output.setBounds (r.removeFromRight (96));
    r.removeFromRight (8);
    mix.setBounds (r.removeFromRight (82));
    r.removeFromRight (8);
    input.setBounds (r.removeFromRight (88));
    dividers.push_back ((float) input.getX() - 12.0f);
}

} // namespace stsplit
