#include "BandRack.h"
#include "PluginMenu.h"

namespace stsplit
{

namespace
{
    const juce::String dragPrefix = "st-split-slot:";
    constexpr int rowGap = 3;
    juce::String ellipsis() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }
    juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")); }
}

//==============================================================================
SlotRow::SlotRow (BandRack& r, const ChainHost::SlotInfo& i, int idx) : rack (r), info (i), index (idx)
{
    if (info.status == ChainHost::Status::failed)
        setTooltip (info.name + ": " + info.error + ". Its settings are kept; reinstall it and reopen the session.");
    else
        setTooltip ("Click to open " + info.name + ". Drag to reorder or move to the other band, right-click for more.");
}

void SlotRow::resized()
{
    auto r = getLocalBounds().toFloat();
    powerBox = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ r.getX() + 32.0f, r.getCentreY() });
    removeBox = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ r.getRight() - 14.0f, r.getCentreY() });
    editBox = removeBox.translated (-22.0f, 0.0f);
}

SlotRow::Part SlotRow::partAt (juce::Point<float> p) const
{
    if (powerBox.expanded (4.0f).contains (p)) return Part::power;
    if (isMouseOver (true) && removeBox.expanded (2.0f).contains (p)) return Part::remove;
    if (isMouseOver (true) && editBox.expanded (2.0f).contains (p) && info.status == ChainHost::Status::ready) return Part::edit;
    return Part::body;
}

