#include "PluginCatalog.h"

namespace stsplit
{

namespace
{
    juce::File& scannerOverride()
    {
        static juce::File f;
        return f;
    }

    /** Opens each VST3 bundle in the "ST Split Scanner" helper process (see scanner/Main.cpp). */
    struct OutOfProcessScanner : juce::KnownPluginList::CustomScanner
    {
        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            const auto helper = PluginCatalog::getScannerExecutable();
            if (format.getName() != "VST3" || ! helper.existsAsFile())
                return true;   // nothing found, but don't mark the bundle as broken

            juce::TemporaryFile out (".xml");
            juce::ChildProcess child;
            if (! child.start (juce::StringArray { helper.getFullPathName(), "--vst3", fileOrIdentifier,
                                                   "--out", out.getFile().getFullPathName() }, 0))
                return true;   // the host doesn't let us start processes: no VST3 scan, nothing blacklisted

            const auto started = juce::Time::getMillisecondCounter();
            while (child.isRunning())
            {
                if (shouldExit())
                {
                    child.kill();
                    return true;   // cancelled: scanned again next time
                }
                if (juce::Time::getMillisecondCounter() - started > (juce::uint32) PluginCatalog::scanTimeoutSeconds * 1000u)
                {
                    child.kill();
                    return false;  // hung: skip it from now on
                }
                juce::Thread::sleep (15);
            }

            const auto exitCode = child.getExitCode();
            if (exitCode != 0 && exitCode != 2)
                return false;      // crashed (or couldn't report): skip it from now on

            if (auto xml = juce::XmlDocument::parse (out.getFile()))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e)) result.add (d.release());
                }
            return true;
        }
    };
}

juce::File PluginCatalog::getScannerExecutable()
{
    if (scannerOverride() != juce::File()) return scannerOverride();
    // <bundle>/Contents/MacOS/<binary>  ->  <bundle>/Contents/Helpers/ST Split Scanner
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
               .getParentDirectory().getParentDirectory().getChildFile ("Helpers").getChildFile ("ST Split Scanner");
}

std::unique_ptr<juce::KnownPluginList::CustomScanner> PluginCatalog::makeOutOfProcessScanner()
{
    return std::make_unique<OutOfProcessScanner>();
}

void PluginCatalog::setScannerExecutable (const juce::File& f)
{
    scannerOverride() = f;
}

PluginCatalog::PluginCatalog() : juce::Thread ("ST Split plug-in scan")
{
    juce::addDefaultFormatsToManager (formats);
    getDataDirectory().createDirectory();
    // Left by builds that scanned inside the host (it may name a bundle that only crashed because of that).
    getDataDirectory().getChildFile ("VST3 Scan In Progress.txt").deleteFile();
    vst3List.setCustomScanner (makeOutOfProcessScanner());

    if (auto xml = juce::XmlDocument::parse (vst3CacheFile()))
        vst3List.recreateFromXml (*xml);
    loadSettings();

    audioUnits = listAudioUnitEffects();
}

PluginCatalog::~PluginCatalog()
{
    stopThread (4000);
}

juce::File PluginCatalog::getDataDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("ST Audio").getChildFile ("ST Split");
}

bool PluginCatalog::isSelf (const juce::PluginDescription& d)
{
    return d.name.equalsIgnoreCase ("ST Split") && d.manufacturerName.equalsIgnoreCase ("ST Audio");
}

juce::String PluginCatalog::shortFormatName (const juce::PluginDescription& d)
{
    return d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName;
}

std::vector<juce::PluginDescription> PluginCatalog::getEffects() const
{
    std::vector<juce::PluginDescription> all;
    for (auto& d : audioUnits)
        if (! isSelf (d)) all.push_back (d);
    for (auto& d : vst3List.getTypes())
        if (! d.isInstrument && ! isSelf (d)) all.push_back (d);

    auto key = [] (const juce::PluginDescription& d) { return d.manufacturerName.toLowerCase().trim() + "\n" + d.name.toLowerCase().trim(); };

    if (preference != FormatPreference::both)
    {
        const auto preferred = preference == FormatPreference::audioUnit ? juce::String ("AudioUnit") : juce::String ("VST3");
        std::map<juce::String, size_t> best;
        for (size_t i = 0; i < all.size(); ++i)
        {
            auto [it, inserted] = best.emplace (key (all[i]), i);
            if (! inserted && all[i].pluginFormatName == preferred && all[it->second].pluginFormatName != preferred)
                it->second = i;
        }
        std::vector<juce::PluginDescription> unique;
        for (auto& [k, i] : best) unique.push_back (all[i]);
        all = std::move (unique);
    }

    std::sort (all.begin(), all.end(), [] (const auto& a, const auto& b)
    {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        if (m != 0) return m < 0;
        const int n = a.name.compareNatural (b.name);
        return n != 0 ? n < 0 : a.pluginFormatName < b.pluginFormatName;
    });
    return all;
}

