#include "Engine.h"

namespace stsplit
{

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels, int maxLatencySamples)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);
    numCh = juce::jlimit (1, 2, numChannels);
    maxLatency = juce::jmax (0, maxLatencySamples);

    crossover.prepare (sr, maxBlock, numCh);
    input.setSize (numCh, maxBlock, false, true, true);
    for (auto& b : bandBuffers) b.setSize (numCh, maxBlock, false, true, true);
    for (auto& b : dryBuffers) b.setSize (numCh, maxBlock, false, true, true);
    for (int b = 0; b < kMaxBands; ++b)
    {
        dryDelay[(size_t) b].prepare (numCh, maxLatency);
        alignDelay[(size_t) b].prepare (numCh, maxLatency);
    }

    inGain.prepare (sr, 20.0);
    outGain.prepare (sr, 20.0);
    for (int b = 0; b < kMaxBands; ++b)
    {
        level[(size_t) b].prepare (sr, 20.0);
        wetMix[(size_t) b].prepare (sr, 20.0);
        audible[(size_t) b].prepare (sr, 8.0);
    }

    setSettings (settings);
    reset();
}

void Engine::reset() noexcept
{
    crossover.reset();
    for (auto& d : dryDelay) d.reset();
    for (auto& d : alignDelay) d.reset();

    inGain.setCurrentAndTarget (st::dsp::dbToGain (settings.inputGainDb));
    outGain.setCurrentAndTarget (st::dsp::dbToGain (settings.outputGainDb));
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& s = settings.bands[(size_t) b];
        level[(size_t) b].setCurrentAndTarget (st::dsp::dbToGain (s.levelDb));
        wetMix[(size_t) b].setCurrentAndTarget (s.mix * settings.mix);
        audible[(size_t) b].setCurrentAndTarget (s.audible ? 1.0f : 0.0f);
        bandPeak[(size_t) b].store (0.0f);
    }
}

void Engine::setSettings (const EngineSettings& s) noexcept
{
    settings = s;
    crossover.setNumBands (kMaxBands);
    crossover.setSlope (s.slope);
    for (int i = 0; i < kMaxCrossovers; ++i)
        crossover.setCrossoverFrequency (i, s.crossovers[(size_t) i]);

    inGain.setTarget (st::dsp::dbToGain (s.inputGainDb));
    outGain.setTarget (st::dsp::dbToGain (s.outputGainDb));
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& band = s.bands[(size_t) b];
        level[(size_t) b].setTarget (st::dsp::dbToGain (band.levelDb));
        wetMix[(size_t) b].setTarget (band.mix * s.mix);
        audible[(size_t) b].setTarget (band.audible ? 1.0f : 0.0f);
    }
}

void Engine::process (juce::AudioBuffer<float>& io, int n, BandProcessor* chains) noexcept
{
    jassert (n <= maxBlock);
    n = juce::jmin (n, maxBlock);
    const int ch = juce::jmin (numCh, io.getNumChannels());
    if (n <= 0 || ch <= 0) return;

    // ---- input gain into a private buffer (the crossover input must not alias its outputs)
    for (int c = 0; c < ch; ++c)
        input.copyFrom (c, 0, io, c, 0, n);
    for (int c = ch; c < numCh; ++c)
        input.clear (c, 0, n);
    {
        auto g = inGain;   // same ramp for every channel
        for (int c = 0; c < numCh; ++c)
        {
            g = inGain;
            auto* x = input.getWritePointer (c);
            for (int i = 0; i < n; ++i) x[i] *= g.getNext();
        }
        inGain = g;
    }

    juce::AudioBuffer<float> inView (input.getArrayOfWritePointers(), numCh, n);
    crossover.process (inView, bandBuffers.data(), n);

    // ---- every band arrives as late as the slowest chain
    std::array<int, kMaxBands> chainLatency {};
    int total = 0;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const int l = chains != nullptr ? juce::jlimit (0, maxLatency, chains->getBandLatency (b)) : 0;
        total = juce::jmax (total, l);
        chainLatency[(size_t) b] = settings.bands[(size_t) b].bypass ? 0 : l;
    }
    latency = total;

    for (int c = 0; c < ch; ++c)
        io.clear (c, 0, n);

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto bi = (size_t) b;
        juce::AudioBuffer<float> wet (bandBuffers[bi].getArrayOfWritePointers(), numCh, n);
        juce::AudioBuffer<float> dry (dryBuffers[bi].getArrayOfWritePointers(), numCh, n);
        for (int c = 0; c < numCh; ++c)
            dry.copyFrom (c, 0, wet, c, 0, n);

        const bool bypass = settings.bands[bi].bypass;
        bool processed = bypass;
        if (! bypass && chains != nullptr)
            processed = chains->processBand (b, wet, n);

        // Dry: aligned with the chain's output.
        dryDelay[bi].setDelay (chainLatency[bi]);
        dryDelay[bi].process (dry, n);

        if (bypass || ! processed)
            for (int c = 0; c < numCh; ++c)
                wet.copyFrom (c, 0, dry, c, 0, n);

        // out = audible * (dry + mix * (wet * level - dry)); bypass passes the band untouched.
        auto lv = level[bi], mx = wetMix[bi], au = audible[bi];
        for (int c = 0; c < numCh; ++c)
        {
            lv = level[bi]; mx = wetMix[bi]; au = audible[bi];
            auto* w = wet.getWritePointer (c);
            const auto* d = dry.getReadPointer (c);
            if (bypass)
                for (int i = 0; i < n; ++i) { mx.getNext(); lv.getNext(); w[i] = d[i] * au.getNext(); }
            else
                for (int i = 0; i < n; ++i)
                {
                    const float m = mx.getNext(), l = lv.getNext();
                    w[i] = (d[i] + m * (w[i] * l - d[i])) * au.getNext();
                }
        }
        level[bi] = lv; wetMix[bi] = mx; audible[bi] = au;

        alignDelay[bi].setDelay (total - chainLatency[bi]);
        alignDelay[bi].process (wet, n);

        float peak = 0.0f;
        for (int c = 0; c < ch; ++c)
        {
            io.addFrom (c, 0, wet, c, 0, n);
            peak = juce::jmax (peak, wet.getMagnitude (c, 0, n));
        }
        bandPeak[bi].store (juce::jmax (peak, bandPeak[bi].load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    }

    auto g = outGain;
    for (int c = 0; c < ch; ++c)
    {
        g = outGain;
        auto* x = io.getWritePointer (c);
        for (int i = 0; i < n; ++i) x[i] *= g.getNext();
    }
    outGain = g;
}

} // namespace stsplit
