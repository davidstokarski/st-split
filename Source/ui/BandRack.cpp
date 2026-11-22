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
    powerBox = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() - 1.0f });
    removeBox = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ r.getRight() - 14.0f, r.getCentreY() - 1.0f });
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
    const bool dim = info.bypassed || failed || dragging;
    auto r = getLocalBounds().toFloat();

    // Grey hardware face, like the family's bar dropdowns; bypassed slots sink back.
    st::paint::greyButton (g, r, t.radius, hover, false);
    auto face = r.withTrimmedBottom (2.0f).reduced (0.5f, 0.0f);
    if (dim)
    {
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (face, t.radius);
    }
    if (editorOpen)
    {
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (face.withTop (face.getBottom() - 2.5f).reduced (6.0f, 0.0f), 1.0f);
    }

    st::icons::draw (g, st::icons::Icon::power, powerBox,
                     info.bypassed ? t.text.withAlpha (hoverPart == Part::power ? 0.7f : 0.4f)
                                   : colour.brighter (hoverPart == Part::power ? 0.3f : 0.1f), 1.7f);

    auto textArea = face.withTrimmedLeft (32.0f).withTrimmedRight (hover ? 50.0f : 10.0f);
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
        g.setColour (failed ? t.danger.brighter (0.2f) : t.text.withAlpha (0.55f));
        g.drawText (trailing, textArea.removeFromRight (w), juce::Justification::centredRight, true);
        textArea.removeFromRight (8.0f);
    }

    g.setFont (st::fonts::medium (12.5f));
    const float nameW = juce::jmin (textArea.getWidth(), st::fonts::stringWidth (g.getCurrentFont(), info.name) + 2.0f);
    g.setColour (failed ? t.danger.brighter (0.25f) : t.text.withAlpha (dim ? 0.5f : 0.95f));
    g.drawText (info.name, textArea.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    g.setFont (st::fonts::regular (11.0f));
    g.setColour (t.text.withAlpha (dim ? 0.3f : 0.5f));
    g.drawText (info.manufacturer + "  " + dot() + "  " + info.format, textArea.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);

    if (hover)
    {
        if (info.status == ChainHost::Status::ready)
            st::icons::draw (g, st::icons::Icon::pencil, editBox.reduced (2.5f), hoverPart == Part::edit ? t.text : t.text.withAlpha (0.6f), 1.5f);
        st::icons::draw (g, st::icons::Icon::close, removeBox.reduced (4.0f), hoverPart == Part::remove ? t.danger.brighter (0.3f) : t.text.withAlpha (0.6f), 1.6f);
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
BandRack::BandRack (StSplitProcessor& p, int b) : proc (p), band (b)
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    const auto colour = getBandColour (band);

    // Grey buttons on the bar's top edge, like ST Sat's band bar: on / solo / mute.
    power = std::make_unique<st::ToggleChip> (proc.bandParam (band, "bypass"), um, "", colour);
    power->setIcon (st::icons::Icon::power);
    power->setInverted (true);
    solo = std::make_unique<st::ToggleChip> (proc.bandParam (band, "solo"), um, "S", t.warning);
    mute = std::make_unique<st::ToggleChip> (proc.bandParam (band, "mute"), um, "M", t.danger);
    for (auto* c : { power.get(), solo.get(), mute.get() })
    {
        c->setGreyStyle (true);
        addAndMakeVisible (*c);
    }
    power->setTooltip ("Band plug-ins on / off (off passes the band through untouched)");
    solo->setTooltip ("Solo this band");
    mute->setTooltip ("Mute this band");

    level = std::make_unique<st::Knob> (proc.bandParam (band, "level"), um, "Level");
    level->setSize (st::Knob::Size::medium);
    level->setLook (st::Knob::Look::ringed);
    level->setColour (colour);
    level->setBipolar (true);
    level->setTooltip ("Output level of this band");
    mix = std::make_unique<st::Knob> (proc.bandParam (band, "mix"), um, "Mix");
    mix->setSize (st::Knob::Size::medium);
    mix->setColour (colour);
    mix->setTooltip ("Dry / wet of this band's plug-ins (latency compensated, so partial mixes don't comb)");
    addAndMakeVisible (*level);
    addAndMakeVisible (*mix);

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

bool BandRack::showsAddSocket() const
{
    return rows.size() < kMaxSlots;
}

int BandRack::getPreferredHeight() const
{
    const int sockets = rows.size() + (showsAddSocket() ? 1 : 0);
    const int listHeight = sockets * (slotHeight + slotGap) - slotGap;
    return edgeHeight + juce::jmax (minBarHeight, 18 + titleHeight + 4 + listHeight + 14) + 6;
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
    if (lo.isEmpty()) return "below " + hi;
    if (hi.isEmpty()) return "above " + lo;
    return lo + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + hi;
}

void BandRack::timerCallback()
{
    const float x = band > 0 ? proc.getCrossover (band - 1) : proc.getCrossover (band);
    if (! juce::exactlyEqual (x, lastCrossover))
    {
        lastCrossover = x;
        repaint (title);
    }
}

//==============================================================================
juce::Rectangle<int> BandRack::socketBounds (int index) const
{
    return { list.getX(), list.getY() + index * (slotHeight + slotGap), list.getWidth(), slotHeight };
}

void BandRack::resized()
{
    auto bounds = getLocalBounds();
    box = bounds.toFloat().withTrimmedTop ((float) edgeHeight).reduced (3.0f, 0.0f).withTrimmedBottom (6.0f);

    // top edge: [on][S][M]  BAND  range
    {
        auto edge = juce::Rectangle<int> (18, 0, bounds.getWidth() - 36, 24);
        power->setBounds (edge.removeFromLeft (32));
        edge.removeFromLeft (6);
        solo->setBounds (edge.removeFromLeft (28));
        edge.removeFromLeft (4);
        mute->setBounds (edge.removeFromLeft (28));
    }

    auto inner = box.toNearestInt().reduced (14, 0).withTrimmedTop (18).withTrimmedBottom (14);
    knobs = inner.removeFromRight (knobAreaWidth);
    inner.removeFromRight (14);
    title = inner.removeFromTop (titleHeight);
    inner.removeFromTop (4);
    list = inner;

    // knobs: ringed Level + matte Mix, side by side at the top of the bar
    {
        auto k = knobs.withHeight (minBarHeight - 26).translated (0, -6);
        level->setBounds (k.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (96, 112));
        mix->setBounds (k.withSizeKeepingCentre (84, 100).translated (0, 4));
    }

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (socketBounds (i));
}

void BandRack::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = getBandColour (band);

    style::paintGlassBar (g, box, selected ? colour : juce::Colour());

    // band name + range at the top of the bar, above the slots
    {
        auto tr = title.toFloat();
        const auto name = getBandName (band).toUpperCase();
        g.setFont (st::fonts::caption (11.5f));
        const float nameW = st::fonts::stringWidth (g.getCurrentFont(), name) + 6.0f;
        g.setColour (colour.withAlpha (selected ? 1.0f : 0.8f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ tr.getX() + 3.0f, tr.getCentreY() }));
        tr.removeFromLeft (13.0f);
        g.setColour (t.text.withAlpha (selected ? 1.0f : 0.85f));
        g.drawText (name, tr.removeFromLeft (nameW), juce::Justification::centredLeft, false);
        g.setFont (st::fonts::medium (12.0f));
        g.setColour (t.textDim);
        g.drawText ("Everything " + rangeText(), tr.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
    }

    // faded divider between the plug-ins and the knobs
    {
        const float x = (float) knobs.getX() - 7.5f;
        st::paint::fadedLine (g, { x, box.getY() + 14.0f }, { x, box.getBottom() - 14.0f }, juce::Colours::white.withAlpha (0.08f), 1.0f, 0.35f);
    }

    // "Add plug-in" well
    if (showsAddSocket())
    {
        auto s = socketBounds (rows.size()).toFloat().reduced (0.5f, 0.5f).withTrimmedBottom (1.5f);
        g.setColour (juce::Colours::black.withAlpha (addHover ? 0.22f : 0.3f));
        g.fillRoundedRectangle (s, t.radius);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (s, t.radius);
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (addHover ? 0.3f : 0.14f));
        g.fillPath (dashed);
        const auto c = addHover ? t.text : t.textDim;
        st::icons::draw (g, st::icons::Icon::plus, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ s.getX() + 16.0f, s.getCentreY() }), c, 1.6f);
        g.setFont (st::fonts::medium (12.0f));
        g.setColour (c);
        g.drawText (rows.isEmpty() ? "Add a plug-in to the " + getBandName (band).toLowerCase() + " band" : juce::String ("Add plug-in"),
                    s.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
    }

    if (rows.isEmpty())
    {
        g.setFont (st::fonts::regular (11.5f));
        g.setColour (t.textFaint);
        g.drawFittedText ("Drag plug-ins between bands to move them.",
                          socketBounds (1).translated (2, 2), juce::Justification::centredLeft, 1);
    }
}

void BandRack::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    const float y = (float) socketBounds (dropIndex).getY() - (float) slotGap * 0.5f - 1.0f;
    st::paint::fadedLine (g, { (float) list.getX(), y }, { (float) list.getRight(), y },
                          dropRefused ? st::Theme::get().danger : getBandColour (band), 2.0f, 0.15f);
}

bool BandRack::isOverAddSocket (juce::Point<int> p) const
{
    return showsAddSocket() && proc.getChains().canAdd (band) && socketBounds (rows.size()).contains (p);
}

void BandRack::mouseMove (const juce::MouseEvent& e)
{
    const bool over = isOverAddSocket (e.getPosition());
    if (over != addHover)
    {
        addHover = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (socketBounds (rows.size()).expanded (2));
    }
}

void BandRack::mouseExit (const juce::MouseEvent&)
{
    if (addHover) { addHover = false; repaint (socketBounds (rows.size()).expanded (2)); }
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
