#pragma once
#include "../dsp/Engine.h"
#include "PluginCatalog.h"

namespace stsplit
{

/** Hosts the user's plug-ins: one serial chain of up to kMaxSlots instances per band.

    Source of truth = apvts.state:
        CHAINS
          BAND (one per band, in order)
            SLOT uid="…" bypass="0" state="<base64 plug-in state>"
              PLUGIN …            (juce::PluginDescription XML)
    Every edit (add / remove / move / replace / bypass) is an undoable change of that tree, and a reconciler turns
    the tree into live instances: so undo/redo, presets, A/B and session restore all work the same way. Instances
    whose SLOT uid survives a change are kept (moving a plug-in between bands keeps it running); a whole-state
    replacement (preset, A/B, session) also re-applies each plug-in's saved state. Plug-in states are written back
    into the tree when the host saves, before a slot is removed, and about once a second for plug-ins that reported
    changes.

    Audio: the audio thread runs a published snapshot of the chains under a try-lock (never blocks; if an edit holds
    the lock for its pointer swap the engine substitutes the aligned dry band for that block). Removed instances are
    destroyed only after the swap, so the audio thread never touches a dead plug-in. Band latency = sum of the
    active plug-ins' latencies; the engine aligns bands and the processor reports the maximum to the host.

    Threads: editing / queries / listeners on the message thread; prepare / release from the host (not concurrently
    with processing); AudioScope on the audio thread. */
class ChainHost : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    ChainHost (juce::AudioProcessor& owner, juce::AudioProcessorValueTreeState&, juce::UndoManager&);
    ~ChainHost() override;

    static inline const juce::Identifier chainsType { "CHAINS" }, bandType { "BAND" }, slotType { "SLOT" },
                                         pluginType { "PLUGIN" }, uidProp { "uid" }, bypassProp { "bypass" },
                                         stateProp { "state" };

    PluginCatalog& getCatalog() noexcept { return *catalog; }

    //==============================================================================
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void release();
    void setNonRealtime (bool) noexcept;

    //==============================================================================
    // Editing: message thread, undoable.
    bool canAdd (int band) const;
    /** Inserts a plug-in at `index` (clamped) of `band`. Returns the new slot's uid (empty if the band is full). */
    juce::String addPlugin (int band, int index, const juce::PluginDescription&);
    void removeSlot (const juce::String& uid);
    /** Moves a slot to `toIndex` in `toBand` (index in the target list before the move, like a drop position). */
    void moveSlot (const juce::String& uid, int toBand, int toIndex);
    void replacePlugin (const juce::String& uid, const juce::PluginDescription&);
    void setSlotBypassed (const juce::String& uid, bool);

    //==============================================================================
    enum class Status { loading, ready, failed };
    struct SlotInfo
    {
        juce::String uid, name, manufacturer, format, error;
        Status status = Status::loading;
        bool bypassed = false;
        int latencySamples = 0;
    };
    std::vector<SlotInfo> getSlots (int band) const;
    int getNumSlots (int band) const;
    /** Band of a slot, or -1. */
    int findBand (const juce::String& uid) const;
    /** The live instance of a slot (null while loading or if it failed). Message thread. */
    juce::AudioPluginInstance* getInstance (const juce::String& uid) const;
    juce::PluginDescription getDescription (const juce::String& uid) const;

    int getBandLatency (int band) const noexcept { return bandLatency[(size_t) band].load (std::memory_order_relaxed); }
    int getTotalLatency() const noexcept;
    double getTailSeconds() const noexcept { return tailSeconds.load (std::memory_order_relaxed); }

    /** Writes the plug-ins' current states into the tree (message thread). */
    void flushStates (bool onlyChanged = false);
    /** Applies pending tree changes now instead of on the next message-loop tick. */
    void handlePendingChanges() { handleUpdateNowIfNeeded(); }

    //==============================================================================
    struct Listener
    {
        virtual ~Listener() = default;
        /** Slots, their status or latency changed. */
        virtual void chainsChanged() {}
        /** Called just before a slot's instance is destroyed: drop its editor now. */
        virtual void slotRemoved (const juce::String& uid) { juce::ignoreUnused (uid); }
    };
    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Called (message thread, or the thread calling prepare) when the total latency changes. */
    std::function<void()> onLatencyChanged;

    //==============================================================================
    /** Audio-thread access for one block: try-locks the chain snapshot for its lifetime. */
    class AudioScope : public BandProcessor
    {
    public:
        AudioScope (ChainHost&, juce::AudioPlayHead*) noexcept;
        ~AudioScope() override;
        int getBandLatency (int band) const noexcept override;
        bool processBand (int band, juce::AudioBuffer<float>&, int numSamples) noexcept override;

    private:
        ChainHost& host;
        juce::AudioPlayHead* playHead;
        const bool locked;
        JUCE_DECLARE_NON_COPYABLE (AudioScope)
    };

private:
    struct Slot;

    juce::AudioProcessor& owner;
    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager& undo;
    juce::SharedResourcePointer<PluginCatalog> catalog;
    juce::ListenerList<Listener> listeners;

    // Message-thread structure (structureLock guards it against prepare / release on host threads).
    std::array<std::vector<std::unique_ptr<Slot>>, kMaxBands> bands;
    juce::CriticalSection structureLock;

    // Audio snapshot (audioLock).
    struct Live
    {
        std::array<std::array<Slot*, kMaxSlots>, kMaxBands> slots {};
        std::array<int, kMaxBands> count {}, latency {};
    };
    Live live;
    juce::CriticalSection audioLock;
    juce::MidiBuffer midi;

    std::array<std::atomic<int>, kMaxBands> bandLatency {};
    std::atomic<double> tailSeconds { 0.0 };
    int lastTotalLatency = 0;

    double sampleRate = 44100.0;
    int blockSize = 512, numChannels = 2;
    bool prepared = false;
    std::atomic<bool> nonRealtime { false };

    std::atomic<bool> reconcilePending { false }, fullReloadPending { false }, latencyDirty { false };
    bool ignoreTreeChanges = false;

    // Tree
    juce::ValueTree getChainsTree();
    juce::ValueTree getBandTree (int band);
    juce::ValueTree findSlotTree (const juce::String& uid);
    static bool isInChains (const juce::ValueTree&);

    // Reconcile / load
    void reconcile (bool fullReload);
    void startLoad (Slot&, const juce::ValueTree& slotTree);
    void finishLoad (const juce::String& uid, int generation, std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error);
    void prepareInstance (Slot&);
    void applySavedState (Slot&, const juce::ValueTree& slotTree);
    void flushSlot (Slot&);
    Slot* findSlot (const juce::String& uid) const;
    void publish();
    void notifyChanged();
    static void processSlot (Slot&, juce::AudioBuffer<float>&, int numSamples, juce::MidiBuffer&, juce::AudioPlayHead*) noexcept;

    // ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void scheduleReconcile (bool full);

    void handleAsyncUpdate() override;
    void timerCallback() override { flushStates (true); }

    JUCE_DECLARE_WEAK_REFERENCEABLE (ChainHost)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainHost)
};

} // namespace stsplit
