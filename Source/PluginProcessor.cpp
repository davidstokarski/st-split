#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace stsplit;

namespace
{
    constexpr double kMaxLatencySeconds = 2.0;   // longest chain latency the band alignment can absorb
}

StSplitProcessor::StSplitProcessor()
    : ProcessorBase (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                     createParameterLayout()),
      chains (*this, apvts, undoManager)
{
    // ST Split identity: a cool graphite display (between ST Sat's red haze and ST EQ's charcoal) with a copper
    // accent; the bands carry their own colours (warm low, cool high).
    auto& theme = st::Theme::get();
    theme.displayTop  = juce::Colour (0xff222630);
    theme.displayGlow = juce::Colour (0xff394152);
    theme.bg0 = juce::Colour (0xff0e0f12);
    theme.bg1 = juce::Colour (0xff15161a);
    theme.bg2 = juce::Colour (0xff1c1e23);
    theme.bg3 = juce::Colour (0xff272a31);
    theme.bg4 = juce::Colour (0xff343841);
    theme.stroke = juce::Colour (0xff353841);
    theme.strokeSoft = juce::Colour (0xff1f2126);
    theme.glassPanel = juce::Colour (0xb0262931);
    theme.spectrum = juce::Colour (0xffcfd6e4);
    theme.accent = juce::Colour (0xffe0663a);
    theme.accent2 = juce::Colour (0xffff9466);

    inGain.bind (apvts, ids::inputGain);
    outGain.bind (apvts, ids::outputGain);
    mix.bind (apvts, ids::mix);
    slope.bind (apvts, ids::slope);
    for (int i = 0; i < kMaxCrossovers; ++i)
        crossovers[(size_t) i].bind (apvts, ids::crossover (i));
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& r = bandRefs[(size_t) b];
        r.level.bind (apvts, ids::band (b, "level"));
        r.mix.bind (apvts, ids::band (b, "mix"));
        r.mute.bind (apvts, ids::band (b, "mute"));
        r.solo.bind (apvts, ids::band (b, "solo"));
        r.bypass.bind (apvts, ids::band (b, "bypass"));
    }

    // Presets hold the whole chain (plug-ins and their settings); the factory ones are empty starting points.
    presets.addFactoryPreset ("Init", "Templates", {});
    presets.addFactoryPreset ("Sub Split 120 Hz", "Templates", { { ids::crossover (0), 120.0f }, { ids::slope, 2.0f } });
    presets.addFactoryPreset ("Low End 250 Hz", "Templates", { { ids::crossover (0), 250.0f } });
    presets.addFactoryPreset ("Vocal Body 1.2 kHz", "Templates", { { ids::crossover (0), 1200.0f } });
    presets.addFactoryPreset ("Air 6 kHz", "Templates", { { ids::crossover (0), 6000.0f }, { ids::slope, 0.0f } });

    chains.onLatencyChanged = [this]
    {
        if (juce::MessageManager::existsAndIsCurrentThread()) handleAsyncUpdate();
        else triggerAsyncUpdate();
    };
}

StSplitProcessor::~StSplitProcessor()
{
    unregisterInstance();   // first: views of this instance (elsewhere) still use its members
    cancelPendingUpdate();
}

//==============================================================================
void StSplitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareShell (sampleRate);

    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    const int block = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate, block, channels, (int) std::ceil (sampleRate * kMaxLatencySeconds));
    chains.prepare (sampleRate, block, channels);
    updateEngineSettings();
    engine.reset();
    prepared = true;

    setLatencySamples (juce::jmin (chains.getTotalLatency(), engine.getMaxLatencySamples()));
}

void StSplitProcessor::releaseResources()
{
    chains.release();
    prepared = false;
}

void StSplitProcessor::setNonRealtime (bool b) noexcept
{
    AudioProcessor::setNonRealtime (b);
    chains.setNonRealtime (b);
}

void StSplitProcessor::handleAsyncUpdate()
{
    setLatencySamples (juce::jmin (chains.getTotalLatency(), engine.getMaxLatencySamples()));
}

void StSplitProcessor::updateEngineSettings() noexcept
{
    EngineSettings s;
    s.slope = (st::dsp::Crossover::Slope) juce::jlimit (0, 2, slope.getInt());
    for (int i = 0; i < kMaxCrossovers; ++i)
        s.crossovers[(size_t) i] = crossovers[(size_t) i].get();
    s.inputGainDb = inGain.get();
    s.outputGainDb = outGain.get();
    s.mix = mix.get() * 0.01f;

    bool anySolo = false;
    for (auto& r : bandRefs) anySolo |= r.solo.getBool();

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& r = bandRefs[(size_t) b];
        auto& band = s.bands[(size_t) b];
        band.levelDb = r.level.get();
        band.mix = r.mix.get() * 0.01f;
        band.bypass = r.bypass.getBool();
        band.audible = ! (r.mute.getBool() || (anySolo && ! r.solo.getBool()));
    }
    engine.setSettings (s);
}

void StSplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, total);
    if (! prepared) return;

    inputMeter.push (buffer, total);
    preAnalyzer.push (buffer, total);

    updateEngineSettings();
    const int channels = juce::jmin (2, numOut);
    const int maxBlock = engine.getMaxBlockSize();

    // One scope per host block: the chain snapshot can't change underneath us.
    ChainHost::AudioScope scope (chains, getPlayHead());
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        juce::AudioBuffer<float> chunk (ptrs, channels, n);
        engine.process (chunk, n, &scope);
    }

    outputMeter.push (buffer, total);
    postAnalyzer.push (buffer, total);
}

//==============================================================================
void StSplitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // The hosted plug-ins' current settings go into the tree first (only safe on the message thread; elsewhere the
    // tree holds what was flushed within the last second).
    if (juce::MessageManager::existsAndIsCurrentThread())
        chains.flushStates();
    ProcessorBase::getStateInformation (dest);
}

void StSplitProcessor::setCrossover (int index, float hz, bool asGesture)
{
    if (! juce::isPositiveAndBelow (index, kMaxCrossovers)) return;
    constexpr float minRatio = 1.18f;
    const float lo = index > 0 ? getCrossover (index - 1) * minRatio : 20.0f;
    const float hi = index < kMaxCrossovers - 1 ? getCrossover (index + 1) / minRatio : 20000.0f;
    hz = juce::jlimit (lo, juce::jmax (lo, hi), hz);

    auto& p = param (ids::crossover (index));
    if (asGesture) p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (hz));
    if (asGesture) p.endChangeGesture();
}

juce::AudioProcessorEditor* StSplitProcessor::createEditor()
{
    return new StSplitEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StSplitProcessor();
}
