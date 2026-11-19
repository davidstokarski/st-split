#include "ChainHost.h"

namespace stsplit
{

//==============================================================================
struct ChainHost::Slot : juce::AudioProcessorListener
{
    explicit Slot (ChainHost& h) : host (h) {}
    ~Slot() override
    {
        if (instance != nullptr) instance->removeListener (this);
    }

    ChainHost& host;
    juce::String uid;
    juce::PluginDescription desc;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    Status status = Status::loading;
    juce::String error;
    bool bypassed = false, instancePrepared = false;
    int latency = 0, inChannels = 0, outChannels = 0, loadGeneration = 0;
    juce::AudioBuffer<float> scratch;
    std::atomic<bool> stateChanged { false };

    // Called from any thread (often the audio thread for automation inside the plug-in): flags only.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { stateChanged = true; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.latencyChanged)
        {
            host.latencyDirty = true;
            host.triggerAsyncUpdate();
        }
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged)
            stateChanged = true;
    }
};

//==============================================================================
ChainHost::ChainHost (juce::AudioProcessor& o, juce::AudioProcessorValueTreeState& s, juce::UndoManager& u)
    : owner (o), apvts (s), undo (u)
{
    catalog->setDefaultPreferenceFor (owner.wrapperType);
    midi.ensureSize (4096);
    getChainsTree();
    apvts.state.addListener (this);
    startTimer (1000);
}

ChainHost::~ChainHost()
{
    stopTimer();
    cancelPendingUpdate();
    apvts.state.removeListener (this);

    {
        const juce::ScopedLock sl (audioLock);
        live = {};
    }
    for (auto& band : bands)
        for (auto& slot : band)
            listeners.call ([&] (Listener& l) { l.slotRemoved (slot->uid); });

    const juce::ScopedLock sl (structureLock);
    for (auto& band : bands) band.clear();
}

//==============================================================================
juce::ValueTree ChainHost::getChainsTree()
{
    const juce::ScopedValueSetter<bool> svs (ignoreTreeChanges, true);
    auto chains = apvts.state.getChildWithName (chainsType);
    if (! chains.isValid())
    {
        chains = juce::ValueTree (chainsType);
        apvts.state.appendChild (chains, nullptr);
    }
    // Exactly kMaxBands BAND children (sessions saved with more bands keep their extra trees untouched).
    int numBands = 0;
    for (const auto& c : chains)
        if (c.hasType (bandType)) ++numBands;
    for (; numBands < kMaxBands; ++numBands)
        chains.appendChild (juce::ValueTree (bandType), nullptr);
    return chains;
}

juce::ValueTree ChainHost::getBandTree (int band)
{
    int i = 0;
    for (auto c : getChainsTree())
        if (c.hasType (bandType) && i++ == band) return c;
    jassertfalse;
    return {};
}

juce::ValueTree ChainHost::findSlotTree (const juce::String& uid)
{
    for (auto bandTree : getChainsTree())
        for (auto slot : bandTree)
            if (slot.hasType (slotType) && slot[uidProp].toString() == uid) return slot;
    return {};
}

bool ChainHost::isInChains (const juce::ValueTree& t)
{
    for (auto p = t; p.isValid(); p = p.getParent())
        if (p.hasType (chainsType)) return true;
    return false;
}

ChainHost::Slot* ChainHost::findSlot (const juce::String& uid) const
{
    for (auto& band : bands)
        for (auto& s : band)
            if (s->uid == uid) return s.get();
    return nullptr;
}

//==============================================================================
bool ChainHost::canAdd (int band) const
{
    return juce::isPositiveAndBelow (band, kMaxBands) && getNumSlots (band) < kMaxSlots;
}

juce::String ChainHost::addPlugin (int band, int index, const juce::PluginDescription& desc)
{
    if (! juce::isPositiveAndBelow (band, kMaxBands)) return {};
    auto bandTree = getBandTree (band);
    if (bandTree.getNumChildren() >= kMaxSlots) return {};

    juce::ValueTree slot (slotType);
    const auto uid = juce::Uuid().toString();
    slot.setProperty (uidProp, uid, nullptr);
    slot.setProperty (bypassProp, false, nullptr);
    if (auto xml = desc.createXml())
        slot.appendChild (juce::ValueTree::fromXml (*xml), nullptr);

    undo.beginNewTransaction ("Add " + desc.name);
    bandTree.addChild (slot, juce::jlimit (0, bandTree.getNumChildren(), index), &undo);
    catalog->noteUsed (desc);
    handlePendingChanges();
    return uid;
}

