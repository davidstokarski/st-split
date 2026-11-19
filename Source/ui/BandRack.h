#pragma once
#include "../PluginProcessor.h"

namespace stsplit
{

class BandRack;

/** One hosted plug-in in a rack: number, power (slot bypass), name / maker / format, latency, edit and remove.
    Click opens the plug-in's window, drag reorders (or moves it to the other band), right-click for more. */
class SlotRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotRow (BandRack&, const ChainHost::SlotInfo&, int index);

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

    static constexpr int height = 26;

private:
    enum class Part { none, power, body, edit, remove };
    BandRack& rack;
    ChainHost::SlotInfo info;
    int index;
    bool editorOpen = false, dragging = false;
    Part hoverPart = Part::none, downPart = Part::none;
    juce::Rectangle<float> powerBox, editBox, removeBox;

    Part partAt (juce::Point<float>) const;
    void showContextMenu();
};

//==============================================================================
/** A band's plug-in chain: a glass panel with the band's name, frequency range and S / M / bypass, the ordered list
    of hosted plug-ins (drop target for drags from either rack) with an "Add plug-in" row, and Level / Mix knobs. */
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

    void showAddMenu (int index, juce::Component* target);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    StSplitProcessor& getProcessor() noexcept { return proc; }

private:
    friend class SlotRow;
    StSplitProcessor& proc;
    const int band;
    bool selected = false;
    int dropIndex = -1;
    bool dropRefused = false;
    float lastCrossover = -1.0f;
    juce::StringArray openEditors;

    std::unique_ptr<st::ToggleChip> solo, mute, bypass;
    std::unique_ptr<st::Knob> level, mix;
    juce::OwnedArray<SlotRow> rows;

    struct AddButton : juce::Component, juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
    } addButton;

    juce::Rectangle<float> panel;
    juce::Rectangle<int> header, list, knobColumn;

    void rebuildRows();
    juce::String rangeText() const;
    int indexForY (float y) const;
    void chainsChanged() override { rebuildRows(); }
    void timerCallback() override;
};

} // namespace stsplit
