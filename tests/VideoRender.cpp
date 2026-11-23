// Use-case videos of the real editor (website), on the shell's video kit (st_shell/testing/st_video.h): a
// synthesized mix plays through the processor (hosting Apple's Audio Units) while a scripted pointer works the UI.
//   StSplitPluginTests --video <dir> [scene ...]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/SplitDisplay.h"
#include "host/PluginCatalog.h"
#include <st_shell/testing/st_video.h>

using st::video::Director;
using st::video::Music;
using st::video::Scene;

namespace
{
std::optional<juce::PluginDescription> appleAU (const juce::String& subtype)
{
    for (auto& d : stsplit::listAudioUnitEffects())
        if (d.fileOrIdentifier.endsWith ("," + subtype + ",appl")) return d;
    return std::nullopt;
}

void setup (juce::AudioProcessor& ap, float splitHz, int band, const juce::String& subtype)
{
    auto& p = dynamic_cast<StSplitProcessor&> (ap);
    p.presets.loadPreset (0);
    p.getChains().handlePendingChanges();
    if (auto d = appleAU (subtype)) p.getChains().addPlugin (band, 0, *d);
    st::video::setParam (p, stsplit::ids::crossover (0), splitHz);
    p.uiState.setProperty ("selectedBand", 0, nullptr);
}

/** ST Split places: a band's open area (between its plug-ins and its level button) and the split line. */
struct Split
{
    Director& d;
    stsplit::SplitDisplay& display() const { return *d.findType<stsplit::SplitDisplay>(); }
    juce::Point<float> inBand (int band, float y = 250.0f) const
    {
        const auto r = display().getBandXRange (band);
        return d.pointIn (display(), { r.getStart() + r.getLength() * 0.5f, y });
    }
    juce::Point<float> split (float y = 250.0f) const { return d.pointIn (display(), { display().getBandXRange (0).getEnd(), y }); }
    juce::Point<float> knob (int band, const char* suffix) const { return d.controlCentre (stsplit::ids::band (band, suffix)); }
};

std::vector<Scene> scenes()
{
    std::vector<Scene> v;

    // 1. Distortion on the top only: the kick and bass stay clean under the split
    v.push_back ({ "distort-highs", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        setup (p, 900.0f, 1, "dist");
        m.drums = 1.0f; m.bass = 0.9f; m.pad = 0.3f;
    }, [] (Director& dir)
    {
        auto s = std::make_shared<Split> (Split { dir });
        dir.wait (1.3);
        dir.move ([s] { return s->inBand (1); }, 1.0);
        dir.click();                                              // select the high band (the distortion)
        dir.wait (0.5);
        dir.move ([s] { return s->split(); }, 1.0);
        dir.dragBy ({ -105.0f, 0.0f }, 1.4);                     // split down to ~300 Hz
        dir.wait (0.5);
        dir.move ([s] { return s->knob (1, "mix"); }, 1.0);
        dir.dragBy ({ 0.0f, 60.0f }, 1.2);                        // blend: about half distorted
        dir.move ([s] { return s->knob (1, "level"); }, 0.8);
        dir.dragBy ({ 0.0f, 25.0f }, 0.9);                        // tuck it under
        dir.wait (2.6);
    } });

    // 2. A delay only on the vocal's top end: the body stays dry and upfront
    v.push_back ({ "delay-top", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        setup (p, 2500.0f, 1, "dely");
        m.vocal = 1.0f; m.sib = 0.6f; m.pad = 0.3f; m.drums = 0.25f; m.bass = 0.3f;
    }, [] (Director& dir)
    {
        auto s = std::make_shared<Split> (Split { dir });
        dir.wait (1.3);
        dir.move ([s] { return s->inBand (1); }, 1.0);
        dir.click();
        dir.wait (0.5);
        dir.move ([s] { return s->split(); }, 1.0);
        dir.dragBy ({ -40.0f, 0.0f }, 1.1);                      // split a little lower
        dir.move ([s] { return s->knob (1, "mix"); }, 1.0);
        dir.dragBy ({ 0.0f, 45.0f }, 1.1);
        dir.move ([s] { return s->inBand (0); }, 1.1);
        dir.click();                                              // the dry low band
        dir.move ([s] { return s->knob (0, "level"); }, 0.9);
        dir.dragBy ({ 0.0f, -20.0f }, 0.8);
        dir.wait (2.4);
    } });
    return v;
}
}

int runVideoRender (int argc, char** argv);
int runVideoRender (int argc, char** argv)
{
    return st::video::run (argc, argv, "st-split", [] { return std::make_unique<StSplitProcessor>(); }, scenes());
}
