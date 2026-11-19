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
}

SplitDisplay::SplitDisplay (StSplitProcessor& p)
    : proc (p), preSpectrum (p.preAnalyzer, analyzerOptions()), postSpectrum (p.postAnalyzer, analyzerOptions())
{
    setOpaque (false);
    for (int i = 0; i < kMaxCrossovers; ++i)
        xoverPos[(size_t) i].snap (proc.getCrossover (i));
    proc.getChains().addListener (this);
    chainsChanged();
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

void SplitDisplay::chainsChanged()
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        juce::StringArray names;
        for (auto& s : proc.getChains().getSlots (b))
            names.add (s.name);
        chainSummary[(size_t) b] = names.isEmpty() ? juce::String ("No plug-ins")
                                                   : names.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")));
    }
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

int SplitDisplay::bandAtX (float x) const
{
    for (int b = 0; b < kMaxBands - 1; ++b)
        if (x < getBandXRange (b).getEnd()) return b;
    return kMaxBands - 1;
}

juce::Rectangle<float> SplitDisplay::handleBounds (int b) const
{
    const auto xr = getBandXRange (b);
    // Keep the handle clear of the split grip when a band gets narrow.
    const float cx = juce::jlimit (xr.getStart() + 34.0f, juce::jmax (xr.getStart() + 34.0f, xr.getEnd() - 34.0f), xr.getStart() + xr.getLength() * 0.5f);
    return juce::Rectangle<float> (60.0f, 24.0f).withCentre ({ cx, handleY[(size_t) b].current });
}

juce::Rectangle<float> SplitDisplay::gripBounds (int i) const
{
    const float x = xForHz (xoverPos[(size_t) i].current);
    return juce::Rectangle<float> (26.0f, 30.0f).withCentre ({ x, handleTop + (handleBottom - handleTop) * 0.5f });
}

SplitDisplay::Hit SplitDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p) || p.y < 46.0f) return {};

    for (int b = 0; b < kMaxBands; ++b)
        if (handleBounds (b).expanded (4.0f).contains (p))
            return { Target::handle, b };

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const auto g = gripBounds (i);
        if (g.expanded (3.0f).contains (p) || std::abs (g.getCentreX() - p.x) < 6.0f)
            return { Target::crossover, i };
    }

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
    const float zeroY = plot.getBottom() - 132.0f, floorY = plot.getBottom() - 24.0f;

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
            const float y = zeroY + (floorY - zeroY) * (-db / responseRangeDb);
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
        const bool hov = hover.index == b && (hover.target == Target::handle || hover.target == Target::bandBody);
        hoverAmt[i].setTarget (hov ? 1.0f : 0.0f);
        changed |= hoverAmt[i].tick (dt, 16.0f);
        selectAmt[i].setTarget (b == selected ? 1.0f : 0.0f);
        changed |= selectAmt[i].tick (dt, 10.0f);
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const bool on = (hover.target == Target::crossover && hover.index == i) || (drag.target == Target::crossover && drag.index == i);
        xoverHover[(size_t) i].setTarget (on ? 1.0f : 0.0f);
        changed |= xoverHover[(size_t) i].tick (dt, 18.0f);
        xoverPos[(size_t) i].setTarget (proc.getCrossover (i));
        changed |= xoverPos[(size_t) i].tick (dt, drag.target == Target::crossover ? 80.0f : 20.0f);
    }

    // Response curves follow the (animated) split frequency and slope.
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
    drawBandHeaders (g);
}

