// Whole-plugin tests (engine alignment, hosting Apple's built-in Audio Units, state, stress) and editor snapshots.
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace stsplit;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

void setParam (StSplitProcessor& p, const juce::String& id, float plain)
{
    auto& prm = p.param (id);
    prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
}

void fillNoise (juce::AudioBuffer<float>& buf, juce::Random& rng, int offset)
{
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.25f + 0.3f * std::sin ((float) (offset + i) * 0.031f * (float) (c + 1)));
}

float runNoise (StSplitProcessor& p, int blocks, int blockSize, bool& finite)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::Random rng (1);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        fillNoise (buf, rng, b * blockSize);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
            }
    }
    return peak;
}

/** A chain that delays one band by a fixed number of samples (reporting it as latency), or fails to run. */
struct FakeChains : BandProcessor
{
    int delayedBand = 0, delay = 0;
    bool fail = false;
    std::vector<std::vector<float>> history;   // per channel ring

    void prepare (int maxDelay) { history.assign (2, std::vector<float> ((size_t) maxDelay + 1, 0.0f)); pos = 0; }
    int getBandLatency (int band) const noexcept override { return band == delayedBand ? delay : 0; }
    bool processBand (int band, juce::AudioBuffer<float>& buf, int n) noexcept override
    {
        if (fail) return false;
        if (band != delayedBand || delay == 0) return true;
        const int size = (int) history[0].size();
        int startPos = pos;
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            int w = startPos;
            auto* x = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                history[(size_t) c][(size_t) w] = x[i];
                x[i] = history[(size_t) c][(size_t) ((w - delay + size) % size)];
                w = (w + 1) % size;
            }
        }
        pos = (startPos + n) % size;
        return true;
    }
    int pos = 0;
};

std::vector<float> renderEngine (Engine& e, BandProcessor* chains, int total, int block, int seed = 3)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::Random rng (seed);
    std::vector<float> out;
    for (int start = 0; start < total; start += block)
    {
        fillNoise (buf, rng, start);
        e.process (buf, block, chains);
        for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

void engineTests()
{
    std::cout << "Engine\n";

    // ---- The split sums back flat (Linkwitz-Riley allpass) at every slope.
    for (int slope = 0; slope < 3; ++slope)
    {
        Engine e;
        e.prepare (48000.0, 512, 1, 4800);
        EngineSettings s;
        s.slope = (st::dsp::Crossover::Slope) slope;
        s.crossovers[0] = 400.0f;
        e.setSettings (s);
        e.reset();

        constexpr int order = 14, size = 1 << order;
        juce::AudioBuffer<float> buf (1, 512);
        std::vector<float> ir;
        for (int start = 0; start < size; start += 512)
        {
            buf.clear();
            if (start == 0) buf.setSample (0, 0, 1.0f);
            e.process (buf, 512, nullptr);
            for (int i = 0; i < 512; ++i) ir.push_back (buf.getSample (0, i));
        }
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);
        std::copy (ir.begin(), ir.end(), data.begin());
        fft.performFrequencyOnlyForwardTransform (data.data());
        float worst = 0.0f;
        for (int k = 1; k < size / 2; ++k)
            worst = juce::jmax (worst, std::abs (juce::Decibels::gainToDecibels (data[(size_t) k], -200.0f)));
        check (worst < 0.05f, "slope " + juce::String (slope) + ": bands sum flat (max dev " + juce::String (worst, 4) + " dB)");
    }

    // ---- A chain with latency on one band: every band (and the dry part of partial mixes) stays aligned,
    //      so the result equals the plain split delayed by the chain latency.
    for (int delayedBand = 0; delayedBand < kMaxBands; ++delayedBand)
        for (float bandMix : { 1.0f, 0.5f })
        {
            const int delay = 137, total = 48000, block = 256;
            EngineSettings s;
            s.crossovers[0] = 700.0f;
            s.mix = 0.8f;
            for (auto& b : s.bands) b.mix = bandMix;

            Engine ref, test;
            for (auto* e : { &ref, &test }) { e->prepare (48000.0, block, 2, 4800); e->setSettings (s); e->reset(); }
            FakeChains chains;
            chains.prepare (4800);
            chains.delayedBand = delayedBand;
            chains.delay = delay;

            const auto a = renderEngine (ref, nullptr, total, block);
            const auto b = renderEngine (test, &chains, total, block);
            float err = 0.0f;
            for (int i = delay; i < total; ++i)
                err = juce::jmax (err, std::abs (b[(size_t) i] - a[(size_t) (i - delay)]));
            check (test.getLatencySamples() == delay && err < 1.0e-4f,
                   "band " + juce::String (delayedBand) + " latency " + juce::String (delay) + ", band mix " + juce::String (bandMix)
                   + ": aligned (max err " + juce::String (err, 7) + ")");
        }

    // ---- Chain can't run (edit in progress): output stays the aligned, unprocessed split.
    {
        const int delay = 64, total = 9600, block = 128;
        EngineSettings s;
        Engine ref, test;
        for (auto* e : { &ref, &test }) { e->prepare (48000.0, block, 2, 4800); e->setSettings (s); e->reset(); }
        FakeChains chains;
        chains.prepare (4800);
        chains.delay = delay;
        chains.fail = true;
        const auto a = renderEngine (ref, nullptr, total, block);
        const auto b = renderEngine (test, &chains, total, block);
        float err = 0.0f;
        for (int i = delay; i < total; ++i)
            err = juce::jmax (err, std::abs (b[(size_t) i] - a[(size_t) (i - delay)]));
        check (err < 1.0e-4f, "skipped chain falls back to the aligned dry band (max err " + juce::String (err, 7) + ")");
    }

    // ---- Mute both bands = silence, solo works through the processor too (below).
    {
        Engine e;
        e.prepare (48000.0, 256, 2, 4800);
        EngineSettings s;
        for (auto& b : s.bands) b.audible = false;
        e.setSettings (s);
        e.reset();
        const auto out = renderEngine (e, nullptr, 4096, 256);
        float peak = 0.0f;
        for (auto v : out) peak = juce::jmax (peak, std::abs (v));
        check (peak < 1.0e-6f, "both bands muted -> silence");
    }
}

