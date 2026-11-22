#include "Style.h"

namespace stsplit::style
{

void applyTheme()
{
    // Near-black with a cool slate cast: neutral, so both band colours glow true.
    auto& t = st::Theme::get();
    t.bg0 = juce::Colour (0xff090a0d);
    t.bg1 = juce::Colour (0xff0f1115);
    t.bg2 = juce::Colour (0xff16191e);
    t.bg3 = juce::Colour (0xff1f232a);
    t.bg4 = juce::Colour (0xff2a2f37);
    t.stroke = juce::Colour (0xff292d34);
    t.strokeSoft = juce::Colour (0xff181b20);
    t.highlight = juce::Colour (0x12ffffff);
    t.displayTop = juce::Colour (0xff181c24);    // slate haze at the top of the window
    t.displayGlow = juce::Colour (0xff2c3442);
    t.glassPanel = juce::Colour (0xb81a1e25);
    t.knobLight = juce::Colour (0xffb8b9bd);
    t.knobDark = juce::Colour (0xff696b71);
    t.buttonTop = juce::Colour (0xff55585f);
    t.buttonBottom = juce::Colour (0xff3d4046);
    t.text = juce::Colour (0xffeceef1);
    t.textDim = juce::Colour (0xffa6abb4);
    t.textFaint = juce::Colour (0xff6a707a);
    t.accent = getBandColour (kMaxBands - 1);    // the logo mark runs amber -> cyan: the split
    t.accent2 = getBandColour (0);
    t.spectrum = juce::Colour (0xffc2c8d2);
    t.curve = juce::Colour (0xffe9edf2);
    t.danger = juce::Colour (0xffe5554a);
    t.warning = juce::Colour (0xffe9c35c);
}

void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::ColourGradient base (t.displayTop, r.getCentreX(), r.getY(), t.bg0, r.getCentreX(), r.getBottom(), false);
    base.addColour (0.42, t.displayTop.interpolatedWith (t.bg0, 0.55f));
    base.addColour (0.78, t.bg1.interpolatedWith (t.bg0, 0.5f));
    g.setGradientFill (base);
    g.fillRect (r);

    auto pool = [&] (juce::Point<float> c, float radius, juce::Colour col, float alpha)
    {
        juce::ColourGradient rg (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + radius, c.y, true);
        g.setGradientFill (rg);
        g.fillRect (r);
    };
    pool ({ r.getCentreX(), r.getY() - r.getHeight() * 0.12f }, r.getWidth() * 0.6f, t.displayGlow, 0.45f);
    pool ({ r.getX() + r.getWidth() * 0.10f, r.getY() + r.getHeight() * 0.62f }, r.getWidth() * 0.36f, getBandColour (0), 0.07f);
    pool ({ r.getRight() - r.getWidth() * 0.10f, r.getY() + r.getHeight() * 0.42f }, r.getWidth() * 0.36f, getBandColour (kMaxBands - 1), 0.07f);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(), r.getY() + r.getHeight() * 0.42f,
                                   juce::Colours::black.withAlpha (0.42f), r.getX() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.42f, true);
    g.setGradientFill (vignette);
    g.fillRect (r);
}

void paintTray (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = st::Theme::get();
    juce::Path bar;
    bar.addRoundedRectangle (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, r.getHeight() + 20.0f, 16.0f, 16.0f, true, true, false, false);
    g.setGradientFill (juce::ColourGradient (t.bg2.withAlpha (0.92f), 0.0f, r.getY(), t.bg0.withAlpha (0.96f), 0.0f, r.getBottom(), false));
    g.fillPath (bar);
    st::paint::fadedLine (g, { r.getX() + 24.0f, r.getY() + 0.5f }, { r.getRight() - 24.0f, r.getY() + 0.5f },
                          juce::Colours::white.withAlpha (0.10f), 1.0f, 0.18f);
}

void paintGlassBar (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour edge)
{
    const auto& t = st::Theme::get();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (box.translated (0.0f, 3.0f).expanded (2.0f), 9.0f);
    g.setColour (t.glassPanel);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    const auto light = edge.isTransparent() ? juce::Colours::white.withAlpha (0.13f) : edge.withAlpha (0.6f);
    juce::ColourGradient top (light.withAlpha (0.0f), box.getX(), 0.0f, light.withAlpha (0.0f), box.getRight(), 0.0f, false);
    top.addColour (0.25, light);
    top.addColour (0.75, light);
    g.setGradientFill (top);
    g.fillRect (box.getX() + 8.0f, box.getY() + 0.5f, box.getWidth() - 16.0f, 1.0f);
}

} // namespace stsplit::style
