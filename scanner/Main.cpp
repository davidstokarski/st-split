// ST Split Scanner: loads ONE VST3 bundle in its own process and writes what it contains.
//
//   "ST Split Scanner" --vst3 <bundle path> --out <xml file>
//
// ST Split never loads an unknown plug-in into the host's process just to list it: a plug-in that crashes or hangs
// while being opened only takes this helper down, and ST Split skips that bundle. Exit codes: 0 = plug-ins found,
// 2 = loaded but nothing usable inside, 1 = bad arguments, 3 = couldn't write the result.
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cstdlib>

int main (int argc, char* argv[])
{
    const juce::StringArray args (argv + 1, argc - 1);
    const int vst3Index = args.indexOf ("--vst3"), outIndex = args.indexOf ("--out");
    if (vst3Index < 0 || outIndex < 0 || args[vst3Index + 1].isEmpty() || args[outIndex + 1].isEmpty())
        return 1;

    const juce::String bundle = args[vst3Index + 1];
    const juce::File out (args[outIndex + 1]);

    juce::ScopedJuceInitialiser_GUI init;   // some plug-ins expect a message manager on the main thread

    juce::OwnedArray<juce::PluginDescription> found;
    {
        juce::VST3PluginFormatHeadless format;
        format.findAllTypesForFile (found, bundle);
    }

    juce::XmlElement root ("STSPLIT_SCAN");
    for (auto* d : found)
        if (auto xml = d->createXml())
            root.addChildElement (xml.release());

    if (! root.writeTo (out))
        std::_Exit (3);

    // Skip static destructors: whatever the plug-in left behind must not get a chance to hang or crash us now.
    std::_Exit (found.isEmpty() ? 2 : 0);
}
