#pragma once
#include "../Params.h"

namespace stsplit
{

/** What runs inside each band (the hosted plug-in chains). Implemented by ChainHost; tests use fakes. */
struct BandProcessor
{
    virtual ~BandProcessor() = default;
    /** Processing latency of band `band`'s chain in samples (constant while it doesn't change structure). */
    virtual int getBandLatency (int band) const noexcept = 0;
    /** Processes the band in place. Returns false if the chain could not run this block (e.g. it is being edited);
        the engine then substitutes the latency-aligned dry signal so the output stays continuous. */
    virtual bool processBand (int band, juce::AudioBuffer<float>& buffer, int numSamples) noexcept = 0;
};

struct EngineSettings
{
    st::dsp::Crossover::Slope slope = st::dsp::Crossover::Slope::db24;
    std::array<float, kMaxCrossovers> crossovers {};
    float inputGainDb = 0.0f, outputGainDb = 0.0f, mix = 1.0f;

    struct Band
    {
        float levelDb = 0.0f, mix = 1.0f;
        bool bypass = false;   // skip the chain (band passes straight through)
        bool audible = true;   // mute / solo result
    };
    std::array<Band, kMaxBands> bands {};
};

/** Input gain → phase-coherent Linkwitz-Riley split → per band: chain (BandProcessor), latency-aligned dry/wet mix,
    level, mute/solo → per-band latency compensation so every band arrives together → sum → output gain.

    Latency: every band is delayed to the slowest chain (L = max band latency, bypassed bands included so toggling
    bypass doesn't change the latency reported to the host). Within a band the dry signal is delayed by the chain's
    own latency before mixing, so partial mix never combs.

    prepare(): message thread (allocates). process(): audio thread, allocation-free, numSamples <= maxBlockSize. */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, int maxLatencySamples);
    void reset() noexcept;
    void setSettings (const EngineSettings&) noexcept;

    void process (juce::AudioBuffer<float>& io, int numSamples, BandProcessor* chains) noexcept;

    /** Latency used by the last process() call (= the latency the host should be told). */
    int getLatencySamples() const noexcept { return latency; }
    int getMaxLatencySamples() const noexcept { return maxLatency; }
    int getMaxBlockSize() const noexcept { return maxBlock; }

    const st::dsp::Crossover& getCrossover() const noexcept { return crossover; }

    /** Peak level of each band's output (linear), for the UI. Any thread. */
    float getBandPeak (int band) const noexcept { return bandPeak[(size_t) band].load (std::memory_order_relaxed); }

private:
    double sr = 48000.0;
    int maxBlock = 0, numCh = 2, maxLatency = 0, latency = 0;
    EngineSettings settings;

    st::dsp::Crossover crossover;
    juce::AudioBuffer<float> input;
    std::array<juce::AudioBuffer<float>, st::dsp::Crossover::kMaxBands> bandBuffers;   // crossover writes all of them
    std::array<juce::AudioBuffer<float>, kMaxBands> dryBuffers;
    std::array<st::dsp::DelayLine, kMaxBands> dryDelay, alignDelay;

    st::dsp::LinearSmoother inGain, outGain;
    std::array<st::dsp::LinearSmoother, kMaxBands> level, wetMix, audible;
    std::array<std::atomic<float>, kMaxBands> bandPeak {};
};

} // namespace stsplit
