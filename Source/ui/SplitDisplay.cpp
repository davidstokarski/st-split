#include "SplitDisplay.h"

namespace stsplit
{

namespace
{
    st::SpectrumAnalyzer::Options analyzerOptions()
    {
        st::SpectrumAnalyzer::Options o;
        o.fftOrder = 12;
        o.hopSize = 128;
        o.numDisplayPoints = 340;
        o.minHz = 15.0f;
        o.maxHz = 24000.0f;
        o.attack = 0.55f;
        o.releaseDbPerSec = 34.0f;
        o.smoothingPasses = 3;
        return o;
    }

    constexpr float specMinDb = -84.0f, specMaxDb = 0.0f;
    constexpr float responseRangeDb = 30.0f;
    constexpr double momentaryHoldSeconds = 0.35;   // hold S / M longer than this = only while held
    const juce::StringArray slopeNames { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
}

SplitDisplay::SplitDisplay (StSplitProcessor& p)
    : proc (p), preSpectrum (p.preAnalyzer, analyzerOptions()), postSpectrum (p.postAnalyzer, analyzerOptions())
{
    setOpaque (false);
    for (int i = 0; i < kMaxCrossovers; ++i)
        xoverPos[(size_t) i].snap (proc.getCrossover (i));
    proc.getChains().addListener (this);
    clock = std::make_unique<st::FrameClock> (*this, [this] (double dt) { frame (dt); });
}

SplitDisplay::~SplitDisplay()
{
    proc.getChains().removeListener (this);
}

void SplitDisplay::setAnalyzerMode (AnalyzerMode m)
{
    analyzerMode = m;
    proc.preAnalyzer.setActive (m == AnalyzerMode::preAndPost);
    proc.postAnalyzer.setActive (m != AnalyzerMode::off);
    if (m == AnalyzerMode::off) { preSpectrum.clear(); postSpectrum.clear(); }
    repaint();
}

//==============================================================================
float SplitDisplay::xForHz (float hz) const noexcept
{
    return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float SplitDisplay::hzForX (float x) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return minHz * std::pow (maxHz / minHz, t);
}

float SplitDisplay::yForLevel (float db) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - levelMin) / (levelMax - levelMin));
    return handleBottom + (handleTop - handleBottom) * t;
}

float SplitDisplay::levelForY (float y) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (handleBottom - y) / (handleBottom - handleTop));
    return levelMin + (levelMax - levelMin) * t;
}

float SplitDisplay::level (int band) const
{
    return proc.apvts.getRawParameterValue (ids::band (band, "level"))->load();
}

juce::Range<float> SplitDisplay::getBandXRange (int b) const
{
    const float x0 = b == 0 ? plot.getX() : xForHz (xoverPos[(size_t) b - 1].current);
    const float x1 = b >= kMaxBands - 1 ? plot.getRight() : xForHz (xoverPos[(size_t) b].current);
    return { x0, juce::jmax (x0 + 1.0f, x1) };
}

juce::Rectangle<int> SplitDisplay::getStackArea (int band, int stackHeight) const
{
    const auto xr = getBandXRange (band);
    const float w = juce::jlimit (136.0f, 230.0f, xr.getLength() - 32.0f);
    float x = xr.getStart() + (xr.getLength() - w) * 0.5f;
    x = juce::jlimit (plot.getX() + 10.0f, plot.getRight() - 10.0f - w, x);
    return juce::Rectangle<float> (x, stackTop, w, (float) stackHeight).toNearestInt();
}

int SplitDisplay::bandAtX (float x) const
{
    for (int b = 0; b < kMaxBands - 1; ++b)
        if (x < getBandXRange (b).getEnd()) return b;
    return kMaxBands - 1;
}

juce::Rectangle<float> SplitDisplay::handleBounds (int b) const
{
    const auto xr = getBandXRange (b);
    const float cx = juce::jlimit (xr.getStart() + 46.0f, juce::jmax (xr.getStart() + 46.0f, xr.getEnd() - 46.0f), xr.getStart() + xr.getLength() * 0.5f);
    return juce::Rectangle<float> (60.0f, 24.0f).withCentre ({ cx, handleY[(size_t) b].current });
}

