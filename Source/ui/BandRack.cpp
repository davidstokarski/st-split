#include "BandRack.h"
#include "PluginMenu.h"

namespace stsplit
{

namespace
{
    const juce::String dragPrefix = "st-split-slot:";
    juce::String ellipsis() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }
    juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")); }
}

//==============================================================================
SlotRow::SlotRow (BandRack& r, const ChainHost::SlotInfo& i) : rack (r), info (i)
{
    if (info.status == ChainHost::Status::failed)
        setTooltip (info.name + ": " + info.error + ". Its settings are kept; reinstall it and reopen the session.");
    else
        setTooltip ("Click to open " + info.name + ". Drag to reorder or move to the other band, right-click for more.");
}

void SlotRow::resized()
{
    auto r = getLocalBounds().toFloat();
    powerBox = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() });
    removeBox = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ r.getRight() - 14.0f, r.getCentreY() });
    editBox = removeBox.translated (-22.0f, 0.0f);
}

SlotRow::Part SlotRow::partAt (juce::Point<float> p) const
{
    if (powerBox.expanded (5.0f).contains (p)) return Part::power;
    if (isMouseOver (true) && removeBox.expanded (2.0f).contains (p)) return Part::remove;
    if (isMouseOver (true) && editBox.expanded (2.0f).contains (p) && info.status == ChainHost::Status::ready) return Part::edit;
    return Part::body;
}

void SlotRow::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = getBandColour (rack.getBand());
    const bool hover = isMouseOver (true) && ! dragging;
    const bool failed = info.status == ChainHost::Status::failed;
    const bool loading = info.status == ChainHost::Status::loading;
    const bool dim = info.bypassed || failed;
    auto r = getLocalBounds().toFloat();

    style::button (g, r, hover, false, dragging ? t.bg2 : (info.bypassed ? t.bg3 : t.bg4));
    if (editorOpen)
    {
        g.setColour (t.accent);
        g.drawRoundedRectangle (r.reduced (0.5f), style::controlRadius, 1.0f);
    }

    st::icons::draw (g, st::icons::Icon::power, powerBox,
                     info.bypassed ? t.textFaint.brighter (hoverPart == Part::power ? 0.4f : 0.0f)
                                   : colour.brighter (hoverPart == Part::power ? 0.25f : 0.0f), 1.7f);

    auto textArea = r.withTrimmedLeft (32.0f).withTrimmedRight (hover ? 50.0f : 10.0f);
    juce::String trailing;
    if (failed)       trailing = info.error;
    else if (loading) trailing = "Loading" + ellipsis();
    else if (info.latencySamples > 0 && ! hover)
    {
        const double ms = 1000.0 * info.latencySamples / juce::jmax (1.0, rack.getProcessor().getSampleRate());
        trailing = juce::String (ms, ms < 10.0 ? 1 : 0) + " ms";
    }
    if (trailing.isNotEmpty())
    {
        g.setFont (st::fonts::medium (10.5f));
        const float w = juce::jmin (textArea.getWidth() * 0.45f, st::fonts::stringWidth (g.getCurrentFont(), trailing) + 4.0f);
        g.setColour (failed ? t.danger : t.textFaint);
        g.drawText (trailing, textArea.removeFromRight (w), juce::Justification::centredRight, true);
        textArea.removeFromRight (8.0f);
    }

    g.setFont (st::fonts::medium (12.5f));
    const float nameW = juce::jmin (textArea.getWidth(), st::fonts::stringWidth (g.getCurrentFont(), info.name) + 2.0f);
    g.setColour (failed ? t.danger : t.text.withAlpha (dim ? 0.45f : 1.0f));
    g.drawText (info.name, textArea.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    g.setFont (st::fonts::regular (11.0f));
    g.setColour (t.textDim.withAlpha (dim ? 0.5f : 0.8f));
    g.drawText (info.manufacturer + "  " + dot() + "  " + info.format, textArea.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);

    if (hover)
    {
        if (info.status == ChainHost::Status::ready)
            st::icons::draw (g, st::icons::Icon::pencil, editBox.reduced (2.5f), hoverPart == Part::edit ? t.text : t.textDim, 1.5f);
        st::icons::draw (g, st::icons::Icon::close, removeBox.reduced (4.0f), hoverPart == Part::remove ? t.danger : t.textDim, 1.6f);
    }
}

void SlotRow::mouseMove (const juce::MouseEvent& e)
{
    const auto p = partAt (e.position);
    if (p != hoverPart) { hoverPart = p; repaint(); }
}

void SlotRow::mouseDown (const juce::MouseEvent& e)
{
    downPart = partAt (e.position);
    dragging = false;
    if (rack.onSelected) rack.onSelected (rack.getBand());
    if (e.mods.isPopupMenu()) { downPart = Part::none; showContextMenu(); }
}

