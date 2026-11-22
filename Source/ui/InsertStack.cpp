#include "InsertStack.h"
#include "PluginBrowser.h"

namespace stsplit
{

namespace
{
    const juce::String dragPrefix = "st-split-slot:";
    juce::String ellipsis() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }

    // Compact insert-slot faces (Logic's mixer): neutral greys a step above the display, no hardware gradient.
    juce::Colour slotFace (bool bypassed, bool hover)
    {
        const auto& t = st::Theme::get();
        auto c = bypassed ? t.bg3 : t.buttonBottom;
        return hover ? c.brighter (0.12f) : c;
    }
}

//==============================================================================
InsertSlot::InsertSlot (InsertStack& s, const ChainHost::SlotInfo& i) : stack (s), info (i)
{
    juce::String tip = info.name + "  (" + info.manufacturer + ", " + info.format + ")";
    if (info.status == ChainHost::Status::failed)
        tip << "\n" << info.error << ". Its settings are kept; reinstall it and reopen the session.";
    else
    {
        if (info.latencySamples > 0)
            tip << "\nLatency " << juce::String (1000.0 * info.latencySamples / juce::jmax (1.0, stack.getProcessor().getSampleRate()), 1) << " ms";
        tip << "\nClick to open. Option-click bypasses, Cmd-click removes, drag to reorder or move to the other band.";
    }
    setTooltip (tip);
}

juce::Rectangle<float> InsertSlot::powerBox() const
{
    return juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ 12.0f, (float) getHeight() * 0.5f });
}

juce::Rectangle<float> InsertSlot::removeBox() const
{
    return juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) getWidth() - 12.0f, (float) getHeight() * 0.5f });
}

InsertSlot::Part InsertSlot::partAt (juce::Point<float> p) const
{
    if (! isMouseOver (true)) return Part::none;
    if (powerBox().expanded (5.0f).contains (p)) return Part::power;
    if (removeBox().expanded (4.0f).contains (p)) return Part::remove;
    return Part::body;
}

void InsertSlot::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const bool hover = isMouseOver (true) && ! dragging;
    const bool failed = info.status == ChainHost::Status::failed;
    const bool loading = info.status == ChainHost::Status::loading;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (slotFace (info.bypassed || dragging, hover));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (info.bypassed ? 0.03f : 0.07f));
    g.fillRect (r.getX() + 4.0f, r.getY() + 0.5f, r.getWidth() - 8.0f, 1.0f);
    g.setColour (editorOpen ? getBandColour (stack.getBand()).withAlpha (0.9f) : juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    // name, centred; leaves room for the hover buttons
    auto text = r.reduced (hover ? 22.0f : 8.0f, 0.0f);
    auto name = info.name;
    if (loading) name << "  " << ellipsis();
    g.setFont (st::fonts::medium (11.5f));
    g.setColour (failed ? t.danger.brighter (0.2f) : t.text.withAlpha (info.bypassed ? 0.4f : 0.92f));
    g.drawFittedText (name, text.toNearestInt(), juce::Justification::centred, 1, 0.85f);

    if (hover)
    {
        st::icons::draw (g, st::icons::Icon::power, powerBox(),
                         info.bypassed ? t.text.withAlpha (hoverPart == Part::power ? 0.8f : 0.45f)
                                       : getBandColour (stack.getBand()).brighter (hoverPart == Part::power ? 0.3f : 0.0f), 1.6f);
        st::icons::draw (g, st::icons::Icon::close, removeBox().reduced (1.5f),
                         hoverPart == Part::remove ? t.danger.brighter (0.3f) : t.text.withAlpha (0.55f), 1.5f);
    }
    else if (info.bypassed)
    {
        // bypassed slots keep a small power mark so the state reads without hovering
        st::icons::draw (g, st::icons::Icon::power, powerBox().reduced (1.0f), t.text.withAlpha (0.3f), 1.4f);
    }
}

void InsertSlot::mouseMove (const juce::MouseEvent& e)
{
    const auto p = partAt (e.position);
    if (p != hoverPart) { hoverPart = p; repaint(); }
}

void InsertSlot::mouseDown (const juce::MouseEvent& e)
{
    downPart = partAt (e.position);
    dragging = false;
    if (stack.onSelected) stack.onSelected (stack.getBand());
    if (e.mods.isPopupMenu()) { downPart = Part::none; showContextMenu(); }
}

void InsertSlot::mouseDrag (const juce::MouseEvent& e)
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