std::optional<juce::PluginDescription> findAppleAU (const juce::String& subtype)
{
    for (auto& d : listAudioUnitEffects())
        if (d.fileOrIdentifier.endsWith ("," + subtype + ",appl")) return d;
    return std::nullopt;
}
} // namespace

//==============================================================================
static int runTests()
{
    juce::ScopedJuceInitialiser_GUI gui;
    engineTests();

    std::cout << "Catalogue\n";
    const auto aus = listAudioUnitEffects();
    const auto delay = findAppleAU ("dely"), dist = findAppleAU ("dist"), limiter = findAppleAU ("lmtr"), eq = findAppleAU ("nbeq");
    check (aus.size() > 5 && delay && dist && limiter && eq, "Audio Unit registry lists Apple's effects (" + juce::String ((int) aus.size()) + " AU effects)");
    bool selfListed = false;
    {
        juce::SharedResourcePointer<PluginCatalog> catalog;
        for (auto& d : catalog->getEffects()) selfListed |= PluginCatalog::isSelf (d);
    }
    check (! selfListed, "ST Split never offers itself");
    if (! (delay && dist && limiter && eq)) { std::cout << failures << " FAILURES\n"; return 1; }

    std::cout << "Hosting\n";
    auto proc = std::make_unique<StSplitProcessor>();
    auto& p = *proc;
    auto& chains = p.getChains();
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    const auto eqUid = chains.addPlugin (0, 0, *eq);
    const auto limUid = chains.addPlugin (0, 1, *limiter);
    const auto distUid = chains.addPlugin (1, 0, *dist);
    check (chains.getNumSlots (0) == 2 && chains.getNumSlots (1) == 1, "plug-ins added to both bands");
    check (chains.getInstance (eqUid) != nullptr && chains.getInstance (limUid) != nullptr && chains.getInstance (distUid) != nullptr,
           "Apple AUs instantiated");
    const int limLatency = chains.getInstance (limUid) != nullptr ? chains.getInstance (limUid)->getLatencySamples() : -1;
    check (chains.getBandLatency (0) == limLatency && p.getLatencySamples() == chains.getTotalLatency(),
           "band latency = sum of its plug-ins, reported to the host (" + juce::String (limLatency) + " samples)");

    bool finite = true;
    float peak = runNoise (p, 40, 512, finite);
    check (finite && peak > 0.01f && peak < 16.0f, "hosted chains process (peak " + juce::String (peak, 3) + ")");

    // Move: the same instance keeps running in the other band.
    auto* eqInstance = chains.getInstance (eqUid);
    chains.moveSlot (eqUid, 1, 0);
    check (chains.findBand (eqUid) == 1 && chains.getInstance (eqUid) == eqInstance && chains.getSlots (1)[0].uid == eqUid,
           "move to the other band keeps the instance");
    chains.moveSlot (distUid, 1, 0);
    check (chains.getSlots (1)[0].uid == distUid, "reorder within a band");

    chains.setSlotBypassed (limUid, true);
    check (chains.getSlots (0)[0].bypassed && chains.getBandLatency (0) == 0, "bypassed slot leaves the chain (and its latency)");
    chains.setSlotBypassed (limUid, false);

    // Remove + undo: the plug-in comes back with its settings.
    auto* distInst = chains.getInstance (distUid);
    auto* distParam = distInst != nullptr && ! distInst->getParameters().isEmpty() ? distInst->getParameters()[0] : nullptr;
    if (distParam != nullptr) distParam->setValue (0.37f);
    chains.removeSlot (distUid);
    check (chains.getNumSlots (1) == 1 && chains.getInstance (distUid) == nullptr, "remove");
    p.undoManager.undo();
    chains.handlePendingChanges();
    auto* restored = chains.getInstance (distUid);
    check (restored != nullptr && chains.findBand (distUid) == 1, "undo brings the plug-in back");
    if (restored != nullptr && distParam != nullptr)
        check (std::abs (restored->getParameters()[0]->getValue() - 0.37f) < 1.0e-3f, "... with its settings");
    p.undoManager.redo();
    chains.handlePendingChanges();
    check (chains.getInstance (distUid) == nullptr, "redo removes it again");
    p.undoManager.undo();
    chains.handlePendingChanges();

    // Replace keeps the position.
    chains.replacePlugin (limUid, *delay);
    const auto band0 = chains.getSlots (0);
    check (band0.size() == 1 && band0[0].name == delay->name, "replace keeps the slot position");
    const auto delayUid = band0.empty() ? juce::String() : band0[0].uid;

    // ---- State round trip (session save / restore)
    if (auto* d = chains.getInstance (delayUid))
        if (! d->getParameters().isEmpty()) d->getParameters()[0]->setValue (0.61f);
    setParam (p, ids::crossover (0), 1234.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);

    {
        StSplitProcessor p2;
        p2.setPlayConfigDetails (2, 2, 48000.0, 512);
        p2.prepareToPlay (48000.0, 512);
        p2.setStateInformation (state.getData(), (int) state.getSize());
        p2.getChains().handlePendingChanges();
        const auto s0 = p2.getChains().getSlots (0), s1 = p2.getChains().getSlots (1);
        check (s0.size() == 1 && s1.size() == 2 && s0[0].name == delay->name && s1[0].name == dist->name && s1[1].name == eq->name,
               "session restore rebuilds both chains in order");
        auto* d2 = s0.empty() ? nullptr : p2.getChains().getInstance (s0[0].uid);
        check (d2 != nullptr && ! d2->getParameters().isEmpty() && std::abs (d2->getParameters()[0]->getValue() - 0.61f) < 1.0e-3f,
               "... with each plug-in's settings");
        check (std::abs (p2.getCrossover (0) - 1234.0f) < 1.0f, "... and the split frequency");
        bool f2 = true;
        runNoise (p2, 10, 512, f2);
        check (f2, "restored session processes");
    }

    // ---- A/B: B starts as a copy; emptying it and switching back restores A's chain.
    p.ab.select (1);
    chains.handlePendingChanges();
    for (auto& s : chains.getSlots (1)) chains.removeSlot (s.uid);
    check (chains.getNumSlots (1) == 0, "B: high band emptied");
    p.ab.select (0);
    chains.handlePendingChanges();
    check (chains.getNumSlots (1) == 2, "A: high band chain back");

    // ---- Odd block sizes with hosted plug-ins
    for (int bs : { 1, 7, 63, 64, 65, 333, 2048 })
    {
        p.prepareToPlay (44100.0, bs);
        const float pk = runNoise (p, 8, bs, finite);
        check (finite && pk < 16.0f, "block size " + juce::String (bs));
    }

    // ---- Solo
    p.prepareToPlay (48000.0, 512);
    setParam (p, ids::band (0, "solo"), 1.0f);
    setParam (p, ids::band (0, "mute"), 1.0f);
    {
        bool f = true;
        runNoise (p, 4, 512, f);
        const float pk = runNoise (p, 4, 512, f);
        check (pk < 1.0e-4f, "solo low + mute low -> silence");
    }
    setParam (p, ids::band (0, "solo"), 0.0f);
    setParam (p, ids::band (0, "mute"), 0.0f);

    // ---- Editor: plug-in windows open and close with their slot.
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (StSplitEditor::designWidth, StSplitEditor::designHeight);
        auto* e = dynamic_cast<StSplitEditor*> (ed.get());
        const auto uid = chains.getSlots (1)[0].uid;
        e->openPluginWindow (uid);
        check (e->getNumOpenPluginWindows() == 1, "plug-in window opens");
        chains.removeSlot (uid);
        check (e->getNumOpenPluginWindows() == 0, "removing the slot closes its window first");
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        juce::Graphics g (img);
        ed->paintEntireComponent (g, true);
    }

    // ---- VST3 hosting: load the installed ST Sat VST3 (only that bundle is opened).
    {
        const auto bundle = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                .getChildFile ("Library/Audio/Plug-Ins/VST3/ST Sat.vst3");
        if (bundle.isDirectory())
        {
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* f : chains.getCatalog().getFormatManager().getFormats())
                if (f->getName() == "VST3") f->findAllTypesForFile (found, bundle.getFullPathName());
            check (found.size() == 1, "VST3 description read from ST Sat.vst3");
            if (! found.isEmpty())
            {
                const auto uid = chains.addPlugin (0, 0, *found[0]);
                auto* inst = chains.getInstance (uid);
                check (inst != nullptr && inst->getName() == "ST Sat", "ST Sat VST3 hosted in the low band");
                bool f = true;
                const float pk = runNoise (p, 20, 512, f);
                check (f && pk > 0.01f && pk < 16.0f, "VST3 chain processes (peak " + juce::String (pk, 3) + ")");
                if (inst != nullptr) inst->getParameters()[1]->setValue (0.8f);
                juce::MemoryBlock st;
                p.getStateInformation (st);
                StSplitProcessor p3;
                p3.setPlayConfigDetails (2, 2, 48000.0, 512);
                p3.prepareToPlay (48000.0, 512);
                p3.setStateInformation (st.getData(), (int) st.getSize());
                p3.getChains().handlePendingChanges();
                auto* inst3 = p3.getChains().getInstance (uid);
                check (inst3 != nullptr && std::abs (inst3->getParameters()[1]->getValue() - 0.8f) < 1.0e-3f,
                       "VST3 state restored in a new session");
                chains.removeSlot (uid);
            }
        }
        else
            std::cout << "  (skipped VST3 test: ~/Library/Audio/Plug-Ins/VST3/ST Sat.vst3 not installed)\n";
    }

    // ---- Preset load replaces the chain
    p.presets.loadPreset (0);
    chains.handlePendingChanges();
    check (chains.getNumSlots (0) == 0 && chains.getNumSlots (1) == 0, "Init preset clears the chains");

    // ---- CPU
    {
        chains.addPlugin (0, 0, *eq);
        chains.addPlugin (0, 1, *limiter);
        chains.addPlugin (1, 0, *dist);
        chains.addPlugin (1, 1, *eq);
        const int blocks = 400;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runNoise (p, blocks, 512, finite);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  CPU (4 Apple AUs): " << juce::String (100.0 * ms / (blocks * 512 / 48.0), 1) << "% of one core\n";
    }

    proc.reset();
    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}

