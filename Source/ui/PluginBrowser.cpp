#include "PluginBrowser.h"

namespace stsplit
{

namespace
{
    juce::Component::SafePointer<PluginBrowser>& current()
    {
        static juce::Component::SafePointer<PluginBrowser> p;
        return p;
    }

    juce::String ellipsis() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); }

    /** 0 = name starts with the query, 1 = a word of the name does, 2 = the name contains it, 3 = maker / format only. */
    int rank (const juce::PluginDescription& d, const juce::String& query)
    {
        const auto name = d.name.toLowerCase();
        if (name.startsWith (query)) return 0;
        for (auto& word : juce::StringArray::fromTokens (name, " -_.", ""))
            if (word.startsWith (query)) return 1;
        return name.contains (query) ? 2 : 3;
    }
}

//==============================================================================
std::vector<PluginBrowser::Row> PluginBrowser::buildRows (const std::vector<juce::PluginDescription>& effects,
                                                          const std::vector<juce::PluginDescription>& recent,
                                                          const juce::String& rawQuery)
{
    std::vector<Row> rows;
    const auto query = rawQuery.trim().toLowerCase();

    if (query.isEmpty())
    {
        if (! recent.empty())
        {
            rows.push_back ({ true, "Recent", {} });
            for (auto& d : recent) rows.push_back ({ false, {}, d });
        }
        juce::String maker;
        for (auto& d : effects)
        {
            const auto m = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
            if (! m.equalsIgnoreCase (maker)) { maker = m; rows.push_back ({ true, m, {} }); }
            rows.push_back ({ false, {}, d });
        }
        return rows;
    }

    const auto tokens = juce::StringArray::fromTokens (query, " ", "");
    std::vector<std::pair<int, juce::PluginDescription>> matches;
    for (auto& d : effects)
    {
        const auto hay = (d.name + " " + d.manufacturerName + " " + PluginCatalog::shortFormatName (d) + " "
                          + d.pluginFormatName + " " + d.category).toLowerCase();
        bool all = true;
        for (auto& tok : tokens) all = all && hay.contains (tok);
        if (all) matches.emplace_back (rank (d, query), d);
    }
    std::stable_sort (matches.begin(), matches.end(), [] (const auto& a, const auto& b)
    {
        if (a.first != b.first) return a.first < b.first;
        return a.second.name.compareNatural (b.second.name) < 0;
    });
    for (auto& [r, d] : matches) rows.push_back ({ false, {}, d });
    return rows;
}

//==============================================================================
struct PluginBrowser::List : juce::Component
{
    std::vector<Row> rows;
    int highlight = -1, hoverRow = -1;
    std::function<void (const juce::PluginDescription&)> onChoose;
    static constexpr int rowHeight = 26, headerHeight = 24;

    int rowY (int i) const
    {
        int y = 0;
        for (int k = 0; k < i; ++k) y += rows[(size_t) k].isHeader ? headerHeight : rowHeight;
        return y;
    }
    int rowH (int i) const { return rows[(size_t) i].isHeader ? headerHeight : rowHeight; }
    int totalHeight() const { return rows.empty() ? 0 : rowY ((int) rows.size()); }