void InsertSlot::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDragging = dragging;
    dragging = false;
    repaint();
    if (wasDragging || downPart == Part::none || partAt (e.position) != downPart) return;

    auto& chains = stack.getProcessor().getChains();
    const auto uid = info.uid;
    auto safe = juce::Component::SafePointer<InsertStack> (&stack);
    auto bypass = [&chains, uid, b = ! info.bypassed, safe] { if (safe != nullptr) chains.setSlotBypassed (uid, b); };
    auto remove = [&chains, uid, safe] { if (safe != nullptr) chains.removeSlot (uid); };

    // Logic's modifiers: Option-click bypasses, Cmd-click removes.
    if (downPart == Part::power || (downPart == Part::body && e.mods.isAltDown())) { juce::MessageManager::callAsync (bypass); return; }
    if (downPart == Part::remove || (downPart == Part::body && e.mods.isCommandDown())) { juce::MessageManager::callAsync (remove); return; }
    if (info.status == ChainHost::Status::ready && stack.onOpenEditor) stack.onOpenEditor (uid);
}

void InsertSlot::showContextMenu()
{
    auto& chains = stack.getProcessor().getChains();
    const auto uid = info.uid;
    const int band = stack.getBand();
    auto safe = juce::Component::SafePointer<InsertStack> (&stack);

    juce::PopupMenu m;
    m.addSectionHeader (info.name);
    m.addItem ("Open editor", info.status == ChainHost::Status::ready, false,
               [safe, uid] { if (safe != nullptr && safe->onOpenEditor) safe->onOpenEditor (uid); });
    m.addItem ("Bypass", true, info.bypassed, [&chains, uid, b = ! info.bypassed, safe] { if (safe != nullptr) chains.setSlotBypassed (uid, b); });
    m.addSeparator();
    for (int other = 0; other < kMaxBands; ++other)
        if (other != band)
            m.addItem ("Move to " + getBandName (other) + " band", chains.canAdd (other), false,
                       [&chains, uid, other, safe] { if (safe != nullptr) chains.moveSlot (uid, other, kMaxSlots); });
    m.addItem ("Replace with" + ellipsis(), [self = juce::Component::SafePointer<InsertSlot> (this), &chains, uid, safe, name = info.name]
    {
        if (safe == nullptr || self == nullptr) return;
        PluginBrowser::show (*self, self->getLocalBounds(), chains.getCatalog(), "Replace", name,
                             [&chains, uid, safe] (const juce::PluginDescription& d) { if (safe != nullptr) chains.replacePlugin (uid, d); });
    });
    m.addSeparator();
    m.addItem ("Remove", [&chains, uid, safe] { if (safe != nullptr) chains.removeSlot (uid); });
    st::showMenu (m, nullptr, {}, true);
}

//==============================================================================
InsertStack::InsertStack (StSplitProcessor& p, int b) : proc (p), band (b)
{
    setOpaque (false);
    proc.getChains().addListener (this);
    rebuild();
}

InsertStack::~InsertStack()
{
    proc.getChains().removeListener (this);
}

bool InsertStack::showsAddSlot() const
{
    return slots.size() < kMaxSlots && (revealed || dragOver || slots.isEmpty() || addHover);
}

int InsertStack::getPreferredHeight() const
{
    // The add slot's space is always reserved so revealing it never moves anything.
    const int n = juce::jmin (kMaxSlots, slots.size() + 1);
    return n * (slotHeight + slotGap) - slotGap;
}

void InsertStack::setRevealed (bool b)
{
    if (revealed == b) return;
    revealed = b;
    repaint();
}

void InsertStack::setOpenEditors (const juce::StringArray& uids)
{
    openEditors = uids;
    for (auto* s : slots) s->setEditorOpen (openEditors.contains (s->getUid()));
}

void InsertStack::rebuild()
{
    slots.clear();
    for (auto& s : proc.getChains().getSlots (band))
    {
        auto* slot = slots.add (new InsertSlot (*this, s));
        slot->setEditorOpen (openEditors.contains (s.uid));
        addAndMakeVisible (slot);
    }
    resized();
    repaint();
}