//==============================================================================
namespace
{
/** Renders the editor after letting its timers run for a while (needs a running message loop). */
struct Snapshotter : juce::Timer
{
    struct Job { std::function<void()> setup; juce::String name; float scale = 1.0f; std::function<void (StSplitEditor&)> afterOpen; };
    StSplitProcessor& p;
    juce::File dir;
    std::vector<Job> jobs;
    size_t index = 0;
    int ticks = 0;
    bool opened = false;
    std::unique_ptr<juce::AudioProcessorEditor> ed;

    Snapshotter (StSplitProcessor& proc, juce::File d) : p (proc), dir (d) {}

    void timerCallback() override
    {
        if (ed == nullptr)
        {
            if (index >= jobs.size()) { stopTimer(); juce::JUCEApplicationBase::quit(); return; }
            if (jobs[index].setup) jobs[index].setup();
            p.getChains().handlePendingChanges();
            ed.reset (p.createEditor());
            ed->setSize (juce::roundToInt ((float) StSplitEditor::designWidth * jobs[index].scale),
                         juce::roundToInt ((float) StSplitEditor::designHeight * jobs[index].scale));
            ticks = 0;
            opened = false;
        }

        bool f = true;
        runNoise (p, 4, 256, f);

        if (! opened && ticks > 6 && jobs[index].afterOpen)
        {
            if (auto* e = dynamic_cast<StSplitEditor*> (ed.get())) jobs[index].afterOpen (*e);
            opened = true;
        }

        if (++ticks > 50)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto file = dir.getChildFile (jobs[index].name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
            std::cout << "wrote " << file.getFullPathName() << "\n";
            ed.reset();
            ++index;
        }
    }
};
}