void ChainHost::removeSlot (const juce::String& uid)
{
    auto tree = findSlotTree (uid);
    if (! tree.isValid()) return;
    if (auto* s = findSlot (uid)) flushSlot (*s);   // so undo brings it back exactly as it was

    undo.beginNewTransaction ("Remove " + getDescription (uid).name);
    tree.getParent().removeChild (tree, &undo);
    handlePendingChanges();
}

void ChainHost::moveSlot (const juce::String& uid, int toBand, int toIndex)
{
    auto tree = findSlotTree (uid);
    if (! tree.isValid() || ! juce::isPositiveAndBelow (toBand, kMaxBands)) return;
    auto from = tree.getParent();
    auto to = getBandTree (toBand);
    const int fromIndex = from.indexOf (tree);

    if (from == to)
    {
        int target = juce::jlimit (0, to.getNumChildren(), toIndex);
        if (target > fromIndex) --target;
        if (target == fromIndex) return;
        undo.beginNewTransaction ("Move " + getDescription (uid).name);
        to.moveChild (fromIndex, target, &undo);
    }
    else
    {
        if (to.getNumChildren() >= kMaxSlots) return;
        undo.beginNewTransaction ("Move " + getDescription (uid).name + " to " + getBandName (toBand));
        from.removeChild (tree, &undo);
        to.addChild (tree, juce::jlimit (0, to.getNumChildren(), toIndex), &undo);
    }
    handlePendingChanges();
}

void ChainHost::replacePlugin (const juce::String& uid, const juce::PluginDescription& desc)
{
    auto tree = findSlotTree (uid);
    if (! tree.isValid()) return;
    auto parent = tree.getParent();
    const int index = parent.indexOf (tree);
    if (auto* s = findSlot (uid)) flushSlot (*s);

    juce::ValueTree slot (slotType);
    slot.setProperty (uidProp, juce::Uuid().toString(), nullptr);
    slot.setProperty (bypassProp, false, nullptr);
    if (auto xml = desc.createXml())
        slot.appendChild (juce::ValueTree::fromXml (*xml), nullptr);

    undo.beginNewTransaction ("Replace with " + desc.name);
    parent.removeChild (tree, &undo);
    parent.addChild (slot, index, &undo);
    catalog->noteUsed (desc);
    handlePendingChanges();
}

void ChainHost::setSlotBypassed (const juce::String& uid, bool b)
{
    auto tree = findSlotTree (uid);
    if (! tree.isValid() || (bool) tree[bypassProp] == b) return;
    undo.beginNewTransaction (juce::String (b ? "Bypass " : "Enable ") + getDescription (uid).name);
    tree.setProperty (bypassProp, b, &undo);
    handlePendingChanges();
}

//==============================================================================
std::vector<ChainHost::SlotInfo> ChainHost::getSlots (int band) const
{
    std::vector<SlotInfo> result;
    if (! juce::isPositiveAndBelow (band, kMaxBands)) return result;
    for (auto& s : bands[(size_t) band])
    {
        SlotInfo i;
        i.uid = s->uid;
        i.name = s->desc.name;
        i.manufacturer = s->desc.manufacturerName;
        i.format = PluginCatalog::shortFormatName (s->desc);
        i.error = s->error;
        i.status = s->status;
        i.bypassed = s->bypassed;
        i.latencySamples = s->latency;
        result.push_back (i);
    }
    return result;
}

int ChainHost::getNumSlots (int band) const
{
    return juce::isPositiveAndBelow (band, kMaxBands) ? (int) bands[(size_t) band].size() : 0;
}

int ChainHost::findBand (const juce::String& uid) const
{
    for (int b = 0; b < kMaxBands; ++b)
        for (auto& s : bands[(size_t) b])
            if (s->uid == uid) return b;
    return -1;
}

juce::AudioPluginInstance* ChainHost::getInstance (const juce::String& uid) const
{
    auto* s = findSlot (uid);
    return s != nullptr && s->status == Status::ready ? s->instance.get() : nullptr;
}

juce::PluginDescription ChainHost::getDescription (const juce::String& uid) const
{
    auto* s = findSlot (uid);
    return s != nullptr ? s->desc : juce::PluginDescription();
}

int ChainHost::getTotalLatency() const noexcept
{
    int l = 0;
    for (auto& b : bandLatency) l = juce::jmax (l, b.load (std::memory_order_relaxed));
    return l;
}