void PluginCatalog::refresh()
{
    refreshedOnce = true;
    audioUnits = listAudioUnitEffects();
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    sendChangeMessage();
}

void PluginCatalog::rescanAll()
{
    stopThread (4000);
    vst3List.clear();
    vst3List.clearBlacklistedFiles();
    vst3CacheFile().deleteFile();
    refresh();
}

juce::String PluginCatalog::getScanProgress() const
{
    const juce::ScopedLock sl (progressLock);
    return progress;
}

juce::StringArray PluginCatalog::getSkippedFiles() const
{
    return vst3List.getBlacklistedFiles();
}

void PluginCatalog::run()
{
    juce::AudioPluginFormat* vst3 = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == "VST3") vst3 = f;
    if (vst3 == nullptr || ! canScanVst3()) return;   // never open VST3s inside the host

    {
        // Incremental: unchanged bundles already in the list are not opened again.
        juce::PluginDirectoryScanner scanner (vst3List, *vst3, vst3->getDefaultLocationsToSearch(), true,
                                              deadMansPedalFile(), false);
        juce::String name;
        for (;;)
        {
            if (threadShouldExit()) break;
            {
                const juce::ScopedLock sl (progressLock);
                progress = juce::File::createFileWithoutCheckingPath (scanner.getNextPluginFileThatWillBeScanned())
                               .getFileNameWithoutExtension();
            }
            if (! scanner.scanNextFile (true, name)) break;
            sendChangeMessage();
        }
        for (auto& f : scanner.getFailedFiles())
            vst3List.addToBlacklist (f);
    }

    // Drop entries whose bundle disappeared.
    for (auto& d : vst3List.getTypes())
        if (! juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier).exists())
            vst3List.removeType (d);

    if (auto xml = vst3List.createXml())
        xml->writeTo (vst3CacheFile());

    {
        const juce::ScopedLock sl (progressLock);
        progress = {};
    }
    sendChangeMessage();
}

void PluginCatalog::noteUsed (const juce::PluginDescription& d)
{
    const auto id = d.createIdentifierString();
    recent.removeString (id);
    recent.insert (0, id);
    while (recent.size() > 10) recent.remove (recent.size() - 1);
    saveSettings();
}

std::vector<juce::PluginDescription> PluginCatalog::getRecent() const
{
    std::vector<juce::PluginDescription> result;
    const auto effects = getEffects();
    for (auto& id : recent)
        for (auto& d : effects)
            if (d.createIdentifierString() == id) { result.push_back (d); break; }
    return result;
}

void PluginCatalog::setPreference (FormatPreference p)
{
    preference = p;
    preferenceChosen = true;
    saveSettings();
    sendChangeMessage();
}

void PluginCatalog::setDefaultPreferenceFor (juce::AudioProcessor::WrapperType w)
{
    if (preferenceChosen) return;
    preference = w == juce::AudioProcessor::wrapperType_VST3 ? FormatPreference::vst3 : FormatPreference::audioUnit;
}

void PluginCatalog::loadSettings()
{
    auto xml = juce::XmlDocument::parse (settingsFile());
    if (xml == nullptr) return;
    if (xml->hasAttribute ("formatPreference"))
    {
        preference = (FormatPreference) juce::jlimit (0, 2, xml->getIntAttribute ("formatPreference"));
        preferenceChosen = true;
    }
    if (auto* r = xml->getChildByName ("RECENT"))
        for (auto* e : r->getChildIterator())
            recent.add (e->getStringAttribute ("id"));
}

void PluginCatalog::saveSettings() const
{
    juce::XmlElement xml ("STSPLIT_SETTINGS");
    if (preferenceChosen) xml.setAttribute ("formatPreference", (int) preference);
    auto* r = xml.createNewChildElement ("RECENT");
    for (auto& id : recent)
        r->createNewChildElement ("PLUGIN")->setAttribute ("id", id);
    xml.writeTo (settingsFile());
}

} // namespace stsplit
