#pragma once
#include "Style.h"

namespace stsplit
{

/** Searchable plug-in browser (replaces a native menu, which can't hold a search field). Opens as a popover next
    to the slot it adds to; the search field has focus straight away.

    - Type to filter: every word must match the name, maker, format or category; names that start with the query
      rank first. Empty search: Recent, then every plug-in grouped by maker.
    - Up / Down move, Return inserts, Esc or a click outside closes; click or double-click a row to insert.
    - The footer shows the VST3 scan (the list updates live while it runs) and has Rescan. */
class PluginBrowser : public st::Popover, private juce::ChangeListener, private juce::TextEditor::Listener,
                      private juce::KeyListener, private juce::ComponentListener
{
public:
    struct Row
    {
        bool isHeader = false;
        juce::String header;
        juce::PluginDescription desc;
    };

    /** The rows for a query (pure; tested). */
    static std::vector<Row> buildRows (const std::vector<juce::PluginDescription>& effects,
                                       const std::vector<juce::PluginDescription>& recent,
                                       const juce::String& query);

    /** Opens the browser in the editor that contains `anchor`, next to `anchorArea` (in anchor's coordinates).
        Only one browser is open at a time. */
    static void show (juce::Component& anchor, juce::Rectangle<int> anchorArea, PluginCatalog&, const juce::String& title,
                      const juce::String& subtitle, std::function<void (const juce::PluginDescription&)> onChosen);
    /** The open browser, if any (tests / snapshots). */
    static PluginBrowser* getOpen();

    ~PluginBrowser() override;

    void setQuery (const juce::String&);
    int getNumResults() const;
    static constexpr int panelWidth = 400, panelHeight = 440;

private:
    PluginBrowser (PluginCatalog&, const juce::String& title, const juce::String& subtitle,
                   std::function<void (const juce::PluginDescription&)> onChosen);

    struct List;
    PluginCatalog& catalog;
    std::function<void (const juce::PluginDescription&)> chosen;
    juce::TextEditor search;
    juce::Viewport viewport;
    std::unique_ptr<List> list;
    st::FlatButton rescan { "Rescan", "Look for new or updated plug-ins" };
    juce::Rectangle<int> footer;
    juce::Component* watched = nullptr;

    void refresh();
    void choose (const juce::PluginDescription&);
    void close();
    void moveHighlight (int delta);

    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside (from the parent) closes
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); repaint (footer); }
    void textEditorTextChanged (juce::TextEditor&) override { refresh(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    using st::Popover::keyPressed;
    void componentBeingDeleted (juce::Component&) override;   // the editor closes: go with it
};

} // namespace stsplit