void SlotRow::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = getBandColour (rack.getBand());
    const bool hover = isMouseOver (true);
    const bool failed = info.status == ChainHost::Status::failed;
    const bool loading = info.status == ChainHost::Status::loading;
    const bool dim = info.bypassed || failed;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (juce::Colours::white.withAlpha (dragging ? 0.02f : (hover ? 0.075f : 0.045f)));
    g.fillRoundedRectangle (r, 5.0f);
    if (editorOpen)
    {
        g.setColour (colour.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }

    // number
    g.setFont (st::fonts::semibold (10.5f));
    g.setColour (t.textFaint);
    g.drawText (juce::String (index + 1), juce::Rectangle<float> (8.0f, r.getY(), 12.0f, r.getHeight()), juce::Justification::centred, false);

    // power
    const bool powerHover = hoverPart == Part::power;
    st::icons::draw (g, st::icons::Icon::power, powerBox,
                     info.bypassed ? t.textFaint.withAlpha (powerHover ? 1.0f : 0.8f) : colour.withAlpha (powerHover ? 1.0f : 0.85f), 1.7f);

    // name + maker
    auto textArea = r.withTrimmedLeft (50.0f).withTrimmedRight (hover ? 52.0f : 12.0f);
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
        g.setColour (failed ? t.danger.withAlpha (0.8f) : t.textFaint);
        g.drawText (trailing, textArea.removeFromRight (w), juce::Justification::centredRight, true);
        textArea.removeFromRight (8.0f);
    }

    g.setFont (st::fonts::medium (13.0f));
    const float nameW = juce::jmin (textArea.getWidth(), st::fonts::stringWidth (g.getCurrentFont(), info.name) + 2.0f);
    g.setColour (failed ? t.danger.withAlpha (0.85f) : t.text.withAlpha (dim ? 0.45f : 0.95f));
    g.drawText (info.name, textArea.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    const auto maker = info.manufacturer + "  " + dot() + "  " + info.format;
    g.setFont (st::fonts::regular (11.0f));
    g.setColour (t.textFaint.withAlpha (dim ? 0.6f : 1.0f));
    g.drawText (maker, textArea.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);

    if (hover)
    {
        if (info.status == ChainHost::Status::ready)
            st::icons::draw (g, st::icons::Icon::pencil, editBox.reduced (2.0f),
                             hoverPart == Part::edit ? t.text : t.textDim.withAlpha (0.8f), 1.5f);
        st::icons::draw (g, st::icons::Icon::close, removeBox.reduced (3.0f),
                         hoverPart == Part::remove ? t.danger : t.textDim.withAlpha (0.8f), 1.6f);
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
        dragging = true;
        auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
        image.multiplyAllAlphas (0.85f);
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
    auto& proc = rack.getProcessor();
    auto& chains = proc.getChains();
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
void BandRack::AddButton::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const bool hover = isMouseOver (true);
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    juce::Path outline;
    outline.addRoundedRectangle (r, 5.0f);
    juce::Path dashed;
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.28f : 0.14f));
    g.fillPath (dashed);
    if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (r, 5.0f);
    }

    st::icons::draw (g, st::icons::Icon::plus, juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 32.0f, r.getCentreY() }),
                     hover ? t.text : t.textDim, 1.6f);
    g.setFont (st::fonts::medium (12.5f));
    g.setColour (hover ? t.text : t.textDim);
    g.drawText ("Add plug-in", r.withTrimmedLeft (50.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
BandRack::BandRack (StSplitProcessor& p, int b) : proc (p), band (b)
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    const auto colour = getBandColour (band);

    solo = std::make_unique<st::ToggleChip> (proc.bandParam (band, "solo"), um, "S", t.warning);
    mute = std::make_unique<st::ToggleChip> (proc.bandParam (band, "mute"), um, "M", t.danger);
    bypass = std::make_unique<st::ToggleChip> (proc.bandParam (band, "bypass"), um, "", colour);
    bypass->setIcon (st::icons::Icon::power);
    bypass->setInverted (true);
    for (auto* c : { solo.get(), mute.get(), bypass.get() })
    {
        c->setGreyStyle (true);
        addAndMakeVisible (*c);
    }
    solo->setTooltip ("Solo this band");
    mute->setTooltip ("Mute this band");
    bypass->setTooltip ("Band plug-ins on / off (off passes the band through untouched)");

    level = std::make_unique<st::Knob> (proc.bandParam (band, "level"), um, "Level");
    mix = std::make_unique<st::Knob> (proc.bandParam (band, "mix"), um, "Mix");
    for (auto* k : { level.get(), mix.get() })
    {
        k->setSize (st::Knob::Size::medium);
        k->setColour (colour);
        addAndMakeVisible (*k);
    }
    level->setBipolar (true);
    level->setTooltip ("Output level of this band");
    mix->setTooltip ("Dry / wet of this band's plug-ins (latency compensated, so partial mixes don't comb)");

    addButton.onClick = [this] { showAddMenu (rows.size(), &addButton); };
    addButton.setTooltip ("Add one of your plug-ins to the " + getBandName (band).toLowerCase() + " band");
    addAndMakeVisible (addButton);

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
    const auto slots = proc.getChains().getSlots (band);
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto* row = rows.add (new SlotRow (*this, slots[(size_t) i], i));
        row->setEditorOpen (openEditors.contains (slots[(size_t) i].uid));
        addAndMakeVisible (row);
    }
    addButton.setVisible (proc.getChains().canAdd (band));
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
    }, target == nullptr, target == nullptr ? "Add to " + getBandName (band) : juce::String());
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
        if (rows.isEmpty()) repaint (list);
    }
}

//==============================================================================
void BandRack::resized()
{
    panel = getLocalBounds().toFloat().reduced (4.0f);
    auto r = panel.toNearestInt().reduced (14, 10);
    header = r.removeFromTop (28);
    r.removeFromTop (8);

    {
        auto h = header;
        bypass->setBounds (h.removeFromRight (30).withSizeKeepingCentre (30, 22));
        h.removeFromRight (5);
        mute->setBounds (h.removeFromRight (26).withSizeKeepingCentre (26, 22));
        h.removeFromRight (5);
        solo->setBounds (h.removeFromRight (26).withSizeKeepingCentre (26, 22));
    }

    knobColumn = r.removeFromRight (86);
    r.removeFromRight (12);
    list = r;

    {
        auto k = knobColumn;
        const int kh = juce::jmin (100, k.getHeight() / 2);
        level->setBounds (k.removeFromTop (kh).withSizeKeepingCentre (80, 96));
        mix->setBounds (k.removeFromTop (kh).withSizeKeepingCentre (80, 96));
    }

    int y = list.getY();
    for (auto* row : rows)
    {
        row->setBounds (list.getX(), y, list.getWidth(), SlotRow::height);
        y += SlotRow::height + rowGap;
    }
    addButton.setBounds (list.getX(), y, list.getWidth(), SlotRow::height);
}

