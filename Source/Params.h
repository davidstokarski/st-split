#pragma once
#include <st_shell/st_shell.h>

namespace stsplit
{

/** One split point = two bands. The engine, IDs and UI are written for kMaxBands so more split points can be
    added later without renaming anything (IDs follow ST Sat: "xover_<n>", "b<n>_<suffix>"). */
constexpr int kMaxBands = 2;
constexpr int kMaxCrossovers = kMaxBands - 1;
/** Hosted plug-ins per band. */
constexpr int kMaxSlots = 8;

namespace ids
{
    inline const juce::String inputGain  = "in_gain";
    inline const juce::String outputGain = "out_gain";
    inline const juce::String mix        = "mix";
    inline const juce::String slope      = "xover_slope";

    inline juce::String crossover (int i) { return "xover_" + juce::String (i + 1); }

    // Per-band parameter suffixes. Full ID: "b<n>_<suffix>", n = 1..kMaxBands
    inline const juce::StringArray bandSuffixes { "level", "mix", "mute", "solo", "bypass" };
    inline juce::String band (int b, const juce::String& suffix) { return "b" + juce::String (b + 1) + "_" + suffix; }
}

namespace defaults
{
    constexpr float crossovers[kMaxCrossovers] = { 400.0f };
}

/** Band names shown in the UI ("Low", "High"; "Mid" in between once there are more bands). */
juce::String getBandName (int band, int numBands = kMaxBands);
/** Identity colour of a band (warm for the bottom, cool for the top). */
juce::Colour getBandColour (int band, int numBands = kMaxBands);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace stsplit
