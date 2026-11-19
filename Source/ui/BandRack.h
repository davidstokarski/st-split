#pragma once
#include "Style.h"

namespace stsplit
{

class BandRack;

/** One hosted plug-in, drawn like a Logic insert slot: power (slot bypass), name, maker / format, latency; edit and
    remove on hover. Click opens the plug-in's window, drag reorders (or moves it to the other band), right-click
    for more. */
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
/** A band's channel strip: header (colour, name, range, M / S / on), kMaxSlots insert sockets (filled ones are
    SlotRows; empty ones open the plug-in menu; the whole list is a drop target for slots from either rack), and a
    column with the band's meter, Level and Mix. */
class BandRack : public juce::Component, public juce::DragAndDropTarget, private ChainHost::Listener, private juce::Timer
{
public:
    BandRack (StSplitProcessor&, int band);
    ~BandRack() override;

    int getBand() const noexcept { return band; }
    void setSelected (bool);
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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    StSplitProcessor& getProcessor() noexcept { return proc; }

    static constexpr int slotHeight = 28, slotGap = 4, headerHeight = 34;

private:
    StSplitProcessor& proc;
    const int band;
    bool selected = false, addHover = false;
    int dropIndex = -1;
    bool dropRefused = false;
    float lastCrossover = -1.0f;
    juce::StringArray openEditors;

    std::unique_ptr<style::Toggle> mute, solo, power;
    std::unique_ptr<st::Knob> level, mix;
    style::Meter meter;
    juce::OwnedArray<SlotRow> rows;

    juce::Rectangle<int> header, list, side;

    juce::Rectangle<int> socketBounds (int index) const;
    void rebuildRows();
    juce::String rangeText() const;
    int indexForY (float y) const;
    bool isOverAddSocket (juce::Point<int>) const;
    void chainsChanged() override { rebuildRows(); }
    void timerCallback() override;
};

} // namespace stsplit