void BandRack::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const auto colour = getBandColour (band);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (panel.translated (0.0f, 3.0f).expanded (1.0f), 10.0f);
    g.setColour (t.glassPanel);
    g.fillRoundedRectangle (panel, 9.0f);
    if (selected)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.07f), 0.0f, panel.getY(), colour.withAlpha (0.0f), 0.0f, panel.getY() + 90.0f, false));
        g.fillRoundedRectangle (panel, 9.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 9.0f, 1.0f);
    {
        const auto edgeColour = selected ? colour.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.13f);
        juce::ColourGradient edge (edgeColour.withAlpha (0.0f), panel.getX(), 0.0f, edgeColour.withAlpha (0.0f), panel.getRight(), 0.0f, false);
        edge.addColour (0.25, edgeColour);
        edge.addColour (0.75, edgeColour);
        g.setGradientFill (edge);
        g.fillRect (panel.getX() + 9.0f, panel.getY() + 0.5f, panel.getWidth() - 18.0f, 1.0f);
    }

    // header: ● LOW  Below 400 Hz
    auto h = header.toFloat();
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ h.getX() + 3.5f, h.getCentreY() }));
    h.removeFromLeft (15.0f);
    const auto name = getBandName (band).toUpperCase();
    g.setFont (st::fonts::caption (11.5f));
    const float nameW = st::fonts::stringWidth (g.getCurrentFont(), name) + 6.0f;
    g.setColour (t.text);
    g.drawText (name, h.removeFromLeft (nameW), juce::Justification::centredLeft, false);
    g.setFont (st::fonts::medium (12.0f));
    g.setColour (t.textDim);
    g.drawText (rangeText(), h.withTrimmedLeft (6.0f).withTrimmedRight (110.0f), juce::Justification::centredLeft, true);

    // knob column divider
    st::paint::fadedLine (g, { (float) knobColumn.getX() - 6.5f, (float) knobColumn.getY() + 6.0f },
                          { (float) knobColumn.getX() - 6.5f, (float) knobColumn.getBottom() - 6.0f },
                          juce::Colours::white.withAlpha (0.08f), 1.0f, 0.35f);

    if (rows.isEmpty())
    {
        const auto hint = band < kMaxBands - 1
            ? "Everything below " + st::params::formatHz (proc.getCrossover (band)) + " runs through the plug-ins you add here."
            : "Everything above " + st::params::formatHz (proc.getCrossover (band - 1)) + " runs through the plug-ins you add here.";
        g.setFont (st::fonts::regular (12.0f));
        g.setColour (t.textFaint);
        g.drawFittedText (hint + "\nDrag plug-ins between bands to move them.",
                          list.withTrimmedTop (SlotRow::height + 16).withHeight (40), juce::Justification::centredTop, 2);
    }
}

void BandRack::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    const auto colour = dropRefused ? st::Theme::get().danger : getBandColour (band);
    const float y = (float) list.getY() + (float) dropIndex * (float) (SlotRow::height + rowGap) - (float) rowGap * 0.5f - 0.5f;
    st::paint::fadedLine (g, { (float) list.getX(), y }, { (float) list.getRight(), y }, colour, 2.0f, 0.15f);
}

void BandRack::mouseDown (const juce::MouseEvent& e)
{
    if (onSelected) onSelected (band);
    if (e.mods.isPopupMenu() && list.contains (e.getPosition()))
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