    int rowAt (int y) const
    {
        int top = 0;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const int h = rowH (i);
            if (y >= top && y < top + h) return i;
            top += h;
        }
        return -1;
    }

    void setRows (std::vector<Row> r)
    {
        rows = std::move (r);
        highlight = -1;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (! rows[(size_t) i].isHeader) { highlight = i; break; }
        setSize (getWidth(), juce::jmax (1, totalHeight()));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = st::Theme::get();
        const auto clip = g.getClipBounds();
        int y = 0;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& row = rows[(size_t) i];
            const int h = rowH (i);
            const juce::Rectangle<int> r (0, y, getWidth(), h);
            y += h;
            if (! r.intersects (clip)) continue;

            if (row.isHeader)
            {
                st::paint::caption (g, row.header, r.toFloat().withTrimmedLeft (10.0f).withTrimmedTop (6.0f), t.textFaint,
                                    juce::Justification::centredLeft, 9.5f);
                continue;
            }

            auto rf = r.toFloat().reduced (2.0f, 1.0f);
            if (i == highlight || i == hoverRow)
            {
                g.setColour (juce::Colours::white.withAlpha (i == highlight ? 0.09f : 0.05f));
                g.fillRoundedRectangle (rf, 4.0f);
                if (i == highlight)
                {
                    g.setColour (t.accent);
                    g.fillRoundedRectangle (rf.withWidth (2.5f).reduced (0.0f, 5.0f), 1.0f);
                }
            }

            auto text = rf.reduced (10.0f, 0.0f);
            const auto format = PluginCatalog::shortFormatName (row.desc);
            g.setFont (st::fonts::caption (9.0f));
            const float fw = st::fonts::stringWidth (g.getCurrentFont(), format.toUpperCase()) + 10.0f;
            st::paint::caption (g, format, text.removeFromRight (fw), t.textFaint, juce::Justification::centredRight, 9.0f);

            g.setFont (st::fonts::medium (13.0f));
            const float nw = juce::jmin (text.getWidth(), st::fonts::stringWidth (g.getCurrentFont(), row.desc.name) + 2.0f);
            g.setColour (t.text);
            g.drawText (row.desc.name, text.removeFromLeft (nw), juce::Justification::centredLeft, true);
            g.setFont (st::fonts::regular (11.5f));
            g.setColour (t.textDim);
            g.drawText (row.desc.manufacturerName, text.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        }

        if (rows.empty())
        {
            g.setFont (st::fonts::regular (12.5f));
            g.setColour (t.textFaint);
            g.drawText ("No plug-ins match", getLocalBounds().withHeight (60), juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.y);
        const int h = r >= 0 && ! rows[(size_t) r].isHeader ? r : -1;
        if (h != hoverRow) { hoverRow = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.y);
        if (r >= 0 && ! rows[(size_t) r].isHeader && e.mouseWasClicked() && onChoose)
            onChoose (rows[(size_t) r].desc);
    }
};

//==============================================================================
PluginBrowser* PluginBrowser::getOpen()
{
    return current().getComponent();
}

void PluginBrowser::show (juce::Component& anchor, juce::Rectangle<int> anchorArea, PluginCatalog& catalog, const juce::String& title,
                          const juce::String& subtitle, std::function<void (const juce::PluginDescription&)> onChosen)
{
    if (auto* old = current().getComponent())
        delete old;

    // Live in the editor's (scaled) content: the component whose parent is the editor.
    juce::Component* parent = &anchor;
    while (parent->getParentComponent() != nullptr && dynamic_cast<st::EditorBase*> (parent->getParentComponent()) == nullptr)
        parent = parent->getParentComponent();
    if (parent->getParentComponent() == nullptr) parent = anchor.getTopLevelComponent();

    auto* b = new PluginBrowser (catalog, title, subtitle, std::move (onChosen));
    current() = b;
    parent->addAndMakeVisible (b);

    const auto area = parent->getLocalArea (&anchor, anchorArea);
    const auto size = st::Popover::totalSize (panelWidth, panelHeight);
    const int m = st::Popover::margin;
    int y = area.getBottom() + 4 - m;
    if (y + size.y - m > parent->getHeight())
        y = area.getY() - 4 - panelHeight - m;
    y = juce::jlimit (-m + 4, juce::jmax (-m + 4, parent->getHeight() - size.y + m - 4), y);
    const int x = juce::jlimit (-m + 8, parent->getWidth() - size.x + m - 8, area.getCentreX() - size.x / 2);
    b->setBounds (x, y, size.x, size.y);
    b->toFront (false);
    b->search.grabKeyboardFocus();

    b->watched = parent;
    parent->addMouseListener (b, true);
    parent->addComponentListener (b);
}

PluginBrowser::PluginBrowser (PluginCatalog& c, const juce::String& title, const juce::String& subtitle,
                              std::function<void (const juce::PluginDescription&)> onChosen)
    : st::Popover (title, subtitle), catalog (c), chosen (std::move (onChosen))
{
    const auto& t = st::Theme::get();
    if (catalog.needsInitialRefresh()) catalog.refresh();
    catalog.addChangeListener (this);

    search.setFont (st::fonts::regular (13.5f));
    search.setTextToShowWhenEmpty ("Search plug-ins, makers, AU / VST3" + ellipsis(), t.textFaint);
    search.setColour (juce::TextEditor::backgroundColourId, t.bg0.withAlpha (0.7f));
    search.setColour (juce::TextEditor::textColourId, t.text);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.08f));
    search.setColour (juce::TextEditor::focusedOutlineColourId, t.accent.withAlpha (0.6f));
    search.setColour (juce::CaretComponent::caretColourId, t.text);
    search.setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    search.setIndents (10, 7);
    search.setEscapeAndReturnKeysConsumed (true);
    search.addListener (this);
    search.addKeyListener (this);
    addAndMakeVisible (search);

    list = std::make_unique<List>();
    list->onChoose = [this] (const juce::PluginDescription& d) { choose (d); };
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    rescan.onClick = [this] { catalog.rescanAll(); repaint (footer); };
    rescan.setFontHeight (12.0f);
    addAndMakeVisible (rescan);

    onClose = [this] { close(); };
    refresh();
}