void SlotRow::mouseDrag (const juce::MouseEvent& e)
{
    if (downPart != Part::body || dragging || e.getDistanceFromDragStart() < 5) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
        image.multiplyAllAlphas (0.9f);
        dragging = true;
        container->startDragging (dragPrefix + info.uid, this, juce::ScaledImage (image, 2.0));
        repaint();
    }
}

void SlotRow::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDragging = dragging;
    dragging = false;
    repaint();
    if (wasDragging || downPart == Part::none || partAt (e.position) != downPart) return;

    auto& chains = rack.getProcessor().getChains();
    const auto uid = info.uid;
    auto safeRack = juce::Component::SafePointer<BandRack> (&rack);
    switch (downPart)
    {
        case Part::power:  juce::MessageManager::callAsync ([&chains, uid, b = ! info.bypassed, safeRack] { if (safeRack != nullptr) chains.setSlotBypassed (uid, b); }); break;
        case Part::remove: juce::MessageManager::callAsync ([&chains, uid, safeRack] { if (safeRack != nullptr) chains.removeSlot (uid); }); break;
        case Part::edit:
        case Part::body:   if (info.status == ChainHost::Status::ready && rack.onOpenEditor) rack.onOpenEditor (uid); break;
        case Part::none:   break;
    }
}

void SlotRow::showContextMenu()
{
    auto& chains = rack.getProcessor().getChains();
    const auto uid = info.uid;
    const int band = rack.getBand();
    auto safeRack = juce::Component::SafePointer<BandRack> (&rack);

    juce::PopupMenu m;
    m.addSectionHeader (info.name);
    m.addItem ("Open editor", info.status == ChainHost::Status::ready, false,
               [safeRack, uid] { if (safeRack != nullptr && safeRack->onOpenEditor) safeRack->onOpenEditor (uid); });
    m.addItem ("Bypass", true, info.bypassed, [&chains, uid, b = ! info.bypassed, safeRack] { if (safeRack != nullptr) chains.setSlotBypassed (uid, b); });
    m.addSeparator();
    for (int other = 0; other < kMaxBands; ++other)
        if (other != band)
            m.addItem ("Move to " + getBandName (other) + " band", chains.canAdd (other), false,
                       [&chains, uid, other, safeRack] { if (safeRack != nullptr) chains.moveSlot (uid, other, kMaxSlots); });
    juce::PopupMenu replace;
    addPluginItems (replace, chains.getCatalog(), [&chains, uid, safeRack] (const juce::PluginDescription& d)
    {
        if (safeRack != nullptr) chains.replacePlugin (uid, d);
    });
    m.addSubMenu ("Replace with", replace);
    m.addSeparator();
    m.addItem ("Remove", [&chains, uid, safeRack] { if (safeRack != nullptr) chains.removeSlot (uid); });
    st::showMenu (m, nullptr, {}, true);
}

//==============================================================================
BandRack::BandRack (StSplitProcessor& p, int b)
    : proc (p), band (b), meter ([&p, b] { return p.getEngine().getBandPeak (b); })
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    const auto colour = getBandColour (band);

    mute = std::make_unique<style::Toggle> (proc.bandParam (band, "mute"), um, "M", t.accent2);
    solo = std::make_unique<style::Toggle> (proc.bandParam (band, "solo"), um, "S", t.warning);
    power = std::make_unique<style::Toggle> (proc.bandParam (band, "bypass"), um, "", colour);
    power->setIcon (st::icons::Icon::power);
    power->setInverted (true);
    mute->setTooltip ("Mute this band");
    solo->setTooltip ("Solo this band");
    power->setTooltip ("Band plug-ins on / off (off passes the band through untouched)");
    for (auto* c : { mute.get(), solo.get(), power.get() }) addAndMakeVisible (*c);

    level = std::make_unique<st::Knob> (proc.bandParam (band, "level"), um, "Level");
    mix = std::make_unique<st::Knob> (proc.bandParam (band, "mix"), um, "Mix");
    for (auto* k : { level.get(), mix.get() })
    {
        k->setSize (st::Knob::Size::medium);
        k->setLook (st::Knob::Look::arc);
        k->setColour (colour);
        addAndMakeVisible (*k);
    }
    level->setBipolar (true);
    level->setTooltip ("Output level of this band");
    mix->setTooltip ("Dry / wet of this band's plug-ins (latency compensated, so partial mixes don't comb)");

    addAndMakeVisible (meter);

    proc.getChains().addListener (this);
    rebuildRows();
    startTimerHz (10);
}

