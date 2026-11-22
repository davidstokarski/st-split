#include "BandPanel.h"

namespace stsplit
{

BandPanel::BandPanel (StSplitProcessor& p, int b) : proc (p), band (b)
{
    auto* um = &proc.undoManager;
    const auto colour = getBandColour (band);

    power = std::make_unique<st::ToggleChip> (proc.bandParam (band, "bypass"), um, "", colour);
    power->setIcon (st::icons::Icon::power);
    power->setInverted (true);
    power->setGreyStyle (true);
    power->setTooltip ("Band plug-ins on / off (off passes the band through untouched)");
    addAndMakeVisible (*power);

    mix = std::make_unique<st::Knob> (proc.bandParam (band, "mix"), um, "Mix");
    mix->setSize (st::Knob::Size::medium);
    mix->setTooltip ("Dry / wet of this band's plug-ins (latency compensated, so partial mixes don't comb)");
    level = std::make_unique<st::Knob> (proc.bandParam (band, "level"), um, "Level");
    level->setSize (st::Knob::Size::medium);
    level->setLook (st::Knob::Look::ringed);
    level->setColour (colour);
    level->setBipolar (true);
    level->setTooltip ("Output level of this band (also the handle on the display)");
    addAndMakeVisible (*mix);
    addAndMakeVisible (*level);

    shownInfo = infoText();
    startTimerHz (4);
}

juce::String BandPanel::infoText() const
{
    const int n = proc.getChains().getNumSlots (band);
    const int lat = proc.getChains().getBandLatency (band);
    juce::String s = n == 0 ? juce::String ("No plug-ins") : juce::String (n) + (n == 1 ? " plug-in" : " plug-ins");
    const double ms = 1000.0 * lat / juce::jmax (1.0, proc.getSampleRate());
    s << "\n" << (lat > 0 ? juce::String (ms, ms < 10.0 ? 1 : 0) + " ms latency" : juce::String ("No latency"));
    return s;
}

void BandPanel::timerCallback()
{
    const auto s = infoText();
    if (s != shownInfo) { shownInfo = s; repaint (info); }
}

void BandPanel::resized()
{
    box = getLocalBounds().toFloat().withTrimmedTop ((float) edgeHeight).reduced (3.0f, 0.0f).withTrimmedBottom (6.0f);
    power->setBounds (18, 0, 32, 24);

    auto inner = box.toNearestInt().reduced (12, 0).withTrimmedTop (16).withTrimmedBottom (6);
    mix->setBounds (inner.removeFromLeft (86).withSizeKeepingCentre (84, 100).translated (0, 4));
    level->setBounds (inner.removeFromLeft (104).withSizeKeepingCentre (96, 112));
    inner.removeFromLeft (10);
    info = inner.withSizeKeepingCentre (inner.getWidth(), 36).translated (0, -6);
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    style::paintGlassBar (g, box, getBandColour (band));

    // chain readout
    st::paint::fadedLine (g, { (float) info.getX() - 6.5f, box.getY() + 14.0f }, { (float) info.getX() - 6.5f, box.getBottom() - 12.0f },
                          juce::Colours::white.withAlpha (0.08f), 1.0f, 0.35f);
    const auto lines = juce::StringArray::fromLines (shownInfo);
    g.setFont (st::fonts::medium (12.5f));
    g.setColour (t.text.withAlpha (0.9f));
    g.drawText (lines[0], info.withHeight (18), juce::Justification::centredLeft, true);
    g.setFont (st::fonts::regular (11.5f));
    g.setColour (t.textDim);
    g.drawText (lines[1], info.withTrimmedTop (18), juce::Justification::centredLeft, true);
}

} // namespace stsplit
