#pragma once
#include "Params.h"
#include "dsp/Engine.h"
#include "host/ChainHost.h"

class StSplitProcessor : public st::ProcessorBase, private juce::AsyncUpdater
{
public:
    StSplitProcessor();
    ~StSplitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    void setNonRealtime (bool) noexcept override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return chains.getTailSeconds(); }

    void getStateInformation (juce::MemoryBlock&) override;

    //==============================================================================
    stsplit::ChainHost& getChains() noexcept { return chains; }
    stsplit::Engine& getEngine() noexcept { return engine; }
    juce::RangedAudioParameter& bandParam (int band, const juce::String& suffix) { return param (stsplit::ids::band (band, suffix)); }
    float getCrossover (int i) const noexcept { return crossovers[(size_t) i].get(); }

    /** Moves split point `index` keeping split points ordered (message thread). */
    void setCrossover (int index, float hz, bool asGesture);

private:
    stsplit::Engine engine;
    stsplit::ChainHost chains;
    bool prepared = false;

    st::params::Ref inGain, outGain, mix, slope;
    std::array<st::params::Ref, stsplit::kMaxCrossovers> crossovers;
    struct BandRefs { st::params::Ref level, mix, mute, solo, bypass; };
    std::array<BandRefs, stsplit::kMaxBands> bandRefs;

    void updateEngineSettings() noexcept;
    void handleAsyncUpdate() override;   // latency changes → host

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StSplitProcessor)
};