void InsertStack::showAddMenu (int index, juce::Component* target)
{
    if (! proc.getChains().canAdd (band)) return;
    auto safe = juce::Component::SafePointer<InsertStack> (this);
    const auto lo = band > 0 ? st::params::formatHz (proc.getCrossover (band - 1)) : juce::String();
    const auto hi = band < kMaxBands - 1 ? st::params::formatHz (proc.getCrossover (band)) : juce::String();
    auto& anchor = target != nullptr ? *target : *static_cast<juce::Component*> (this);
    const auto area = target != nullptr ? target->getLocalBounds() : slotBounds (juce::jmin (slots.size(), kMaxSlots - 1));
    PluginBrowser::show (anchor, area, proc.getChains().getCatalog(), "Add to " + getBandName (band),
                         lo.isEmpty() ? "below " + hi : hi.isEmpty() ? "above " + lo : lo + " - " + hi,
                         [safe, index] (const juce::PluginDescription& d)
    {
        if (auto* self = safe.getComponent())
        {
            const auto uid = self->proc.getChains().addPlugin (self->band, index, d);
            if (uid.isNotEmpty() && self->onOpenEditor && self->proc.getChains().getInstance (uid) != nullptr)
                self->onOpenEditor (uid);
        }
    });
}

juce::Rectangle<int> InsertStack::slotBounds (int index) const
{
    return { 0, index * (slotHeight + slotGap), getWidth(), slotHeight };
}

void InsertStack::resized()
{
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setBounds (slotBounds (i));
}

bool InsertStack::hitTest (int x, int y)
{
    // Only the slots (and a visible add slot) take the mouse: the display underneath stays usable.
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (c->isDragAndDropActive()) return true;
    const int used = slots.size() + (showsAddSlot() ? 1 : 0);
    return used > 0 && juce::Rectangle<int> (0, 0, getWidth(), slotBounds (used - 1).getBottom()).contains (x, y);
}

void InsertStack::paint (juce::Graphics& g)
{
    if (! showsAddSlot()) return;
    const auto& t = st::Theme::get();
    auto s = slotBounds (slots.size()).toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (addHover ? 0.35f : 0.25f));
    g.fillRoundedRectangle (s, 4.0f);
    juce::Path outline, dashed;
    outline.addRoundedRectangle (s, 4.0f);
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (juce::Colours::white.withAlpha (addHover ? 0.32f : 0.16f));
    g.fillPath (dashed);

    const auto c = addHover ? t.text : t.textDim;
    const auto label = juce::String ("Add plug-in");
    g.setFont (st::fonts::medium (11.5f));
    const float w = 14.0f + st::fonts::stringWidth (g.getCurrentFont(), label);
    auto row = s.withSizeKeepingCentre (juce::jmin (s.getWidth() - 8.0f, w), s.getHeight());
    st::icons::draw (g, st::icons::Icon::plus, juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ row.getX() + 4.5f, row.getCentreY() }), c, 1.6f);
    g.setColour (c);
    g.drawText (label, row.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
}

void InsertStack::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0) return;
    const float y = (float) slotBounds (dropIndex).getY() - (float) slotGap * 0.5f - 1.0f;
    g.setColour (dropRefused ? st::Theme::get().danger : getBandColour (band));
    g.fillRoundedRectangle (0.0f, juce::jmax (0.0f, y), (float) getWidth(), 2.0f, 1.0f);
}

void InsertStack::mouseMove (const juce::MouseEvent& e)
{
    const bool over = showsAddSlot() && slotBounds (slots.size()).contains (e.getPosition());
    if (over != addHover)
    {
        addHover = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void InsertStack::mouseExit (const juce::MouseEvent&)
{
    if (addHover) { addHover = false; repaint(); }
}

void InsertStack::mouseUp (const juce::MouseEvent& e)
{
    if (onSelected) onSelected (band);
    if (showsAddSlot() && slotBounds (slots.size()).contains (e.getPosition()) && slotBounds (slots.size()).contains (e.getMouseDownPosition()))
        showAddMenu (slots.size(), nullptr);
}

//==============================================================================
int InsertStack::indexForY (float y) const
{
    int i = 0;
    for (auto* s : slots)
        if (y > (float) s->getBounds().getCentreY()) ++i;
    return i;
}

bool InsertStack::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith (dragPrefix);
}

void InsertStack::itemDragEnter (const SourceDetails& d)
{
    dragOver = true;
    itemDragMove (d);
}

void InsertStack::itemDragMove (const SourceDetails& d)
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

void InsertStack::itemDragExit (const SourceDetails&)
{
    dragOver = false;
    dropIndex = -1;
    repaint();
}

void InsertStack::itemDropped (const SourceDetails& d)
{
    const auto uid = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false);
    const int idx = dropIndex >= 0 ? dropIndex : slots.size();
    dragOver = false;
    dropIndex = -1;
    repaint();
    if (onSelected) onSelected (band);

    auto safe = juce::Component::SafePointer<InsertStack> (this);
    juce::MessageManager::callAsync ([safe, uid, idx]
    {
        if (auto* self = safe.getComponent())
            self->proc.getChains().moveSlot (uid, self->band, idx);
    });
}

} // namespace stsplit