void SplitDisplay::drawColumns (juce::Graphics& g)
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto i = (size_t) b;
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;
        if (sel + hov < 0.01f) continue;

        const auto colour = getBandColour (b);
        const auto xr = getBandXRange (b);
        auto col = juce::Rectangle<float> (xr.getStart(), plot.getY(), xr.getLength(), plot.getHeight());

        juce::ColourGradient grad (colour.withAlpha (0.13f * sel + 0.04f * hov), 0.0f, plot.getY(),
                                   colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false);
        grad.addColour (0.5, colour.withAlpha (0.06f * sel + 0.015f * hov));
        g.setGradientFill (grad);
        g.fillRect (col);

        for (int side = 0; side < 2; ++side)
        {
            if ((side == 0 && b == 0) || (side == 1 && b == kMaxBands - 1)) continue;
            const float x = side == 0 ? col.getX() : col.getRight() - 14.0f;
            juce::ColourGradient edge (colour.withAlpha (side == 0 ? 0.10f * sel : 0.0f), x, 0.0f,
                                       colour.withAlpha (side == 0 ? 0.0f : 0.10f * sel), x + 14.0f, 0.0f, false);
            g.setGradientFill (edge);
            g.fillRect (x, plot.getY(), 14.0f, plot.getHeight() * 0.7f);
        }

        juce::ColourGradient topLine (colour.withAlpha (0.0f), col.getX(), 0.0f, colour.withAlpha (0.0f), col.getRight(), 0.0f, false);
        topLine.addColour (0.5, colour.withAlpha (0.35f * sel));
        g.setGradientFill (topLine);
        g.fillRect (col.getX(), plot.getY(), col.getWidth(), 1.0f);
    }
}

void SplitDisplay::drawResponses (juce::Graphics& g)
{
    const float floorY = plot.getBottom() - 24.0f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto colour = getBandColour (b);
        const float emphasis = 0.6f + 0.4f * juce::jmax (selectAmt[(size_t) b].current, hoverAmt[(size_t) b].current);
        const bool off = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;

        auto fill = responseCache[b];
        fill.lineTo (plot.getRight(), floorY);
        fill.lineTo (plot.getX(), floorY);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha ((off ? 0.03f : 0.10f) * emphasis), 0.0f, floorY - 110.0f,
                                                 colour.withAlpha (0.0f), 0.0f, floorY, false));
        g.fillPath (fill);

        // The stroke fades out towards the floor so the stop band never draws a flat line along the bottom.
        const float a = (off ? 0.18f : 0.55f) * emphasis;
        juce::ColourGradient stroke (colour.withAlpha (a), 0.0f, floorY - 108.0f, colour.withAlpha (0.0f), 0.0f, floorY - 2.0f, false);
        stroke.addColour (0.6, colour.withAlpha (a * 0.7f));
        g.setGradientFill (stroke);
        g.strokePath (responseCache[b], juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }
}

void SplitDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (48.0f).withTrimmedBottom (20.0f);

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
    juce::ColourGradient lineGrad (t.spectrum.withAlpha (0.55f), 0.0f, area.getY(),
                                   t.spectrum.withAlpha (0.0f), 0.0f, area.getBottom() - 4.0f, false);
    lineGrad.addColour (0.7, t.spectrum.withAlpha (0.35f));
    g.setGradientFill (lineGrad);
    g.strokePath (line, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
}

void SplitDisplay::drawScale (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    g.setFont (st::fonts::medium (10.0f));
    const float y = plot.getBottom() - 18.0f;
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (t.textFaint.withAlpha (0.8f));
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, y + 7.0f }), juce::Justification::centred, false);
    }
}

void SplitDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const float a = xoverHover[(size_t) i].current;
        const auto grip = gripBounds (i);
        const float x = grip.getCentreX();
        const float top = plot.getY() + 46.0f, bottom = plot.getBottom() - 24.0f;

        juce::Path line, dashed;
        line.startNewSubPath (x, top);
        line.lineTo (x, bottom);
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        const float alpha = 0.36f + 0.4f * a;
        juce::ColourGradient fade (juce::Colours::white.withAlpha (0.0f), x, top, juce::Colours::white.withAlpha (0.0f), x, bottom, false);
        fade.addColour (0.12, juce::Colours::white.withAlpha (alpha));
        fade.addColour (0.8, juce::Colours::white.withAlpha (alpha));
        g.setGradientFill (fade);
        g.fillPath (dashed);

        // grip: dark button with a split glyph (two arrows pointing apart)
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (grip.translated (0.0f, 1.5f), 4.0f);
        g.setColour (t.bg3.brighter (0.15f + 0.25f * a).withAlpha (0.95f));
        g.fillRoundedRectangle (grip, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (grip.reduced (0.5f), 4.0f, 1.0f);

        const auto c = grip.getCentre();
        juce::Path glyph;
        glyph.startNewSubPath (c.x, c.y - 8.0f);
        glyph.lineTo (c.x, c.y + 8.0f);
        glyph.startNewSubPath (c.x - 3.5f, c.y - 3.0f);
        glyph.lineTo (c.x - 7.0f, c.y);
        glyph.lineTo (c.x - 3.5f, c.y + 3.0f);
        glyph.startNewSubPath (c.x + 3.5f, c.y - 3.0f);
        glyph.lineTo (c.x + 7.0f, c.y);
        glyph.lineTo (c.x + 3.5f, c.y + 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f + 0.25f * a));
        g.strokePath (glyph, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // frequency: always shown under the grip, as a bubble while hovered / dragged
        const auto txt = st::params::formatHz (proc.getCrossover (i));
        g.setFont (st::fonts::semibold (11.5f));
        const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 16.0f;
        auto bubble = juce::Rectangle<float> (w, 20.0f).withCentre ({ x, grip.getBottom() + 18.0f });
        g.setColour (t.bg1.withAlpha (0.55f + 0.35f * a));
        g.fillRoundedRectangle (bubble, 4.0f);
        g.setColour (t.text.withAlpha (0.7f + 0.3f * a));
        g.drawText (txt, bubble, juce::Justification::centred, false);
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
            const float a = (bypassed || muted ? 0.10f : 0.22f) + 0.22f * sel + 0.10f * hov;
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
        st::paint::pillHandle (g, hb, hov > 0.5f, active);

        const float captionAlpha = juce::jmax (sel * 0.8f, hov, active ? 1.0f : 0.0f);
        if (captionAlpha > 0.02f)
        {
            g.setFont (st::fonts::medium (11.0f));
            g.setColour (t.text.withAlpha (0.75f * captionAlpha));
            g.drawText ("Level " + proc.bandParam (b, "level").getCurrentValueAsText(),
                        hb.withSizeKeepingCentre (160.0f, 16.0f).translated (0.0f, 22.0f), juce::Justification::centred, false);
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

void SplitDisplay::drawBandHeaders (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto xr = getBandXRange (b);
        auto area = juce::Rectangle<float> (xr.getStart() + 14.0f, 56.0f, juce::jmax (0.0f, xr.getLength() - 28.0f), 36.0f);
        if (area.getWidth() < 40.0f) continue;
        const float sel = selectAmt[(size_t) b].current;
        const auto colour = getBandColour (b);

        const auto name = getBandName (b).toUpperCase();
        g.setFont (st::fonts::caption (11.0f));
        const float nameW = st::fonts::stringWidth (g.getCurrentFont(), name) + 4.0f;
        const float dot = 6.0f;
        const float rowW = juce::jmin (area.getWidth(), dot + 8.0f + nameW);
        auto row = juce::Rectangle<float> (rowW, 16.0f).withCentre ({ area.getCentreX(), area.getY() + 8.0f });
        g.setColour (colour.withAlpha (0.55f + 0.45f * sel));
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre ({ row.getX() + dot * 0.5f, row.getCentreY() }));
        g.setColour (t.text.withAlpha (0.7f + 0.3f * sel));
        g.drawText (name, row.withTrimmedLeft (dot + 8.0f), juce::Justification::centredLeft, false);

        g.setFont (st::fonts::medium (11.5f));
        g.setColour (t.textDim.withAlpha (0.65f + 0.3f * sel));
        g.drawFittedText (chainSummary[(size_t) b], area.withTrimmedTop (18.0f).toNearestInt(), juce::Justification::centredTop, 1, 0.9f);
    }
}

//==============================================================================
void SplitDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);
    if (h.target != hover.target || h.index != hover.index)
    {
        hover = h;
        setMouseCursor (h.target == Target::crossover ? juce::MouseCursor::LeftRightResizeCursor
                      : h.target == Target::handle ? juce::MouseCursor::UpDownResizeCursor
                      : juce::MouseCursor::NormalCursor);
    }
}

