#pragma once
#include "Style.h"

namespace stsplit
{

class BandRack;

/** One hosted plug-in: a grey hardware face (like the family's bar dropdowns) with power (slot bypass), name, maker /
    format and latency; edit and remove on hover; a band-coloured bar underneath while its window is open.
    Click opens the plug-in's window, drag reorders (or moves it to the other band), right-click for more. */
class SlotRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotRow (BandRack&, const ChainHost::SlotInfo&);

    const juce::String& getUid() const noexcept { return info.uid; }
    void setEditorOpen (bool b) { if (editorOpen != b) { editorOpen = b; repaint(); } }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverPart = Part::none; repaint(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Part { none, power, body, edit, remove };
    BandRack& rack;
    ChainHost::SlotInfo info;
    bool editorOpen = false, dragging = false;
    Part hoverPart = Part::none, downPart = Part::none;
    juce::Rectangle<float> powerBox, editBox, removeBox;

    Part partAt (juce::Point<float>) const;
    void showContextMenu();
};

//==============================================================================
/** A band's panel, ST Sat style: a glass bar with grey power / S / M buttons and the band's name sitting on its top
    edge, the ordered plug-in slots with an "Add plug-in" well (drop target for slots from either panel), and a
    ringed Level knob (band colour) next to a matte Mix knob. Its height follows the number of plug-ins
    (getPreferredHeight); the editor anchors it at the bottom and eases it there. */
class BandRack : public juce::Component, public juce::DragAndDropTarget, private ChainHost::Listener, private juce::Timer
{
public:
    BandRack (StSplitProcessor&, int band);
    ~BandRack() override;

    int getBand() const noexcept { return band; }
    void setSelected (bool);
    void setOpenEditors (const juce::StringArray& uids);
    int getPreferredHeight() const;

    std::function<void (const juce::String& uid)> onOpenEditor;
    std::function<void (int band)> onSelected;

    /** Opens the plug-in menu; the choice is inserted at `index`. Null target = at the mouse. */
    void showAddMenu (int index, juce::Component* target);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    StSplitProcessor& getProcessor() noexcept { return proc; }

    static constexpr int edgeHeight = 12;                 // the top-edge buttons stick out this far above the bar
    static constexpr int slotHeight = 26, slotGap = 4, knobAreaWidth = 196, minBarHeight = 140, titleHeight = 22;

private:
    StSplitProcessor& proc;
    const int band;
    bool selected = false, addHover = false;
    int dropIndex = -1;
    bool dropRefused = false;
    float lastCrossover = -1.0f;
    juce::StringArray openEditors;

    std::unique_ptr<st::ToggleChip> power, solo, mute;
    std::unique_ptr<st::Knob> level, mix;
    juce::OwnedArray<SlotRow> rows;

    juce::Rectangle<float> box;
    juce::Rectangle<int> list, knobs, title;   // title: band name + range, inside the bar above the slots

    juce::Rectangle<int> socketBounds (int index) const;
    bool showsAddSocket() const;
    void rebuildRows();
    juce::String rangeText() const;
    int indexForY (float y) const;
    bool isOverAddSocket (juce::Point<int>) const;
    void chainsChanged() override { rebuildRows(); }
    void timerCallback() override;
};

} // namespace stsplit