BandRack::~BandRack()
{
    proc.getChains().removeListener (this);
}

void BandRack::setSelected (bool b)
{
    if (selected == b) return;
    selected = b;
    repaint();
}

void BandRack::setOpenEditors (const juce::StringArray& uids)
{
    openEditors = uids;
    for (auto* r : rows) r->setEditorOpen (openEditors.contains (r->getUid()));
}

void BandRack::rebuildRows()
{
    rows.clear();
    for (auto& s : proc.getChains().getSlots (band))
    {
        auto* row = rows.add (new SlotRow (*this, s));
        row->setEditorOpen (openEditors.contains (s.uid));
        addAndMakeVisible (row);
    }
    resized();
    repaint();
}

void BandRack::showAddMenu (int index, juce::Component* target)
{
    if (! proc.getChains().canAdd (band)) return;
    auto safe = juce::Component::SafePointer<BandRack> (this);
    showPluginMenu (proc.getChains().getCatalog(), target, [safe, index] (const juce::PluginDescription& d)
    {
        if (auto* self = safe.getComponent())
        {
            const auto uid = self->proc.getChains().addPlugin (self->band, index, d);
            if (uid.isNotEmpty() && self->onOpenEditor && self->proc.getChains().getInstance (uid) != nullptr)
                self->onOpenEditor (uid);
        }
    }, target == nullptr, "Add to " + getBandName (band));
}

juce::String BandRack::rangeText() const
{
    const auto lo = band > 0 ? st::params::formatHz (proc.getCrossover (band - 1)) : juce::String();
    const auto hi = band < kMaxBands - 1 ? st::params::formatHz (proc.getCrossover (band)) : juce::String();
    if (lo.isEmpty()) return "Below " + hi;
    if (hi.isEmpty()) return "Above " + lo;
    return lo + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + hi;
}

void BandRack::timerCallback()
{
    const float x = band > 0 ? proc.getCrossover (band - 1) : proc.getCrossover (band);
    if (! juce::exactlyEqual (x, lastCrossover))
    {
        lastCrossover = x;
        repaint (header);
    }
}

//==============================================================================
juce::Rectangle<int> BandRack::socketBounds (int index) const
{
    return { list.getX(), list.getY() + index * (slotHeight + slotGap), list.getWidth(), slotHeight };
}

void BandRack::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (headerHeight);
    {
        auto h = header.reduced (10, 7);
        power->setBounds (h.removeFromRight (26));
        h.removeFromRight (6);
        solo->setBounds (h.removeFromRight (22));
        h.removeFromRight (4);
        mute->setBounds (h.removeFromRight (22));
    }

    r.reduce (10, 10);
    side = r.removeFromRight (112);
    r.removeFromRight (10);
    list = r;

    {
        auto s = side.withTrimmedLeft (10);
        meter.setBounds (s.removeFromRight (6).withTrimmedTop (4).withTrimmedBottom (4));
        s.removeFromRight (4);
        const int kh = s.getHeight() / 2;
        level->setBounds (s.removeFromTop (kh).withSizeKeepingCentre (80, 96));
        mix->setBounds (s.withSizeKeepingCentre (80, 96));
    }

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (socketBounds (i));
}

