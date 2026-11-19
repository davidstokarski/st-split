#pragma once
#include "../PluginProcessor.h"

/** ST Split's Logic Pro look: flat neutral surfaces with hairline edges, Logic's blue selection, M / S buttons that
    fill when active, meters from green to red. Colours come from the Theme tokens set in the processor; this file
    is the one place that turns them into surfaces. */
namespace stsplit::style
{
    constexpr float panelRadius = 6.0f, controlRadius = 4.0f;

    /** Toolbar / footer strip: bg3 with a dark hairline on the edge facing the content. */
    void strip (juce::Graphics&, juce::Rectangle<float>, bool edgeAtBottom);
    /** Raised panel (racks): bg2, hairline border, faint top highlight. */
    void panel (juce::Graphics&, juce::Rectangle<float>, float radius = panelRadius);
    /** Recessed area (display): bg0 with a hairline border. */
    void well (juce::Graphics&, juce::Rectangle<float>, float radius = panelRadius);
    /** Flat Logic button face. `fill` overrides the face colour (e.g. an active M / S button). */
    void button (juce::Graphics&, juce::Rectangle<float>, bool hover, bool down, juce::Colour fill = {});
    /** Text colour that reads on top of `fill`. */
    juce::Colour textOn (juce::Colour fill);

    /** Logic-style toggle for a bool parameter: "M" / "S" letters or an icon; fills with `onColour` when active. */
    class Toggle : public st::ParameterControl
    {
    public:
        Toggle (juce::RangedAudioParameter&, juce::UndoManager*, const juce::String& text, juce::Colour onColour);
        void setIcon (st::icons::Icon i) { icon = i; repaint(); }
        /** Inverted toggles are "on" when the parameter is off (a power button bound to a bypass parameter). */
        void setInverted (bool b) { inverted = b; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String text;
        juce::Colour onColour;
        std::optional<st::icons::Icon> icon;
        bool inverted = false, pressed = false;
        bool isOn() const noexcept { return (normValue >= 0.5f) != inverted; }
    };

    /** Vertical peak meter (Logic colours: green, yellow above -6 dB, red above 0 dB). */
    class Meter : public juce::Component, private juce::Timer
    {
    public:
        explicit Meter (std::function<float()> peakSource);
        void paint (juce::Graphics&) override;

    private:
        std::function<float()> source;
        float levelDb = -100.0f, holdDb = -100.0f;
        int holdFrames = 0;
        void timerCallback() override;
    };
}
