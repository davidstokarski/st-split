#pragma once
#include "Style.h"

namespace stsplit
{

/** Full-width split display: analyser, the bands' crossover responses in their colours, a dashed split line with
    a grip (drag = frequency), one level line + pill handle per band (drag = band level), and a header per band
    column naming its plug-in chain. Click a band to select it, double-click to add a plug-in to it. */
class SplitDisplay : public juce::Component, private ChainHost::Listener
{
public:
    explicit SplitDisplay (StSplitProcessor&);
    ~SplitDisplay() override;

    std::function<void (int band)> onBandSelected;
    std::function<void (int band)> onAddPluginRequested;
    std::function<void (double dt)> onFrame;

    void setSelectedBand (int b) { selected = b; }
    enum class AnalyzerMode { off, post, preAndPost };
    void setAnalyzerMode (AnalyzerMode);

    juce::Range<float> getBandXRange (int band) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float headerHeight = 24.0f, handleTop = 60.0f, handleBottom = 196.0f;

private:
    StSplitProcessor& proc;
    st::SpectrumAnalyzer preSpectrum, postSpectrum;
    AnalyzerMode analyzerMode = AnalyzerMode::post;
    int selected = 0;
    juce::Rectangle<float> plot;

    enum class Target { none, crossover, handle, bandBody };
    struct Hit { Target target = Target::none; int index = -1; };
    Hit hover, drag;
    float dragStartValue = 0.0f;
    juce::Point<float> dragStart;
    juce::RangedAudioParameter* dragParam = nullptr;

    std::array<st::Animated, kMaxBands> handleY, hoverAmt, selectAmt;
    std::array<st::Animated, kMaxCrossovers> xoverHover, xoverPos;
    std::array<juce::String, kMaxBands> chainSummary;
    juce::Path responseCache[kMaxBands];
    float cachedSlope = -1.0f;
    std::array<float, kMaxCrossovers> cachedXover {};
    double sinceRepaint = 0.0;
    std::unique_ptr<st::FrameClock> clock;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float levelMin = -24.0f, levelMax = 24.0f;
    float xForHz (float hz) const noexcept;
    float hzForX (float x) const noexcept;
    float yForLevel (float db) const noexcept;
    float levelForY (float y) const noexcept;
    float level (int band) const;

    int bandAtX (float x) const;
    juce::Rectangle<float> handleBounds (int band) const;
    juce::Rectangle<float> gripBounds (int split) const;
    Hit hitTestAt (juce::Point<float>) const;

    void frame (double dt);
    void rebuildResponses();
    void chainsChanged() override;
    void showBandMenu (int band);

    void drawGrid (juce::Graphics&);
    void drawColumns (juce::Graphics&);
    void drawResponses (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawScale (juce::Graphics&);
    void drawCrossovers (juce::Graphics&);
    void drawHandles (juce::Graphics&);
    void drawBandHeaders (juce::Graphics&);
};

} // namespace stsplit