void BandRack::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = getBandColour (band);
    auto bounds = getLocalBounds().toFloat();

    style::panel (g, bounds);

    // Header: colour stripe, name, range (the M / S / on buttons are children)
    {
        juce::Path top;
        auto h = header.toFloat().reduced (1.0f, 0.0f).withTrimmedTop (1.0f);
        top.addRoundedRectangle (h.getX(), h.getY(), h.getWidth(), h.getHeight(), style::panelRadius - 1.0f, style::panelRadius - 1.0f,
                                 true, true, false, false);
        g.setColour (t.bg3);
        g.fillPath (top);
        g.setColour (t.strokeSoft);
        g.fillRect (h.getX(), h.getBottom() - 1.0f, h.getWidth(), 1.0f);
    }
    {
        // Logic track colour stripe on the left edge, full height; brighter when the band is selected.
        juce::Path stripe;
        stripe.addRoundedRectangle (bounds.getX() + 1.0f, bounds.getY() + 1.0f, 4.0f, bounds.getHeight() - 2.0f,
                                    style::panelRadius - 1.0f, style::panelRadius - 1.0f, true, false, true, false);
        g.setColour (colour.withAlpha (selected ? 1.0f : 0.45f));
        g.fillPath (stripe);
    }

    auto h = header.toFloat().withTrimmedLeft (16.0f);
    const auto name = getBandName (band).toUpperCase();
    g.setFont (st::fonts::make (12.0f, st::fonts::Weight::bold, 0.06f));
    const float nameW = st::fonts::stringWidth (g.getCurrentFont(), name) + 10.0f;
    g.setColour (selected ? t.text : t.text.withAlpha (0.85f));
    g.drawText (name, h.removeFromLeft (nameW), juce::Justification::centredLeft, false);
    g.setFont (st::fonts::medium (12.0f));
    g.setColour (t.textDim);
    g.drawText (rangeText(), h.withTrimmedRight (110.0f), juce::Justification::centredLeft, true);

    // Side column divider
    g.setColour (t.strokeSoft);
    g.fillRect ((float) side.getX(), (float) side.getY(), 1.0f, (float) side.getHeight());
    g.setColour (t.highlight);
    g.fillRect ((float) side.getX() + 1.0f, (float) side.getY(), 1.0f, (float) side.getHeight());

    // Empty insert sockets; the first one adds a plug-in.
    const bool canAdd = proc.getChains().canAdd (band);
    for (int i = rows.size(); i < kMaxSlots; ++i)
    {
        auto s = socketBounds (i).toFloat();
        const bool isAdd = canAdd && i == rows.size();
        const bool hover = isAdd && addHover;
        g.setColour (hover ? t.bg3 : t.bg2.darker (0.18f));
        g.fillRoundedRectangle (s, style::controlRadius);
        g.setColour (juce::Colours::black.withAlpha (hover ? 0.3f : 0.14f));
        g.drawRoundedRectangle (s.reduced (0.5f), style::controlRadius, 1.0f);
        if (isAdd)
        {
            const auto c = hover ? t.text : t.textDim;
            st::icons::draw (g, st::icons::Icon::plus, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ s.getX() + 16.0f, s.getCentreY() }), c, 1.6f);
            g.setFont (st::fonts::medium (12.0f));
            g.setColour (c);
            g.drawText (rows.isEmpty() ? "Add plug-in to the " + getBandName (band).toLowerCase() + " band" : juce::String ("Add plug-in"),
                        s.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
        }
    }
}

void BandRack::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    const auto& t = st::Theme::get();
    const float y = (float) socketBounds (dropIndex).getY() - (float) slotGap * 0.5f - 1.0f;
    g.setColour (dropRefused ? t.danger : t.accent);
    g.fillRoundedRectangle ((float) list.getX(), y, (float) list.getWidth(), 2.0f, 1.0f);
}

bool BandRack::isOverAddSocket (juce::Point<int> p) const
{
    return proc.getChains().canAdd (band) && socketBounds (rows.size()).contains (p);
}

void BandRack::mouseMove (const juce::MouseEvent& e)
{
    const bool over = isOverAddSocket (e.getPosition());
    if (over != addHover)
    {
        addHover = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (socketBounds (rows.size()));
    }
}

void BandRack::mouseExit (const juce::MouseEvent&)
{
    if (addHover) { addHover = false; repaint (socketBounds (rows.size())); }
}

void BandRack::mouseDown (const juce::MouseEvent& e)
{
    if (onSelected) onSelected (band);
    if (e.mods.isPopupMenu() && list.contains (e.getPosition()))
        showAddMenu (rows.size(), nullptr);
}

void BandRack::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && isOverAddSocket (e.getPosition()) && isOverAddSocket (e.getMouseDownPosition()))
        showAddMenu (rows.size(), nullptr);
}

//==============================================================================
int BandRack::indexForY (float y) const
{
    int i = 0;
    for (auto* row : rows)
        if (y > (float) row->getBounds().getCentreY()) ++i;
    return i;
}

bool BandRack::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith (dragPrefix);
}

void BandRack::itemDragMove (const SourceDetails& d)
{
    const auto uid = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false);
    const bool fromHere = proc.getChains().findBand (uid) == band;
    const int idx = indexForY (d.localPosition.toFloat().y);
    const bool refused = ! fromHere && ! proc.getChains().canAdd (band);
    if (idx != dropIndex || refused != dropRefused)
    {
        dropIndex = idx;
        dropRefused = refused;
        repaint();
    }
}

void BandRack::itemDragExit (const SourceDetails&)
{
    dropIndex = -1;
    repaint();
}

void BandRack::itemDropped (const SourceDetails& d)
{
    const auto uid = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false);
    const int idx = dropIndex >= 0 ? dropIndex : rows.size();
    dropIndex = -1;
    repaint();
    if (onSelected) onSelected (band);

    auto safe = juce::Component::SafePointer<BandRack> (this);
    juce::MessageManager::callAsync ([safe, uid, idx]
    {
        if (auto* self = safe.getComponent())
            self->proc.getChains().moveSlot (uid, self->band, idx);
    });
}

} // namespace stsplit
