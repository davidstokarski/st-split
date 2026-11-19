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
    const float cx = juce::jlimit (xr.getStart() + 36.0f, juce::jmax (xr.getStart() + 36.0f, xr.getEnd() - 36.0f), xr.getStart() + xr.getLength() * 0.5f);
    return juce::Rectangle<float> (44.0f, 18.0f).withCentre ({ cx, handleY[(size_t) b].current });
}

juce::Rectangle<float> SplitDisplay::gripBounds (int i) const
{
    const float x = xForHz (xoverPos[(size_t) i].current);
    return juce::Rectangle<float> (22.0f, 28.0f).withCentre ({ x, handleTop + (handleBottom - handleTop) * 0.74f });
}

SplitDisplay::Hit SplitDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p)) return {};

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
    const float zeroY = plot.getBottom() - 116.0f, floorY = plot.getBottom() - 26.0f;

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
    juce::Path clip;
    clip.addRoundedRectangle (plot, style::panelRadius);
    g.saveState();
    g.reduceClipRegion (clip);

    style::well (g, plot);
    drawGrid (g);
    drawColumns (g);
    drawResponses (g);
    drawSpectrum (g);
    drawScale (g);
    drawCrossovers (g);
    drawHandles (g);
    drawBandHeaders (g);

    g.restoreState();
    g.setColour (st::Theme::get().strokeSoft);
    g.drawRoundedRectangle (plot.reduced (0.5f), style::panelRadius, 1.0f);
}

void SplitDisplay::drawGrid (juce::Graphics& g)
{
    const float top = plot.getY() + headerHeight, floorY = plot.getBottom() - 26.0f;

    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (float m : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f })
        {
            const float hz = decade * m;
            if (hz < minHz || hz > maxHz) continue;
            const bool major = m == 1.0f;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.075f : 0.03f));
            g.fillRect (std::round (xForHz (hz)), top, 1.0f, floorY - top);
        }

    for (int i = 1; i <= 3; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (plot.getX(), std::round (top + (floorY - top) * (float) i / 4.0f), plot.getWidth(), 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (plot.getX(), floorY, plot.getWidth(), 1.0f);
}

void SplitDisplay::drawColumns (juce::Graphics& g)
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        const float sel = selectAmt[(size_t) b].current, hov = hoverAmt[(size_t) b].current;
        if (sel + hov < 0.01f) continue;
        const auto xr = getBandXRange (b);
        g.setColour (getBandColour (b).withAlpha (0.045f * sel + 0.02f * hov));
        g.fillRect (xr.getStart(), plot.getY() + headerHeight, xr.getLength(), plot.getHeight() - headerHeight);
    }
}

void SplitDisplay::drawResponses (juce::Graphics& g)
{
    const float floorY = plot.getBottom() - 26.0f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto colour = getBandColour (b);
        const float emphasis = 0.65f + 0.35f * juce::jmax (selectAmt[(size_t) b].current, hoverAmt[(size_t) b].current);
        const bool off = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;

        auto fill = responseCache[b];
        fill.lineTo (plot.getRight(), floorY);
        fill.lineTo (plot.getX(), floorY);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha ((off ? 0.04f : 0.16f) * emphasis), 0.0f, floorY - 90.0f,
                                                 colour.withAlpha (0.0f), 0.0f, floorY, false));
        g.fillPath (fill);

        // The stroke fades out towards the floor so the stop band never draws a flat line along the bottom.
        const float a = (off ? 0.25f : 0.9f) * emphasis;
        juce::ColourGradient stroke (colour.withAlpha (a), 0.0f, floorY - 92.0f, colour.withAlpha (0.0f), 0.0f, floorY - 2.0f, false);
        stroke.addColour (0.6, colour.withAlpha (a * 0.6f));
        g.setGradientFill (stroke);
        g.strokePath (responseCache[b], juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }
}

void SplitDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (headerHeight + 6.0f).withTrimmedBottom (26.0f);

    if (analyzerMode == AnalyzerMode::preAndPost)
    {
        auto pre = preSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
        g.setColour (t.textFaint.withAlpha (0.45f));
        g.strokePath (pre, juce::PathStrokeType (1.0f));
    }

    auto fill = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, true);
    g.setGradientFill (juce::ColourGradient (t.spectrum.withAlpha (0.10f), 0.0f, area.getY(),
                                             t.spectrum.withAlpha (0.02f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    auto line = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
    juce::ColourGradient lineGrad (t.spectrum.withAlpha (0.5f), 0.0f, area.getY(),
                                   t.spectrum.withAlpha (0.0f), 0.0f, area.getBottom() - 2.0f, false);
    lineGrad.addColour (0.75, t.spectrum.withAlpha (0.3f));
    g.setGradientFill (lineGrad);
    g.strokePath (line, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
}

void SplitDisplay::drawScale (juce::Graphics& g)
{
    // Ruler strip along the bottom (like Logic's bar ruler), drawn over the band tints.
    const auto& t = st::Theme::get();
    auto ruler = plot.withTop (plot.getBottom() - 26.0f);
    g.setColour (t.bg1);
    g.fillRect (ruler);
    g.setColour (t.strokeSoft);
    g.fillRect (ruler.getX(), ruler.getY(), ruler.getWidth(), 1.0f);
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (std::round (xForHz (hz)), ruler.getY() + 1.0f, 1.0f, 5.0f);
    }
    g.setFont (st::fonts::medium (10.0f));
    g.setColour (t.textFaint);
    const float y = plot.getBottom() - 13.0f;
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ xForHz (hz), y }), juce::Justification::centred, false);
}

void SplitDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const float a = xoverHover[(size_t) i].current;
        const bool dragging = drag.target == Target::crossover && drag.index == i;
        const auto grip = gripBounds (i);
        const float x = std::round (grip.getCentreX()) + 0.5f;

        g.setColour (juce::Colours::white.withAlpha (0.30f + 0.35f * a));
        g.drawVerticalLine ((int) x, plot.getY() + headerHeight, plot.getBottom() - 26.0f);

        style::button (g, grip, a > 0.5f, dragging);
        g.setColour (a > 0.5f ? t.text : t.textDim);
        for (float dx : { -3.0f, 0.0f, 3.0f })
            g.fillRect (grip.getCentreX() + dx - 0.5f, grip.getCentreY() - 6.0f, 1.0f, 12.0f);

        const auto txt = st::params::formatHz (proc.getCrossover (i));
        g.setFont (st::fonts::semibold (11.0f));
        const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 14.0f;
        auto pill = juce::Rectangle<float> (w, 18.0f).withCentre ({ x, grip.getBottom() + 16.0f });
        g.setColour (t.bg3.withAlpha (0.95f));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (t.strokeSoft);
        g.drawRoundedRectangle (pill.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (t.text.withAlpha (0.75f + 0.25f * a));
        g.drawText (txt, pill, juce::Justification::centred, false);
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
        const float y = std::round (handleY[i].current) + 0.5f;
        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
        const auto colour = getBandColour (b);

        g.setColour (colour.withAlpha ((bypassed || muted ? 0.2f : 0.45f) + 0.3f * juce::jmax (sel, hov)));
        g.drawHorizontalLine ((int) y, xr.getStart() + (b == 0 ? 0.0f : 1.0f), xr.getEnd() - (b == kMaxBands - 1 ? 0.0f : 1.0f));

        auto hb = handleBounds (b);
        const bool active = drag.index == b && drag.target == Target::handle;
        style::button (g, hb, hov > 0.5f, active, t.bg4.brighter (0.25f));
        g.setColour (juce::Colour (0xff1a1a1a).withAlpha (0.6f));
        for (float dx : { -4.0f, 0.0f, 4.0f })
            g.fillRect (hb.getCentreX() + dx - 0.5f, hb.getCentreY() - 4.0f, 1.0f, 8.0f);

        const float captionAlpha = juce::jmax (sel * 0.85f, hov, active ? 1.0f : 0.0f);
        if (captionAlpha > 0.02f)
        {
            g.setFont (st::fonts::medium (11.0f));
            g.setColour (t.text.withAlpha (0.8f * captionAlpha));
            g.drawText ("Level " + proc.bandParam (b, "level").getCurrentValueAsText(),
                        hb.withSizeKeepingCentre (160.0f, 16.0f).translated (0.0f, 20.0f), juce::Justification::centred, false);
        }

        if (bypassed || muted)
        {
            g.setFont (st::fonts::caption (9.0f));
            g.setColour (t.textDim);
            g.drawText (bypassed ? "BYPASSED" : "MUTED", hb.withSizeKeepingCentre (90.0f, 14.0f).translated (0.0f, -20.0f),
                        juce::Justification::centred, false);
        }
    }
}

void SplitDisplay::drawBandHeaders (juce::Graphics& g)
{
    // Logic region style: a coloured header strip per band naming its chain.
    const auto& t = st::Theme::get();
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto xr = getBandXRange (b);
        auto strip = juce::Rectangle<float> (xr.getStart(), plot.getY(), xr.getLength(), headerHeight);
        if (b > 0) strip.removeFromLeft (1.0f);
        const float sel = selectAmt[(size_t) b].current;
        const auto fill = t.bg0.interpolatedWith (getBandColour (b), 0.42f + 0.48f * sel);
        g.setColour (fill);
        g.fillRect (strip);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (strip.getX(), strip.getBottom() - 1.0f, strip.getWidth(), 1.0f);

        auto text = strip.reduced (9.0f, 0.0f);
        if (text.getWidth() < 24.0f) continue;
        const auto fg = style::textOn (fill);
        const auto name = getBandName (b).toUpperCase();
        g.setFont (st::fonts::make (11.0f, st::fonts::Weight::bold, 0.06f));
        const float nameW = juce::jmin (text.getWidth(), st::fonts::stringWidth (g.getCurrentFont(), name) + 10.0f);
        g.setColour (fg);
        g.drawText (name, text.removeFromLeft (nameW), juce::Justification::centredLeft, false);
        g.setFont (st::fonts::medium (11.5f));
        g.setColour (fg.withAlpha (0.8f));
        g.drawText (chainSummary[(size_t) b], text, juce::Justification::centredLeft, true);
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
