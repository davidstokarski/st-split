#pragma once
#include "Style.h"

namespace stsplit
{

class InsertStack;

/** One hosted plug-in as a compact insert slot (Logic's mixer slots): the plug-in's name centred on a small grey
    face. Hover reveals power (bypass) on the left and remove on the right. Click opens the plug-in's window,
    Option-click bypasses, Cmd-click removes, drag reorders or moves it to the other band, right-click for more. */
class InsertSlot : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertSlot (InsertStack&, const ChainHost::SlotInfo&);

    const juce::String& getUid() const noexcept { return info.uid; }
    void setEditorOpen (bool b) { if (editorOpen != b) { editorOpen = b; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverPart = Part::none; repaint(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Part { none, power, body, remove };
    InsertStack& stack;
    ChainHost::SlotInfo info;
    bool editorOpen = false, dragging = false;
    Part hoverPart = Part::none, downPart = Part::none;

    juce::Rectangle<float> powerBox() const;
    juce::Rectangle<float> removeBox() const;
    Part partAt (juce::Point<float>) const;
    void showContextMenu();
};

//==============================================================================
/** A band's insert stack, drawn on the display inside the band's column: its plug-ins top to bottom (signal order)
    plus an "Add" slot, which shows while the band is hovered or selected (or has no plug-ins yet). The whole
    stack is a drop target for slots dragged from either band. The editor positions it over its band every frame. */
class InsertStack : public juce::Component, public juce::DragAndDropTarget, private ChainHost::Listener
{
public:
    InsertStack (StSplitProcessor&, int band);
    ~InsertStack() override;

    int getBand() const noexcept { return band; }
    StSplitProcessor& getProcessor() noexcept { return proc; }

    /** Height needed for the current plug-ins plus the add slot. */
    int getPreferredHeight() const;
    /** Shows the add slot (band hovered or selected). */
    void setRevealed (bool);
    void setOpenEditors (const juce::StringArray& uids);

    std::function<void (const juce::String& uid)> onOpenEditor;
    std::function<void (int band)> onSelected;

    /** Opens the plug-in menu; the choice is inserted at `index`. Null target = at the mouse. */
    void showAddMenu (int index, juce::Component* target);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    static constexpr int slotHeight = 22, slotGap = 3;

private:
    StSplitProcessor& proc;
    const int band;
    bool revealed = false, addHover = false, dragOver = false;
    int dropIndex = -1;
    bool dropRefused = false;
    juce::StringArray openEditors;
    juce::OwnedArray<InsertSlot> slots;

    juce::Rectangle<int> slotBounds (int index) const;
    bool showsAddSlot() const;
    int indexForY (float y) const;
    void rebuild();
    void chainsChanged() override { rebuild(); }
};

} // namespace stsplit
