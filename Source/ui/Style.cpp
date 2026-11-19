#include "Style.h"

namespace stsplit::style
{

void strip (juce::Graphics& g, juce::Rectangle<float> r, bool edgeAtBottom)
{
    const auto& t = st::Theme::get();
    g.setColour (t.bg3);
    g.fillRect (r);
    g.setColour (t.strokeSoft);
    g.fillRect (r.getX(), edgeAtBottom ? r.getBottom() - 1.0f : r.getY(), r.getWidth(), 1.0f);
    g.setColour (t.highlight);
    g.fillRect (r.getX(), edgeAtBottom ? r.getY() : r.getY() + 1.0f, r.getWidth(), 1.0f);
}

void panel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const auto& t = st::Theme::get();
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.strokeSoft);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (t.highlight);
    g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - 2.0f * radius, 1.0f);
}

void well (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const auto& t = st::Theme::get();
    g.setColour (t.bg0);
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.strokeSoft);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void button (juce::Graphics& g, juce::Rectangle<float> r, bool hover, bool down, juce::Colour fill)
{
    const auto& t = st::Theme::get();
    auto face = fill.isTransparent() ? t.bg4 : fill;
    if (hover) face = face.brighter (0.10f);
    if (down)  face = face.darker (0.15f);
    g.setColour (face);
    g.fillRoundedRectangle (r, controlRadius);
    g.setColour (juce::Colours::white.withAlpha (fill.isTransparent() ? 0.06f : 0.18f));
    g.fillRect (r.getX() + controlRadius, r.getY() + 0.5f, r.getWidth() - 2.0f * controlRadius, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), controlRadius, 1.0f);
}

juce::Colour textOn (juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1a1a1a) : st::Theme::get().text;
}

//==============================================================================
Toggle::Toggle (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& txt, juce::Colour on)
    : ParameterControl (p, um), text (txt), onColour (on)
{
}

void Toggle::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = isOn();
    const auto fill = on ? onColour : juce::Colour();
    button (g, r, isMouseOver(), pressed, fill);

    const auto fg = on ? textOn (onColour) : (isMouseOver() ? t.text : t.textDim);
    if (icon.has_value())
        st::icons::draw (g, *icon, r.withSizeKeepingCentre (r.getHeight() - 7.0f, r.getHeight() - 7.0f), fg, 1.6f);
    else
    {
        g.setColour (fg);
        g.setFont (st::fonts::bold (11.5f));
        g.drawText (text, r, juce::Justification::centred, false);
    }
}

void Toggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu ({}); return; }
    pressed = true;
    repaint();
}

void Toggle::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed) return;
    pressed = false;
    if (getLocalBounds().contains (e.getPosition()))
        setNormalisedAsGesture (normValue >= 0.5f ? 0.0f : 1.0f);
    repaint();
}

//==============================================================================
Meter::Meter (std::function<float()> peakSource) : source (std::move (peakSource))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Meter::timerCallback()
{
    const float db = juce::Decibels::gainToDecibels (source ? source() : 0.0f, -100.0f);
    const float next = db > levelDb ? db : juce::jmax (db, levelDb - 1.6f);   // ~48 dB/s fall
    if (next >= holdDb) { holdDb = next; holdFrames = 45; }
    else if (--holdFrames < 0) holdDb = juce::jmax (next, holdDb - 1.0f);
    if (std::abs (next - levelDb) > 0.05f || holdFrames >= 0)
    {
        levelDb = next;
        repaint();
    }
}

void Meter::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.bg0);
    g.fillRoundedRectangle (r, 2.0f);

    auto yFor = [&] (float db)
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);   // -60 .. +6 dB
        return r.getBottom() - 1.0f - norm * (r.getHeight() - 2.0f);
    };
    const auto green = t.positive, yellow = t.warning, red = t.danger;
    const float y = yFor (levelDb);
    if (levelDb > -60.0f)
    {
        auto bar = r.reduced (1.0f).withTop (y);
        juce::ColourGradient grad (green, 0.0f, yFor (-60.0f), red, 0.0f, yFor (6.0f), false);
        const double span = 66.0;
        grad.addColour ((-12.0 + 60.0) / span, green);
        grad.addColour ((-6.0 + 60.0) / span, yellow);
        grad.addColour ((-0.5 + 60.0) / span, yellow);
        grad.addColour ((0.0 + 60.0) / span, red);
        g.setGradientFill (grad);
        g.fillRect (bar);
    }
    if (holdDb > -60.0f)
    {
        g.setColour (holdDb > 0.0f ? red : t.text.withAlpha (0.7f));
        g.fillRect (r.getX() + 1.0f, yFor (holdDb), r.getWidth() - 2.0f, 1.0f);
    }
    // 0 dB tick
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (r.getX(), yFor (0.0f), r.getWidth(), 1.0f);
}

} // namespace stsplit::style