static juce::File snapshotDir;

struct SnapshotApp : juce::JUCEApplicationBase
{
    std::unique_ptr<StSplitProcessor> proc;
    std::unique_ptr<Snapshotter> snap;

    const juce::String getApplicationName() override { return "StSplitSnapshots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override { snap.reset(); proc.reset(); }

    void initialise (const juce::String&) override
    {
        proc = std::make_unique<StSplitProcessor>();
        auto& p = *proc;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        snapshotDir.createDirectory();

        snap = std::make_unique<Snapshotter> (p, snapshotDir);
        snap->jobs.push_back ({ [&p] { p.presets.loadPreset (0); }, "01_empty" });
        snap->jobs.push_back ({ [&p]
        {
            p.presets.loadPreset (0);
            p.getChains().handlePendingChanges();
            auto& c = p.getChains();
            if (auto d = findAppleAU ("nbeq")) c.addPlugin (0, 0, *d);
            if (auto d = findAppleAU ("lmtr")) c.addPlugin (0, 1, *d);
            if (auto d = findAppleAU ("dist")) c.addPlugin (1, 0, *d);
            if (auto d = findAppleAU ("dely"))
            {
                const auto uid = c.addPlugin (1, 1, *d);
                c.setSlotBypassed (uid, true);
            }
            auto missing = *findAppleAU ("dely");
            missing.name = "Vintage Comp";
            missing.manufacturerName = "Nowhere Audio";
            missing.fileOrIdentifier = "AudioUnit:Effects/aufx,zzzz,Nowh";
            c.addPlugin (1, 2, missing);
            auto& x = p.param (ids::crossover (0));
            x.setValueNotifyingHost (x.convertTo0to1 (320.0f));
            p.uiState.setProperty ("selectedBand", 1, nullptr);
        }, "02_chains" });
        snap->jobs.push_back ({ [&p] { p.uiState.setProperty ("selectedBand", 0, nullptr); }, "03_small_scale", 0.75f });
        snap->jobs.push_back ({ [&p]
        {
            auto& s = p.bandParam (0, "solo");
            s.setValueNotifyingHost (1.0f);
            auto& l = p.bandParam (1, "level");
            l.setValueNotifyingHost (l.convertTo0to1 (-9.0f));
        }, "04_solo_low" });
        snap->startTimerHz (60);
    }
};

static juce::JUCEApplicationBase* createSnapshotApp() { return new SnapshotApp(); }

int main (int argc, char** argv)
{
    juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--snapshot"))
    {
        snapshotDir = juce::File (args[args.indexOf ("--snapshot") + 1]);
        juce::JUCEApplicationBase::createInstance = &createSnapshotApp;
        return juce::JUCEApplicationBase::main (argc, (const char**) argv);
    }
    return runTests();
}
