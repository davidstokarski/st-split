#pragma once
#include "../PluginProcessor.h"

/** ST Split's identity inside the ST family (same structure as ST Sat / ST EQ / ST DC: full-window display, glass
    bars with grey buttons on their top edge, matte knobs, footer tray). Its own part: a cool slate near-black,
    with the two bands' colours (amber low, cyan high) as soft light pools on their side of the window. */
namespace stsplit::style
{
    /** Recolours the shared ST tokens. Called once from the processor constructor. */
    void applyTheme();

    /** Window backdrop: slate haze at the top fading to near-black, amber light low-left, cyan light right,
        vignette. No texture. Static: the editor renders it once per scale. */
    void paintBackdrop (juce::Graphics&, juce::Rectangle<float> area);

    /** Footer tray: dark, rounded top corners, faded top edge light. */
    void paintTray (juce::Graphics&, juce::Rectangle<float> area);

    /** Translucent glass bar (band panels): soft shadow, glassPanel fill, hairline, top edge light that is
        brightest in the middle (in `edge` when given, e.g. the selected band's colour). */
    void paintGlassBar (juce::Graphics&, juce::Rectangle<float> box, juce::Colour edge = {});
}