juce::Rectangle<float> SplitDisplay::freqPill (int i) const
{
    return juce::Rectangle<float> (64.0f, 19.0f).withCentre ({ xForHz (xoverPos[(size_t) i].current), headerY + 9.5f });
}

juce::Rectangle<float> SplitDisplay::slopeButton (int i) const
{
    return juce::Rectangle<float> (50.0f, 17.0f).withCentre ({ xForHz (xoverPos[(size_t) i].current), headerY + 32.0f });
}

juce::Rectangle<float> SplitDisplay::headerLabel (int b) const
{
    // Left-aligned in the column, clear of the frequency pills on its splits.
    const auto xr = getBandXRange (b);
    const float x0 = xr.getStart() + (b > 0 ? 40.0f : 16.0f);
    const float x1 = xr.getEnd() - (b < kMaxBands - 1 ? 40.0f : 16.0f);
    return { x0, headerY, juce::jmax (0.0f, x1 - x0), 19.0f };
}

juce::Rectangle<float> SplitDisplay::muteButton (int b) const
{
    const auto label = headerLabel (b);
    return juce::Rectangle<float> (19.0f, 16.0f).withPosition (label.getRight() - 19.0f, label.getY() + 1.5f);
}

juce::Rectangle<float> SplitDisplay::soloButton (int b) const
{
    return muteButton (b).translated (-23.0f, 0.0f);
}

bool SplitDisplay::showsBandButtons (int b) const
{
    return headerLabel (b).getWidth() >= 150.0f;
}

SplitDisplay::Hit SplitDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p) || p.y < 44.0f) return {};

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        if (freqPill (i).expanded (2.0f).contains (p))   return { Target::crossover, i };
        if (slopeButton (i).expanded (2.0f).contains (p)) return { Target::slope, i };
    }

    for (int b = 0; b < kMaxBands; ++b)
    {
        if (handleBounds (b).expanded (4.0f).contains (p)) return { Target::handle, b };
        if (showsBandButtons (b))
        {
            const bool visible = hoverAmt[(size_t) b].current > 0.05f
                              || proc.apvts.getRawParameterValue (ids::band (b, "solo"))->load() > 0.5f
                              || proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
            if (visible && soloButton (b).expanded (2.0f).contains (p)) return { Target::solo, b };
            if (visible && muteButton (b).expanded (2.0f).contains (p)) return { Target::mute, b };
        }
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
        if (std::abs (xForHz (xoverPos[(size_t) i].current) - p.x) < 6.0f && p.y < floorY())
            return { Target::crossover, i };

    return { Target::bandBody, bandAtX (p.x) };
}

//==============================================================================
void SplitDisplay::resized()
{
    plot = getLocalBounds().toFloat();
    for (int b = 0; b < kMaxBands; ++b)
        handleY[(size_t) b].snap (yForLevel (level (b)));
    cachedSlope = -1.0f;
}

void SplitDisplay::rebuildResponses()
{
    const auto& xo = proc.getEngine().getCrossover();
    const float floor = floorY(), zeroY = floor - 150.0f;

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& path = responseCache[b];
        path.clear();
        const int points = 220;
        for (int k = 0; k <= points; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) points;
            const float mag = xo.getBandMagnitude (b, hzForX (x));
            const float db = juce::jlimit (-responseRangeDb, 0.0f, juce::Decibels::gainToDecibels (mag, -100.0f));
            const float y = zeroY + (floor - zeroY) * (-db / responseRangeDb);
            if (k == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
    }
}

