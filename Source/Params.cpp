#include "Params.h"

namespace stsplit
{

juce::String getBandName (int band, int numBands)
{
    if (band <= 0) return "Low";
    if (band >= numBands - 1) return "High";
    return numBands == 3 ? "Mid" : "Mid " + juce::String (band);
}

juce::Colour getBandColour (int band, int numBands)
{
    const juce::Colour low (0xfff0a23c), high (0xff3ec3d8);   // amber low, cyan high
    if (numBands <= 1) return low;
    return low.interpolatedWith (high, (float) band / (float) (numBands - 1));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace st::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (makeFloat (ids::inputGain, "Input", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
    global->addChild (makeFloat (ids::outputGain, "Output", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
    global->addChild (makeFloat (ids::mix, "Mix", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
    global->addChild (makeChoice (ids::slope, "Split Slope", { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1));
    for (int i = 0; i < kMaxCrossovers; ++i)
        global->addChild (makeFloat (ids::crossover (i), "Split " + juce::String (i + 1) + " Frequency",
                                     logarithmic (20.0f, 20000.0f), defaults::crossovers[i], hzAttributes()));
    layout.add (std::move (global));

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto name = getBandName (b);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), name, "|");
        group->addChild (makeFloat (ids::band (b, "level"), name + " Level", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (ids::band (b, "mix"), name + " Mix", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
        group->addChild (makeBool (ids::band (b, "mute"), name + " Mute", false));
        group->addChild (makeBool (ids::band (b, "solo"), name + " Solo", false));
        group->addChild (makeBool (ids::band (b, "bypass"), name + " Bypass", false));
        layout.add (std::move (group));
    }

    return layout;
}

} // namespace stsplit