PluginBrowser::~PluginBrowser()
{
    catalog.removeChangeListener (this);
    if (watched != nullptr)
    {
        watched->removeMouseListener (this);
        watched->removeComponentListener (this);
    }
}

void PluginBrowser::componentBeingDeleted (juce::Component&)
{
    watched = nullptr;
    delete this;
}

void PluginBrowser::setQuery (const juce::String& q)
{
    search.setText (q, true);
}

int PluginBrowser::getNumResults() const
{
    int n = 0;
    for (auto& r : list->rows) n += r.isHeader ? 0 : 1;
    return n;
}

void PluginBrowser::refresh()
{
    list->setRows (buildRows (catalog.getEffects(), catalog.getRecent(), search.getText()));
    list->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (1, list->totalHeight()));
    viewport.setViewPosition (0, 0);
}

void PluginBrowser::moveHighlight (int delta)
{
    const auto& rows = list->rows;
    int i = -1;
    for (int j = list->highlight + delta; j >= 0 && j < (int) rows.size(); j += delta > 0 ? 1 : -1)
        if (! rows[(size_t) j].isHeader) { i = j; break; }
    if (i < 0) return;
    list->highlight = i;
    list->repaint();

    // keep it in view
    const int top = list->rowY (i), bottom = top + list->rowH (i);
    const int viewTop = viewport.getViewPositionY(), viewH = viewport.getMaximumVisibleHeight();
    if (top < viewTop + List::headerHeight) viewport.setViewPosition (0, juce::jmax (0, top - List::headerHeight));
    else if (bottom > viewTop + viewH)      viewport.setViewPosition (0, bottom - viewH);
}

bool PluginBrowser::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (k == juce::KeyPress::downKey) { moveHighlight (1); return true; }
    if (k == juce::KeyPress::upKey)   { moveHighlight (-1); return true; }
    if (k == juce::KeyPress::pageDownKey) { for (int i = 0; i < 10; ++i) moveHighlight (1); return true; }
    if (k == juce::KeyPress::pageUpKey)   { for (int i = 0; i < 10; ++i) moveHighlight (-1); return true; }
    return false;
}

void PluginBrowser::textEditorReturnKeyPressed (juce::TextEditor&)
{
    if (juce::isPositiveAndBelow (list->highlight, (int) list->rows.size()) && ! list->rows[(size_t) list->highlight].isHeader)
        choose (list->rows[(size_t) list->highlight].desc);
}

void PluginBrowser::choose (const juce::PluginDescription& d)
{
    auto cb = chosen;
    close();
    if (cb) cb (d);
}

void PluginBrowser::close()
{
    setVisible (false);
    if (current().getComponent() == this) current() = nullptr;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginBrowser> (this)] { delete safe.getComponent(); });
}

void PluginBrowser::mouseDown (const juce::MouseEvent& e)
{
    // Events from the editor's content arrive here too (mouse listener): a press outside the panel closes us.
    if (e.eventComponent == this || isParentOf (e.eventComponent)) return;
    if (! getScreenBounds().reduced (margin).contains (e.getScreenPosition()) && isVisible())
        close();
}

void PluginBrowser::layoutContent (juce::Rectangle<int> r)
{
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    footer = r.removeFromBottom (24);
    rescan.setBounds (footer.removeFromRight (70));
    viewport.setBounds (r);
    list->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (1, list->totalHeight()));
}

void PluginBrowser::paintContent (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    st::paint::fadedLine (g, { (float) viewport.getX(), (float) viewport.getBottom() + 2.5f },
                          { (float) viewport.getRight(), (float) viewport.getBottom() + 2.5f }, juce::Colours::white.withAlpha (0.08f), 1.0f, 0.2f);
    juce::String status;
    if (catalog.isScanning())
    {
        const auto p = catalog.getScanProgress();
        status = "Scanning VST3" + (p.isNotEmpty() ? ": " + p : juce::String()) + ellipsis();
    }
    else
    {
        status = juce::String ((int) catalog.getEffects().size()) + " plug-ins";
        if (! catalog.canScanVst3()) status << "  (VST3 scanning unavailable)";
    }
    g.setFont (st::fonts::regular (11.5f));
    g.setColour (t.textFaint);
    g.drawText (status, footer.withTrimmedTop (4), juce::Justification::centredLeft, true);
}

} // namespace stsplit