void SplitDisplay::frame (double dt)
{
    bool changed = false;

    if (analyzerMode != AnalyzerMode::off)
    {
        changed |= postSpectrum.update (dt);
        if (analyzerMode == AnalyzerMode::preAndPost) changed |= preSpectrum.update (dt);
    }

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto i = (size_t) b;
        handleY[i].setTarget (yForLevel (level (b)));
        changed |= handleY[i].tick (dt, drag.index == b && drag.target == Target::handle ? 60.0f : 18.0f);
        hoverAmt[i].setTarget (hoverBand == b && hover.target != Target::none ? 1.0f : 0.0f);
        changed |= hoverAmt[i].tick (dt, 16.0f);
        selectAmt[i].setTarget (b == selected ? 1.0f : 0.0f);
        changed |= selectAmt[i].tick (dt, 10.0f);
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const bool on = ((hover.target == Target::crossover || hover.target == Target::slope) && hover.index == i)
                     || (drag.target == Target::crossover && drag.index == i);
        xoverHover[(size_t) i].setTarget (on ? 1.0f : 0.0f);
        changed |= xoverHover[(size_t) i].tick (dt, 18.0f);
        xoverPos[(size_t) i].setTarget (proc.getCrossover (i));
        changed |= xoverPos[(size_t) i].tick (dt, drag.target == Target::crossover || dragAxis == Axis::horizontal ? 80.0f : 20.0f);
    }

    const float slope = proc.apvts.getRawParameterValue (ids::slope)->load();
    bool responseDirty = ! juce::exactlyEqual (slope, cachedSlope);
    for (int i = 0; i < kMaxCrossovers; ++i)
        responseDirty |= std::abs (cachedXover[(size_t) i] - proc.getCrossover (i)) > 1.0e-3f;
    if (responseDirty)
    {
        cachedSlope = slope;
        for (int i = 0; i < kMaxCrossovers; ++i) cachedXover[(size_t) i] = proc.getCrossover (i);
        rebuildResponses();
        changed = true;
    }

    sinceRepaint += dt;
    if (drag.target != Target::none) changed = true;
    if (onFrame) onFrame (dt);

    if (changed || sinceRepaint > 0.25)
    {
        sinceRepaint = 0.0;
        repaint();
    }
}

//==============================================================================
void SplitDisplay::paint (juce::Graphics& g)
{
    drawColumns (g);
    drawResponses (g);
    drawSpectrum (g);
    drawScale (g);
    drawCrossovers (g);
    drawHandles (g);
    drawHeaders (g);
}

void SplitDisplay::drawColumns (juce::Graphics& g)
{
    // The selected band is lit from the top in its colour, with soft inner edges that fade out downwards.
    for (int b = 0; b < kMaxBands; ++b)
    {
        const float sel = selectAmt[(size_t) b].current, hov = hoverAmt[(size_t) b].current;
        if (sel + hov < 0.01f) continue;

        const auto colour = getBandColour (b);
        const auto xr = getBandXRange (b);
        auto col = juce::Rectangle<float> (xr.getStart(), plot.getY(), xr.getLength(), floorY() - plot.getY());

        juce::ColourGradient grad (colour.withAlpha (0.13f * sel + 0.035f * hov), 0.0f, plot.getY(),
                                   colour.withAlpha (0.0f), 0.0f, col.getBottom(), false);
        grad.addColour (0.45, colour.withAlpha (0.045f * sel + 0.012f * hov));
        g.setGradientFill (grad);
        g.fillRect (col);

        for (int side = 0; side < 2; ++side)
        {
            if ((side == 0 && b == 0) || (side == 1 && b == kMaxBands - 1)) continue;
            const float x = side == 0 ? col.getX() : col.getRight() - 14.0f;
            const float height = col.getHeight() * 0.75f;
            constexpr int slices = 16;
            for (int k = 0; k < slices; ++k)
            {
                const float a = 0.10f * sel * (1.0f - (float) k / (float) slices);
                juce::ColourGradient edge (colour.withAlpha (side == 0 ? a : 0.0f), x, 0.0f,
                                           colour.withAlpha (side == 0 ? 0.0f : a), x + 14.0f, 0.0f, false);
                g.setGradientFill (edge);
                g.fillRect (x, plot.getY() + height * (float) k / (float) slices, 14.0f, height / (float) slices + 0.5f);
            }
        }

        juce::ColourGradient topLine (colour.withAlpha (0.0f), col.getX(), 0.0f, colour.withAlpha (0.0f), col.getRight(), 0.0f, false);
        topLine.addColour (0.5, colour.withAlpha (0.30f * sel));
        g.setGradientFill (topLine);
        g.fillRect (col.getX(), plot.getY(), col.getWidth(), 1.0f);
    }
}

