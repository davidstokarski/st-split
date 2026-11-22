#pragma once
#include "Style.h"

namespace stsplit
{

/** Full-window split display, FabFilter-style (Saturn / Pro-MB): everything is edited on it.

    - Each band column: a header with the band's name and range, and S / M buttons that appear while the band is
      hovered (hold = solo / mute only while held, Cmd-click = exclusive). The band's insert stack (InsertStack, a
      child of the editor) sits under the header; the editor places it using getStackArea().
    - The split: drag the line (or the frequency pill on top of it) to move it, double-click to type a frequency,
      Cmd-click to reset; the slope button under the pill opens the slopes.
    - Level buttons: drag up / down = band level, sideways = move the band's split(s) (the drag locks to the first
      direction), double-click = type a level, Cmd-click = reset, wheel = level. The selected band's button is lit.
    - Click a band to select it (the band panel glides under it), double-click empty space to add a plug-in.
    - Analyser, the bands' crossover responses, a frequency scale along the bottom. */
class SplitDisplay : public juce::Component, private ChainHost::Listener
{
public:
    explicit SplitDisplay (StSplitProcessor&);
    ~SplitDisplay() override;

    std::function<void (int band)> onBandSelected;
    std::function<void (int band)> onAddPluginRequested;
    std::function<void (double dt)> onFrame;

    void setSelectedBand (int b) { selected = b; }
    int getHoveredBand() const noexcept { return hover.target == Target::none ? -1 : hoverBand; }
    enum class AnalyzerMode { off, post, preAndPost };
    void setAnalyzerMode (AnalyzerMode);

    /** Current (animated) x-range of a band column. */
    juce::Range<float> getBandXRange (int band) const;
    /** Where the band's insert stack goes (local coordinates), for `stackHeight`. */
    juce::Rectangle<int> getStackArea (int band, int stackHeight) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float headerY = 50.0f, stackTop = 102.0f, handleTop = 300.0f, handleBottom = 448.0f, scaleHeight = 24.0f;

private:
    StSplitProcessor& proc;
    st::SpectrumAnalyzer preSpectrum, postSpectrum;
    AnalyzerMode analyzerMode = AnalyzerMode::post;
    int selected = 0, hoverBand = -1;
    juce::Rectangle<float> plot;

    enum class Target { none, crossover, slope, handle, solo, mute, bandBody };
    struct Hit { Target target = Target::none; int index = -1; };
    Hit hover, drag;
    enum class Axis { undecided, vertical, horizontal };
    Axis dragAxis = Axis::undecided;
    float dragStartValue = 0.0f;
    std::array<float, kMaxCrossovers> dragStartXover {};
    juce::Point<float> dragStart;
    juce::RangedAudioParameter* dragParam = nullptr;
    double pressTime = 0.0;
    bool pressToggled = false;

    std::array<st::Animated, kMaxBands> handleY, hoverAmt, selectAmt;
    std::array<st::Animated, kMaxCrossovers> xoverHover, xoverPos;
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
    float floorY() const noexcept { return plot.getBottom() - scaleHeight; }

    int bandAtX (float x) const;
    juce::Rectangle<float> handleBounds (int band) const;
    juce::Rectangle<float> freqPill (int split) const;
    juce::Rectangle<float> slopeButton (int split) const;
    juce::Rectangle<float> headerLabel (int band) const;
    juce::Rectangle<float> soloButton (int band) const;
    juce::Rectangle<float> muteButton (int band) const;
    bool showsBandButtons (int band) const;
    Hit hitTestAt (juce::Point<float>) const;

    void frame (double dt);
    void rebuildResponses();
    void chainsChanged() override { repaint(); }
    void showBandMenu (int band);
    void showSlopeMenu();
    void promptLevel (int band);
    void promptFrequency (int split);
    void resetParam (juce::RangedAudioParameter&);
    void toggleBandFlag (int band, const char* suffix, bool exclusive);

    void drawColumns (juce::Graphics&);
    void drawResponses (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawScale (juce::Graphics&);
    void drawHeaders (juce::Graphics&);
    void drawCrossovers (juce::Graphics&);
    void drawHandles (juce::Graphics&);
};

} // namespace stsplit