//==============================================================================
void ChainHost::prepare (double sr, int maxBlockSize, int channels)
{
    {
        const juce::ScopedLock al (audioLock);
        const juce::ScopedLock sl (structureLock);
        sampleRate = sr;
        blockSize = juce::jmax (1, maxBlockSize);
        numChannels = juce::jlimit (1, 2, channels);
        prepared = true;
        for (auto& band : bands)
            for (auto& s : band)
                if (s->instance != nullptr) prepareInstance (*s);
    }
    publish();
}

void ChainHost::release()
{
    const juce::ScopedLock al (audioLock);
    const juce::ScopedLock sl (structureLock);
    for (auto& band : bands)
        for (auto& s : band)
            if (s->instance != nullptr && s->instancePrepared)
            {
                s->instance->releaseResources();
                s->instancePrepared = false;
            }
    prepared = false;
}

void ChainHost::setNonRealtime (bool b) noexcept
{
    nonRealtime = b;
    const juce::ScopedLock sl (structureLock);
    for (auto& band : bands)
        for (auto& s : band)
            if (s->instance != nullptr) s->instance->setNonRealtime (b);
}

void ChainHost::prepareInstance (Slot& s)
{
    auto& inst = *s.instance;
    if (s.instancePrepared) inst.releaseResources();

    // Main bus = our channel layout; side-chains and extra buses off.
    const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    auto layout = inst.getBusesLayout();
    for (int i = 0; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();
    for (int i = 0; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();
    if (! inst.setBusesLayout (layout))
    {
        auto mainOnly = inst.getBusesLayout();
        if (! mainOnly.inputBuses.isEmpty())  mainOnly.inputBuses.getReference (0) = set;
        if (! mainOnly.outputBuses.isEmpty()) mainOnly.outputBuses.getReference (0) = set;
        if (! inst.setBusesLayout (mainOnly) && numChannels == 2)
        {
            // Mono-only plug-in: run it on the mono sum.
            auto mono = inst.getBusesLayout();
            if (! mono.inputBuses.isEmpty())  mono.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            if (! mono.outputBuses.isEmpty()) mono.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
            inst.setBusesLayout (mono);
        }
    }

    inst.setRateAndBufferSizeDetails (sampleRate, blockSize);
    inst.prepareToPlay (sampleRate, blockSize);
    inst.setNonRealtime (nonRealtime.load());
    s.instancePrepared = true;

    s.inChannels = inst.getMainBusNumInputChannels();
    s.outChannels = inst.getMainBusNumOutputChannels();
    const int scratchChannels = juce::jmax (1, numChannels, inst.getTotalNumInputChannels(), inst.getTotalNumOutputChannels());
    s.scratch.setSize (scratchChannels, blockSize, false, true, false);
    s.latency = inst.getLatencySamples();
}

//==============================================================================
void ChainHost::applySavedState (Slot& s, const juce::ValueTree& slotTree)
{
    const auto encoded = slotTree[stateProp].toString();
    if (s.instance == nullptr || encoded.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (encoded) && mb.getSize() > 0)
        s.instance->setStateInformation (mb.getData(), (int) mb.getSize());
}

void ChainHost::flushSlot (Slot& s)
{
    if (s.instance == nullptr || s.status != Status::ready) return;
    auto tree = findSlotTree (s.uid);
    if (! tree.isValid()) return;
    juce::MemoryBlock mb;
    s.instance->getStateInformation (mb);
    s.stateChanged = false;
    const juce::ScopedValueSetter<bool> svs (ignoreTreeChanges, true);
    tree.setProperty (stateProp, mb.toBase64Encoding(), nullptr);
}

void ChainHost::flushStates (bool onlyChanged)
{
    JUCE_ASSERT_MESSAGE_THREAD
    for (auto& band : bands)
        for (auto& s : band)
            if (! onlyChanged || s->stateChanged.load())
                flushSlot (*s);
}

//==============================================================================
void ChainHost::scheduleReconcile (bool full)
{
    if (full) fullReloadPending = true;
    reconcilePending = true;
    triggerAsyncUpdate();
}

void ChainHost::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& prop)
{
    if (! ignoreTreeChanges && t.hasType (slotType) && prop == bypassProp)
        scheduleReconcile (false);
}

void ChainHost::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (! ignoreTreeChanges && (child.hasType (chainsType) || isInChains (parent)))
        scheduleReconcile (false);
}

void ChainHost::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (! ignoreTreeChanges && (child.hasType (chainsType) || isInChains (parent)))
        scheduleReconcile (false);
}

void ChainHost::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (! ignoreTreeChanges && isInChains (parent))
        scheduleReconcile (false);
}

void ChainHost::valueTreeRedirected (juce::ValueTree&)
{
    // Preset, A/B or session: may arrive on a host thread, so only flag it.
    scheduleReconcile (true);
}