void SplitDisplay::drawResponses (juce::Graphics& g)
{
    const float floor = floorY();
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto colour = getBandColour (b);
        const float emphasis = 0.6f + 0.4f * juce::jmax (selectAmt[(size_t) b].current, hoverAmt[(size_t) b].current);
        const bool off = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;

        auto fill = responseCache[b];
        fill.lineTo (plot.getRight(), floor);
        fill.lineTo (plot.getX(), floor);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha ((off ? 0.03f : 0.08f) * emphasis), 0.0f, floor - 150.0f,
                                                 colour.withAlpha (0.0f), 0.0f, floor, false));
        g.fillPath (fill);

        const float a = (off ? 0.16f : 0.45f) * emphasis;
        juce::ColourGradient stroke (colour.withAlpha (a), 0.0f, floor - 150.0f, colour.withAlpha (0.0f), 0.0f, floor - 4.0f, false);
        stroke.addColour (0.6, colour.withAlpha (a * 0.6f));
        g.setGradientFill (stroke);
        g.strokePath (responseCache[b], juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }
}

void SplitDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (48.0f).withBottom (floorY());

    if (analyzerMode == AnalyzerMode::preAndPost)
    {
        auto pre = preSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
        g.setGradientFill (juce::ColourGradient (t.textDim.withAlpha (0.25f), 0.0f, area.getY(),
                                                 t.textDim.withAlpha (0.0f), 0.0f, area.getBottom() - 10.0f, false));
        g.strokePath (pre, juce::PathStrokeType (1.0f));
    }

    auto fill = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, true);
    g.setGradientFill (juce::ColourGradient (t.spectrum.withAlpha (0.06f), 0.0f, area.getY(),
                                             t.spectrum.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    auto line = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
    juce::ColourGradient lineGrad (t.spectrum.withAlpha (0.5f), 0.0f, area.getY(),
                                   t.spectrum.withAlpha (0.0f), 0.0f, area.getBottom() - 4.0f, false);
    lineGrad.addColour (0.7, t.spectrum.withAlpha (0.32f));
    g.setGradientFill (lineGrad);
    g.strokePath (line, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
}

void SplitDisplay::drawScale (juce::Graphics& g)
{
    // Frequency scale along the bottom of the display (Pro-MB / Pro-Q), above the footer.
    const auto& t = st::Theme::get();
    const float y = floorY();
    st::paint::fadedLine (g, { plot.getX(), y + 0.5f }, { plot.getRight(), y + 0.5f }, juce::Colours::white.withAlpha (0.07f), 1.0f, 0.1f);
    g.setFont (st::fonts::medium (10.0f));
    g.setColour (t.textFaint);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, scaleHeight).withCentre ({ xForHz (hz), y + scaleHeight * 0.5f }),
                    juce::Justification::centred, false);
}

