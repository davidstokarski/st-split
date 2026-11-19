#pragma once
#include "../Params.h"

namespace stsplit
{

/** The user's effect plug-ins, shared by every ST Split instance in the process (juce::SharedResourcePointer).

    - Audio Units come straight from the system's component registry (no plug-in is loaded to list them), so the
      list is complete and instant.
    - VST3s are scanned on a background thread with JUCE's PluginDirectoryScanner (incremental: only new or changed
      bundles are opened; moduleinfo.json when present). A crash during a scan is remembered in a "dead man's pedal"
      file and that bundle is skipped next time. Results are cached in
      ~/Library/Application Support/ST Audio/ST Split/.
    - ST Split itself is never offered (no recursion).

    All public functions are message-thread only unless noted. Listen with ChangeListener for scan progress. */
class PluginCatalog : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    PluginCatalog();
    ~PluginCatalog() override;

    enum class FormatPreference { audioUnit = 0, vst3 = 1, both = 2 };

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formats; }

    /** Insertable effects, sorted by manufacturer then name. With audioUnit / vst3, a plug-in installed in both
        formats is listed once, in the preferred format. */
    std::vector<juce::PluginDescription> getEffects() const;

    /** Re-reads the Audio Unit registry and starts an incremental VST3 scan unless one is running. Cheap to call. */
    void refresh();
    /** Forgets cached VST3 results (and skipped bundles) and scans everything again. */
    void rescanAll();
    /** True until refresh() has been called once in this process (the list may be stale / incomplete). */
    bool needsInitialRefresh() const noexcept { return ! refreshedOnce; }

    bool isScanning() const noexcept { return isThreadRunning(); }
    /** Name of the bundle being scanned (any thread). */
    juce::String getScanProgress() const;
    /** Bundles skipped because they crashed or failed while scanning. */
    juce::StringArray getSkippedFiles() const;

    void noteUsed (const juce::PluginDescription&);
    std::vector<juce::PluginDescription> getRecent() const;

    FormatPreference getPreference() const noexcept { return preference; }
    void setPreference (FormatPreference);
    /** Picks the default preference for a host: AU in AU hosts, VST3 in VST3 hosts (until the user chooses). */
    void setDefaultPreferenceFor (juce::AudioProcessor::WrapperType);

    /** True for ST Split itself (in any format). */
    static bool isSelf (const juce::PluginDescription&);
    /** "AU" / "VST3". */
    static juce::String shortFormatName (const juce::PluginDescription&);

    static juce::File getDataDirectory();

private:
    juce::AudioPluginFormatManager formats;
    std::vector<juce::PluginDescription> audioUnits;
    juce::KnownPluginList vst3List;
    juce::StringArray recent;   // identifier strings, most recent first
    FormatPreference preference = FormatPreference::audioUnit;
    bool preferenceChosen = false, refreshedOnce = false;

    mutable juce::CriticalSection progressLock;
    juce::String progress;

    void run() override;
    void loadSettings();
    void saveSettings() const;
    juce::File vst3CacheFile() const    { return getDataDirectory().getChildFile ("VST3 Plug-ins.xml"); }
    juce::File deadMansPedalFile() const { return getDataDirectory().getChildFile ("VST3 Scan In Progress.txt"); }
    juce::File settingsFile() const     { return getDataDirectory().getChildFile ("Settings.xml"); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};

/** Lists installed Audio Unit effects (aufx / aumf) from the component registry without instantiating them.
    Descriptions match the ones JUCE's AudioUnitPluginFormat produces (same identifier and unique ID). */
std::vector<juce::PluginDescription> listAudioUnitEffects();

} // namespace stsplit