void SplitDisplay::mouseExit (const juce::MouseEvent&) { hover = {}; }

void SplitDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTestAt (e.position);
    dragStart = e.position;
    dragParam = nullptr;
    if (drag.target == Target::none) return;

    if (drag.target == Target::crossover)
    {
        proc.undoManager.beginNewTransaction ("Move split");
        dragParam = &proc.param (ids::crossover (drag.index));
        dragParam->beginChangeGesture();
        return;
    }

    if (selected != drag.index)
    {
        selected = drag.index;
        if (onBandSelected) onBandSelected (selected);
    }

    if (e.mods.isPopupMenu())
    {
        const int band = drag.index;
        drag = {};
        showBandMenu (band);
        return;
    }

    proc.undoManager.beginNewTransaction ("Band level");
    dragParam = &proc.bandParam (drag.index, "level");
    dragStartValue = dragParam->convertFrom0to1 (dragParam->getValue());
    dragParam->beginChangeGesture();
}

void SplitDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragParam == nullptr) return;

    if (drag.target == Target::crossover)
    {
        const float x = e.mods.isShiftDown() ? dragStart.x + (e.position.x - dragStart.x) * 0.2f : e.position.x;
        proc.setCrossover (drag.index, hzForX (x), false);
        return;
    }

    float db;
    if (drag.target == Target::handle)
    {
        const float startY = yForLevel (dragStartValue);
        db = levelForY (startY + (e.position.y - dragStart.y) * (e.mods.isShiftDown() ? 0.2f : 1.0f));
    }
    else
        db = dragStartValue + (dragStart.y - e.position.y) * (e.mods.isShiftDown() ? 0.04f : 0.2f);

    dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (juce::jlimit (levelMin, levelMax, db)));
}

void SplitDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragParam != nullptr) dragParam->endChangeGesture();
    dragParam = nullptr;
    drag = {};
}

void SplitDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);
    auto reset = [] (juce::RangedAudioParameter& p)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    };

    if (h.target == Target::handle)             reset (proc.bandParam (h.index, "level"));
    else if (h.target == Target::crossover)     reset (proc.param (ids::crossover (h.index)));
    else if (h.target == Target::bandBody && onAddPluginRequested) onAddPluginRequested (h.index);
}

void SplitDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTestAt (e.position);
    const float dy = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);

    if (h.target == Target::crossover)
    {
        const float hz = proc.getCrossover (h.index) * std::pow (2.0f, dy * (e.mods.isShiftDown() ? 0.05f : 0.3f));
        proc.setCrossover (h.index, hz, true);
        return;
    }
    if (h.target != Target::bandBody && h.target != Target::handle) return;

    auto& p = proc.bandParam (h.index, "level");
    const float db = p.convertFrom0to1 (p.getValue()) + dy * (e.mods.isShiftDown() ? 0.5f : 3.0f);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (levelMin, levelMax, db)));
    p.endChangeGesture();
}

void SplitDisplay::showBandMenu (int band)
{
    juce::PopupMenu m;
    m.addItem ("Add plug-in to " + getBandName (band) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
               proc.getChains().canAdd (band), false, [this, band] { if (onAddPluginRequested) onAddPluginRequested (band); });
    m.addSeparator();
    auto toggle = [this, band] (const char* suffix)
    {
        auto& p = proc.bandParam (band, suffix);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getValue() > 0.5f ? 0.0f : 1.0f);
        p.endChangeGesture();
    };
    auto on = [this, band] (const char* suffix) { return proc.bandParam (band, suffix).getValue() > 0.5f; };
    m.addItem ("Solo", true, on ("solo"), [toggle] { toggle ("solo"); });
    m.addItem ("Mute", true, on ("mute"), [toggle] { toggle ("mute"); });
    m.addItem ("Bypass plug-ins", true, on ("bypass"), [toggle] { toggle ("bypass"); });
    st::showMenu (m, nullptr, {}, true);
}

} // namespace stsplit