void SplitDisplay::drawHeaders (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto label = headerLabel (b);
        if (label.getWidth() < 30.0f) continue;
        const float sel = selectAmt[(size_t) b].current, hov = hoverAmt[(size_t) b].current;
        const auto colour = getBandColour (b);

        const bool solo = proc.apvts.getRawParameterValue (ids::band (b, "solo"))->load() > 0.5f;
        const bool mute = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
        const float buttonsAlpha = showsBandButtons (b) ? juce::jmax (hov, solo || mute ? 1.0f : 0.0f) : 0.0f;
        auto text = label;
        if (showsBandButtons (b)) text.setRight (soloButton (b).getX() - 8.0f);

        const auto name = getBandName (b).toUpperCase();
        g.setFont (st::fonts::caption (11.0f));
        const float nameW = st::fonts::stringWidth (g.getCurrentFont(), name) + 6.0f;
        g.setColour (colour.withAlpha (0.7f + 0.3f * sel));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ text.getX() + 3.0f, text.getCentreY() }));
        text.removeFromLeft (13.0f);
        g.setColour (t.text.withAlpha (0.75f + 0.25f * juce::jmax (sel, hov)));
        g.drawText (name, text.removeFromLeft (nameW), juce::Justification::centredLeft, false);

        const auto lo = b > 0 ? st::params::formatHz (proc.getCrossover (b - 1)) : juce::String();
        const auto hi = b < kMaxBands - 1 ? st::params::formatHz (proc.getCrossover (b)) : juce::String();
        g.setFont (st::fonts::medium (11.5f));
        g.setColour (t.textDim.withAlpha (0.7f + 0.3f * juce::jmax (sel, hov)));
        g.drawText (lo.isEmpty() ? "below " + hi : hi.isEmpty() ? "above " + lo : lo + " - " + hi,
                    text.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);

        if (buttonsAlpha > 0.02f)
        {
            auto button = [&] (juce::Rectangle<float> r, const char* txt, bool on, juce::Colour onColour, bool hot)
            {
                const auto face = on ? onColour : t.bg4.brighter (hot ? 0.25f : 0.1f);
                g.setColour (face.withAlpha (buttonsAlpha * 0.95f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.12f * buttonsAlpha));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
                g.setFont (st::fonts::bold (10.5f));
                g.setColour ((on ? juce::Colour (0xff151515) : t.text).withAlpha (buttonsAlpha));
                g.drawText (txt, r, juce::Justification::centred, false);
            };
            button (soloButton (b), "S", solo, t.warning, hover.target == Target::solo && hover.index == b);
            button (muteButton (b), "M", mute, t.danger, hover.target == Target::mute && hover.index == b);
        }
    }
}

void SplitDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const float a = xoverHover[(size_t) i].current;
        const float x = xForHz (xoverPos[(size_t) i].current);
        const auto pill = freqPill (i), slope = slopeButton (i);
        const float top = slope.getBottom() + 4.0f, bottom = floorY() - 2.0f;

        // dashed split line, fading out towards the scale
        juce::Path line, dashed;
        line.startNewSubPath (x, top);
        line.lineTo (x, bottom);
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        const float alpha = 0.34f + 0.4f * a;
        juce::ColourGradient fade (juce::Colours::white.withAlpha (alpha), x, top, juce::Colours::white.withAlpha (0.0f), x, bottom, false);
        fade.addColour (0.8, juce::Colours::white.withAlpha (alpha));
        g.setGradientFill (fade);
        g.fillPath (dashed);

        // frequency pill: the split's handle (drag, double-click to type, Cmd-click to reset)
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (pill.translated (0.0f, 1.5f), 5.0f);
        g.setColour (t.bg4.brighter (0.15f + 0.25f * a));
        g.fillRoundedRectangle (pill, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f + 0.1f * a));
        g.drawRoundedRectangle (pill.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (st::fonts::semibold (11.5f));
        g.setColour (t.text);
        g.drawText (st::params::formatHz (proc.getCrossover (i)), pill, juce::Justification::centred, false);

        // slope button, in the split (Saturn / Pro-MB)
        const bool slopeHot = hover.target == Target::slope && hover.index == i;
        const int slopeIndex = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (ids::slope)->load());
        g.setColour (t.bg1.withAlpha (0.85f));
        g.fillRoundedRectangle (slope, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (slopeHot ? 0.25f : 0.1f));
        g.drawRoundedRectangle (slope.reduced (0.5f), 4.0f, 1.0f);
        g.setFont (st::fonts::medium (10.5f));
        g.setColour (slopeHot ? t.text : t.textDim);
        g.drawText (juce::String ((slopeIndex + 1) * 12) + " dB", slope, juce::Justification::centred, false);
    }
}