void ChainHost::handleAsyncUpdate()
{
    if (reconcilePending.exchange (false))
        reconcile (fullReloadPending.exchange (false));

    if (latencyDirty.exchange (false))
    {
        publish();
        notifyChanged();
    }
}

//==============================================================================
void ChainHost::reconcile (bool fullReload)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto chains = getChainsTree();

    std::map<juce::String, std::unique_ptr<Slot>> pool;
    {
        const juce::ScopedLock sl (structureLock);
        for (auto& band : bands)
        {
            for (auto& s : band)
            {
                auto uid = s->uid;
                pool[uid] = std::move (s);
            }
            band.clear();
        }
    }

    std::array<std::vector<std::unique_ptr<Slot>>, kMaxBands> next;
    std::vector<std::pair<Slot*, juce::ValueTree>> toLoad;

    int b = 0;
    for (auto bandTree : chains)
    {
        if (! bandTree.hasType (bandType)) continue;
        if (b >= kMaxBands) break;

        for (auto slotTree : bandTree)
        {
            if (! slotTree.hasType (slotType) || (int) next[(size_t) b].size() >= kMaxSlots) continue;

            auto uid = slotTree[uidProp].toString();
            if (uid.isEmpty())
            {
                uid = juce::Uuid().toString();
                const juce::ScopedValueSetter<bool> svs (ignoreTreeChanges, true);
                slotTree.setProperty (uidProp, uid, nullptr);
            }

            juce::PluginDescription desc;
            if (auto xml = slotTree.getChildWithName (pluginType).createXml())
                desc.loadFromXml (*xml);

            std::unique_ptr<Slot> slot;
            auto it = pool.find (uid);
            if (it != pool.end() && it->second->desc.createIdentifierString() == desc.createIdentifierString())
            {
                slot = std::move (it->second);
                pool.erase (it);
                if (fullReload)
                {
                    if (slot->status == Status::ready) applySavedState (*slot, slotTree);
                    else if (slot->status == Status::failed) toLoad.emplace_back (slot.get(), slotTree);
                }
            }
            else
            {
                slot = std::make_unique<Slot> (*this);
                slot->uid = uid;
                slot->desc = desc;
                toLoad.emplace_back (slot.get(), slotTree);
            }

            slot->bypassed = (bool) slotTree[bypassProp];
            next[(size_t) b].push_back (std::move (slot));
        }
        ++b;
    }

    {
        const juce::ScopedLock sl (structureLock);
        bands = std::move (next);
    }

    // Leftovers are gone from the tree: close their editors, swap them out of the audio snapshot, then destroy.
    for (auto& [uid, s] : pool)
        listeners.call ([&uid] (Listener& l) { l.slotRemoved (uid); });
    publish();
    pool.clear();

    for (auto& [slot, tree] : toLoad)
        startLoad (*slot, tree);

    notifyChanged();
}

void ChainHost::startLoad (Slot& s, const juce::ValueTree& slotTree)
{
    s.status = Status::loading;
    s.error = {};
    const int generation = ++s.loadGeneration;
    const auto uid = s.uid;

    auto& formats = catalog->getFormatManager();
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == s.desc.pluginFormatName) format = f;

    if (format == nullptr || s.desc.fileOrIdentifier.isEmpty())
    {
        finishLoad (uid, generation, nullptr, s.desc.pluginFormatName.isEmpty() ? "Unknown plug-in" : s.desc.pluginFormatName + " is not supported");
        return;
    }

    if (! formats.doesPluginStillExist (s.desc))
    {
        finishLoad (uid, generation, nullptr, "Not installed");
        return;
    }

    juce::ignoreUnused (slotTree);

    if (! format->requiresUnblockedMessageThreadDuringCreation (s.desc))
    {
        // AUv2 / VST3: synchronous, so a restored session is complete before playback starts.
        juce::String error;
        auto instance = formats.createPluginInstance (s.desc, sampleRate, blockSize, error);
        finishLoad (uid, generation, std::move (instance), error);
        return;
    }

    // AUv3: must be created asynchronously.
    formats.createPluginInstanceAsync (s.desc, sampleRate, blockSize,
        [weak = juce::WeakReference<ChainHost> (this), uid, generation] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (auto* self = weak.get())
                self->finishLoad (uid, generation, std::move (instance), error);
        });
}

