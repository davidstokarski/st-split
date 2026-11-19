// Audio Unit enumeration from the component registry (no plug-in code is loaded).
// Kept in its own file: it needs the AudioToolbox C API next to JUCE.
#include "PluginCatalog.h"

#if JUCE_MAC
 #include <AudioToolbox/AudioComponent.h>
 #include <AudioToolbox/AUComponent.h>
#endif

namespace stsplit
{

#if JUCE_MAC
namespace
{
    juce::String osTypeToString (OSType t)
    {
        const juce::juce_wchar s[4] = { (juce::juce_wchar) ((t >> 24) & 0xff), (juce::juce_wchar) ((t >> 16) & 0xff),
                                        (juce::juce_wchar) ((t >> 8) & 0xff), (juce::juce_wchar) (t & 0xff) };
        return juce::String (s, 4);
    }

    juce::String fromCF (CFStringRef s)
    {
        if (s == nullptr) return {};
        const auto length = CFStringGetLength (s);
        const auto maxBytes = CFStringGetMaximumSizeForEncoding (length, kCFStringEncodingUTF8) + 1;
        juce::HeapBlock<char> buffer ((size_t) maxBytes, true);
        if (! CFStringGetCString (s, buffer, maxBytes, kCFStringEncodingUTF8)) return {};
        return juce::String::fromUTF8 (buffer);
    }
}

std::vector<juce::PluginDescription> listAudioUnitEffects()
{
    std::vector<juce::PluginDescription> result;
    AudioComponent comp = nullptr;

    for (;;)
    {
        AudioComponentDescription d {};
        comp = AudioComponentFindNext (comp, &d);
        if (comp == nullptr) break;
        if (AudioComponentGetDescription (comp, &d) != noErr) continue;
        if (d.componentType != kAudioUnitType_Effect && d.componentType != kAudioUnitType_MusicEffect) continue;

        juce::String name, manufacturer;
        CFStringRef cfName = nullptr;
        if (AudioComponentCopyName (comp, &cfName) == noErr && cfName != nullptr)
        {
            name = fromCF (cfName);
            CFRelease (cfName);
        }
        if (name.containsChar (':'))
        {
            manufacturer = name.upToFirstOccurrenceOf (":", false, false).trim();
            name = name.fromFirstOccurrenceOf (":", false, false).trim();
        }
        if (name.isEmpty()) name = osTypeToString (d.componentSubType);
        if (manufacturer.isEmpty()) manufacturer = osTypeToString (d.componentManufacturer);

        juce::String version;
        UInt32 v = 0;
        if (AudioComponentGetVersion (comp, &v) == noErr)
            version << (int) (v >> 16) << "." << (int) ((v >> 8) & 0xff) << "." << (int) (v & 0xff);

        juce::PluginDescription desc;
        desc.name = name;
        desc.descriptiveName = name;
        desc.manufacturerName = manufacturer;
        desc.version = version;
        desc.pluginFormatName = "AudioUnit";
        desc.category = "Effect";
        desc.isInstrument = false;
        desc.numInputChannels = 2;
        desc.numOutputChannels = 2;
        // Same identifier / unique ID as juce::AudioUnitPluginFormat, so instances and saved sessions match.
        desc.fileOrIdentifier = "AudioUnit:Effects/" + osTypeToString (d.componentType) + ","
                              + osTypeToString (d.componentSubType) + "," + osTypeToString (d.componentManufacturer);
        desc.uniqueId = desc.deprecatedUid = ((int) d.componentType) ^ ((int) d.componentSubType) ^ ((int) d.componentManufacturer);
        result.push_back (desc);
    }

    return result;
}
#else
std::vector<juce::PluginDescription> listAudioUnitEffects() { return {}; }
#endif

} // namespace stsplit