void SplitDisplay::drawHandles (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto i = (size_t) b;
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;
        const auto xr = getBandXRange (b);
        const float y = handleY[i].current;
        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;

        {
            const float a = (bypassed || muted ? 0.10f : 0.2f) + 0.22f * sel + 0.10f * hov;
            const bool openLeft = b == 0, openRight = b == kMaxBands - 1;
            juce::ColourGradient line (juce::Colours::white.withAlpha (openLeft ? 0.0f : a * 0.25f), xr.getStart(), y,
                                       juce::Colours::white.withAlpha (openRight ? 0.0f : a * 0.25f), xr.getEnd(), y, false);
            line.addColour (0.18, juce::Colours::white.withAlpha (a));
            line.addColour (0.82, juce::Colours::white.withAlpha (a));
            g.setGradientFill (line);
            g.fillRect (xr.getStart() + 1.0f, y - 0.5f, xr.getLength() - 2.0f, 1.0f);
        }

        auto hb = handleBounds (b);
        const bool active = drag.index == b && drag.target == Target::handle;
        st::paint::pillHandle (g, hb, hov > 0.5f || sel > 0.5f, active);   // the selected band's button is lit

        if (active || (hover.target == Target::handle && hover.index == b))
        {
            const auto txt = active && dragAxis == Axis::horizontal ? st::params::formatHz (proc.getCrossover (juce::jmin (b, kMaxCrossovers - 1)))
                                                                    : proc.bandParam (b, "level").getCurrentValueAsText();
            g.setFont (st::fonts::semibold (11.0f));
            const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 14.0f;
            auto bubble = juce::Rectangle<float> (w, 18.0f).withCentre ({ hb.getCentreX(), hb.getBottom() + 14.0f });
            g.setColour (t.bg0.withAlpha (0.85f));
            g.fillRoundedRectangle (bubble, 4.0f);
            g.setColour (t.text);
            g.drawText (txt, bubble, juce::Justification::centred, false);
        }

        if (bypassed || muted)
        {
            g.setFont (st::fonts::caption (9.0f));
            g.setColour (t.textFaint);
            g.drawText (bypassed ? "BYPASSED" : "MUTED", hb.withSizeKeepingCentre (90.0f, 14.0f).translated (0.0f, -22.0f),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void SplitDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);
    hoverBand = h.target == Target::none ? -1 : bandAtX (e.position.x);
    if (h.target != hover.target || h.index != hover.index)
    {
        hover = h;
        setMouseCursor (h.target == Target::crossover ? juce::MouseCursor::LeftRightResizeCursor
                      : h.target == Target::handle ? juce::MouseCursor::DraggingHandCursor
                      : h.target == Target::slope || h.target == Target::solo || h.target == Target::mute ? juce::MouseCursor::PointingHandCursor
                      : juce::MouseCursor::NormalCursor);
    }
}

void SplitDisplay::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    hoverBand = -1;
}

void SplitDisplay::resetParam (juce::RangedAudioParameter& p)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.getDefaultValue());
    p.endChangeGesture();
}

void SplitDisplay::toggleBandFlag (int band, const char* suffix, bool exclusive)
{
    auto& p = proc.bandParam (band, suffix);
    const bool turnOn = p.getValue() < 0.5f || exclusive;
    proc.undoManager.beginNewTransaction (juce::String (suffix).substring (0, 1).toUpperCase() + juce::String (suffix).substring (1));
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& q = proc.bandParam (b, suffix);
        const float v = b == band ? (turnOn ? 1.0f : 0.0f) : (exclusive ? 0.0f : q.getValue());
        if (! juce::exactlyEqual (q.getValue(), v))
        {
            q.beginChangeGesture();
            q.setValueNotifyingHost (v);
            q.endChangeGesture();
        }
    }
}

void SplitDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTestAt (e.position);
    dragStart = e.position;
    dragParam = nullptr;
    dragAxis = Axis::undecided;
    if (drag.target == Target::none) return;

    if (drag.target != Target::crossover && drag.target != Target::slope && selected != drag.index)
    {
        selected = drag.index;
        if (onBandSelected) onBandSelected (selected);
    }

    if (e.mods.isPopupMenu())
    {
        const int b = bandAtX (e.position.x);
        drag = {};
        showBandMenu (b);
        return;
    }

    switch (drag.target)
    {
        case Target::solo:
        case Target::mute:
            // click toggles; held longer than a moment it only applies while held (Saturn / Pro-MB)
            toggleBandFlag (drag.index, drag.target == Target::solo ? "solo" : "mute", e.mods.isCommandDown());
            pressTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
            pressToggled = true;
            return;

        case Target::slope:
            return;   // opens on mouse up

        case Target::crossover:
            if (e.mods.isCommandDown()) { resetParam (proc.param (ids::crossover (drag.index))); drag = {}; return; }
            proc.undoManager.beginNewTransaction ("Move split");
            dragParam = &proc.param (ids::crossover (drag.index));
            dragParam->beginChangeGesture();
            return;

        case Target::handle:
            if (e.mods.isCommandDown()) { resetParam (proc.bandParam (drag.index, "level")); drag = {}; return; }
            [[fallthrough]];
        case Target::bandBody:
            proc.undoManager.beginNewTransaction ("Band level");
            dragParam = &proc.bandParam (drag.index, "level");
            dragStartValue = dragParam->convertFrom0to1 (dragParam->getValue());
            for (int i = 0; i < kMaxCrossovers; ++i) dragStartXover[(size_t) i] = proc.getCrossover (i);
            dragParam->beginChangeGesture();
            return;

        case Target::none:
            return;
    }
}

void SplitDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragParam == nullptr) return;
    const bool fine = e.mods.isShiftDown();

    if (drag.target == Target::crossover)
    {
        const float x = fine ? dragStart.x + (e.position.x - dragStart.x) * 0.2f : e.position.x;
        proc.setCrossover (drag.index, hzForX (x), false);
        return;
    }

    const auto delta = e.position - dragStart;
    if (drag.target == Target::handle && dragAxis == Axis::undecided && delta.getDistanceFromOrigin() > 4.0f)
    {
        dragAxis = std::abs (delta.x) > std::abs (delta.y) ? Axis::horizontal : Axis::vertical;
        if (dragAxis == Axis::horizontal)
        {
            // sideways moves the band's split(s): restore the level and switch the gesture over
            dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (dragStartValue));
            dragParam->endChangeGesture();
            dragParam = &proc.param (ids::crossover (juce::jmin (drag.index, kMaxCrossovers - 1)));
            dragParam->beginChangeGesture();
            proc.undoManager.beginNewTransaction ("Move split");
        }
    }

    if (dragAxis == Axis::horizontal)
    {
        const float octaves = (fine ? 0.2f : 1.0f) * delta.x / plot.getWidth() * std::log2 (maxHz / minHz);
        const float ratio = std::pow (2.0f, octaves);
        for (int i : { drag.index - 1, drag.index })
            if (juce::isPositiveAndBelow (i, kMaxCrossovers))
                proc.setCrossover (i, dragStartXover[(size_t) i] * ratio, false);
        return;
    }

    float db;
    if (drag.target == Target::handle)
        db = levelForY (yForLevel (dragStartValue) + delta.y * (fine ? 0.2f : 1.0f));
    else
        db = dragStartValue - delta.y * (fine ? 0.04f : 0.2f);
    dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (juce::jlimit (levelMin, levelMax, db)));
}

void SplitDisplay::mouseUp (const juce::MouseEvent& e)
{
    if ((drag.target == Target::solo || drag.target == Target::mute) && pressToggled)
    {
        // held: momentary, so undo the toggle on release (not after an exclusive Cmd-click)
        if (juce::Time::getMillisecondCounterHiRes() * 0.001 - pressTime > momentaryHoldSeconds && ! e.mods.isCommandDown())
            toggleBandFlag (drag.index, drag.target == Target::solo ? "solo" : "mute", false);
        pressToggled = false;
    }
    else if (drag.target == Target::slope && slopeButton (drag.index).expanded (2.0f).contains (e.position))
        showSlopeMenu();

    if (dragParam != nullptr) dragParam->endChangeGesture();
    dragParam = nullptr;
    drag = {};
    dragAxis = Axis::undecided;
}

void SplitDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);
    if (h.target == Target::handle)          promptLevel (h.index);
    else if (h.target == Target::crossover)  promptFrequency (h.index);
    else if (h.target == Target::bandBody && onAddPluginRequested) onAddPluginRequested (h.index);
}

void SplitDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTestAt (e.position);
    const float dy = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);

    if (h.target == Target::crossover)
    {
        proc.setCrossover (h.index, proc.getCrossover (h.index) * std::pow (2.0f, dy * (e.mods.isShiftDown() ? 0.05f : 0.3f)), true);
        return;
    }
    if (h.target != Target::bandBody && h.target != Target::handle) return;

    auto& p = proc.bandParam (h.index, "level");
    const float db = p.convertFrom0to1 (p.getValue()) + dy * (e.mods.isShiftDown() ? 0.5f : 3.0f);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (levelMin, levelMax, db)));
    p.endChangeGesture();
}

//==============================================================================
void SplitDisplay::promptLevel (int band)
{
    if (auto* parent = getParentComponent())
        st::TextPrompt::show (*parent, getBandName (band) + " level", proc.bandParam (band, "level").getCurrentValueAsText(), "Set",
                              [safe = juce::Component::SafePointer<SplitDisplay> (this), band] (const juce::String& s)
        {
            if (safe == nullptr) return;
            auto& prm = safe->proc.bandParam (band, "level");
            prm.beginChangeGesture();
            prm.setValueNotifyingHost (prm.convertTo0to1 (juce::jlimit (levelMin, levelMax, st::params::parseDb (s))));
            prm.endChangeGesture();
        });
}

void SplitDisplay::promptFrequency (int split)
{
    if (auto* parent = getParentComponent())
        st::TextPrompt::show (*parent, "Split frequency", st::params::formatHz (proc.getCrossover (split)), "Set",
                              [safe = juce::Component::SafePointer<SplitDisplay> (this), split] (const juce::String& s)
        {
            const float hz = st::params::parseHz (s);
            if (safe != nullptr && hz > 0.0f) safe->proc.setCrossover (split, hz, true);
        });
}

void SplitDisplay::showSlopeMenu()
{
    auto& p = proc.param (ids::slope);
    const int current = juce::jlimit (0, 2, juce::roundToInt (p.convertFrom0to1 (p.getValue())));
    juce::PopupMenu m;
    m.addSectionHeader ("Split slope");
    for (int i = 0; i < slopeNames.size(); ++i)
        m.addItem (slopeNames[i], true, i == current, [safe = juce::Component::SafePointer<SplitDisplay> (this), i]
        {
            if (safe == nullptr) return;
            auto& prm = safe->proc.param (ids::slope);
            prm.beginChangeGesture();
            prm.setValueNotifyingHost (prm.convertTo0to1 ((float) i));
            prm.endChangeGesture();
        });
    st::showMenu (m, nullptr, {}, true);
}

void SplitDisplay::showBandMenu (int band)
{
    auto safe = juce::Component::SafePointer<SplitDisplay> (this);
    juce::PopupMenu m;
    m.addItem ("Add plug-in to " + getBandName (band) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
               proc.getChains().canAdd (band), false, [safe, band] { if (safe != nullptr && safe->onAddPluginRequested) safe->onAddPluginRequested (band); });
    m.addSeparator();
    auto on = [this, band] (const char* suffix) { return proc.bandParam (band, suffix).getValue() > 0.5f; };
    m.addItem ("Solo", true, on ("solo"), [safe, band] { if (safe != nullptr) safe->toggleBandFlag (band, "solo", false); });
    m.addItem ("Mute", true, on ("mute"), [safe, band] { if (safe != nullptr) safe->toggleBandFlag (band, "mute", false); });
    m.addItem ("Bypass plug-ins", true, on ("bypass"), [safe, band] { if (safe != nullptr) safe->toggleBandFlag (band, "bypass", false); });
    st::showMenu (m, nullptr, {}, true);
}

} // namespace stsplit