void ChainHost::finishLoad (const juce::String& uid, int generation, std::unique_ptr<juce::AudioPluginInstance> instance,
                            const juce::String& error)
{
    auto* s = findSlot (uid);
    if (s == nullptr || s->loadGeneration != generation)
        return;   // removed or reloaded meanwhile (the instance is simply destroyed)

    if (instance == nullptr)
    {
        s->status = Status::failed;
        s->error = error.isNotEmpty() ? error : juce::String ("Couldn't load");
        notifyChanged();
        return;
    }

    {
        const juce::ScopedLock sl (structureLock);
        s->instance = std::move (instance);
        s->instancePrepared = false;
        prepareInstance (*s);
    }
    applySavedState (*s, findSlotTree (uid));
    s->latency = s->instance->getLatencySamples();
    s->instance->addListener (s);
    s->status = Status::ready;

    publish();
    notifyChanged();
}

//==============================================================================
void ChainHost::publish()
{
    Live next;
    double tail = 0.0;
    {
        const juce::ScopedLock sl (structureLock);
        for (int b = 0; b < kMaxBands; ++b)
        {
            double bandTail = 0.0;
            for (auto& s : bands[(size_t) b])
            {
                if (s->status != Status::ready || s->instance == nullptr) continue;
                s->latency = s->instance->getLatencySamples();
                if (s->bypassed || next.count[(size_t) b] >= kMaxSlots) continue;
                next.slots[(size_t) b][(size_t) next.count[(size_t) b]++] = s.get();
                next.latency[(size_t) b] += juce::jmax (0, s->latency);
                bandTail += juce::jmax (0.0, s->instance->getTailLengthSeconds());
            }
            tail = juce::jmax (tail, bandTail);
        }
    }

    {
        const juce::ScopedLock al (audioLock);
        live = next;
        for (int b = 0; b < kMaxBands; ++b)
            bandLatency[(size_t) b].store (next.latency[(size_t) b], std::memory_order_relaxed);
    }
    tailSeconds = tail;

    const int total = getTotalLatency();
    if (total != lastTotalLatency)
    {
        lastTotalLatency = total;
        if (onLatencyChanged) onLatencyChanged();
    }
}

void ChainHost::notifyChanged()
{
    listeners.call ([] (Listener& l) { l.chainsChanged(); });
}

//==============================================================================
ChainHost::AudioScope::AudioScope (ChainHost& h, juce::AudioPlayHead* ph) noexcept
    : host (h), playHead (ph), locked (h.audioLock.tryEnter())
{
}

ChainHost::AudioScope::~AudioScope()
{
    if (locked) host.audioLock.exit();
}

int ChainHost::AudioScope::getBandLatency (int band) const noexcept
{
    return locked ? host.live.latency[(size_t) band] : host.bandLatency[(size_t) band].load (std::memory_order_relaxed);
}

bool ChainHost::AudioScope::processBand (int band, juce::AudioBuffer<float>& buffer, int n) noexcept
{
    if (! locked) return false;
    const auto& snapshot = host.live;
    for (int i = 0; i < snapshot.count[(size_t) band]; ++i)
        processSlot (*snapshot.slots[(size_t) band][(size_t) i], buffer, n, host.midi, playHead);
    return true;
}

void ChainHost::processSlot (Slot& s, juce::AudioBuffer<float>& band, int n, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead) noexcept
{
    auto& inst = *s.instance;
    const int bandCh = band.getNumChannels();
    const int scratchCh = s.scratch.getNumChannels();
    n = juce::jmin (n, s.scratch.getNumSamples());

    if (s.inChannels == 1 && bandCh > 1)
    {
        s.scratch.copyFrom (0, 0, band.getReadPointer (0), n, 0.5f);
        s.scratch.addFrom (0, 0, band.getReadPointer (1), n, 0.5f);
        for (int c = 1; c < scratchCh; ++c) s.scratch.clear (c, 0, n);
    }
    else
    {
        for (int c = 0; c < scratchCh; ++c)
        {
            if (c < bandCh) s.scratch.copyFrom (c, 0, band, c, 0, n);
            else            s.scratch.clear (c, 0, n);
        }
    }

    juce::AudioBuffer<float> view (s.scratch.getArrayOfWritePointers(), scratchCh, n);
    midi.clear();
    inst.setPlayHead (playHead);
    inst.processBlock (view, midi);

    if (s.outChannels == 1 && bandCh > 1)
    {
        for (int c = 0; c < bandCh; ++c) band.copyFrom (c, 0, view, 0, 0, n);
    }
    else
    {
        for (int c = 0; c < juce::jmin (bandCh, juce::jmax (1, s.outChannels), scratchCh); ++c)
            band.copyFrom (c, 0, view, c, 0, n);
    }
}

} // namespace stsplit
