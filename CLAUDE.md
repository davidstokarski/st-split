# ST Split — guide for agents

ST Split splits the input at a frequency and hosts the user's own AU / VST3 plug-ins on each band. It is built on
the shared module in `../st-shell` (https://github.com/davidstokarski/st-shell). Before changing anything, read the
shell docs:

- `../st-shell/CLAUDE.md` — non-negotiables (design, real-time safety, native menus, verification)
- `../st-shell/docs/DESIGN.md`, `../st-shell/docs/ARCHITECTURE.md`, `../st-shell/docs/EXAMPLES.md`
- `../st-sat` is the reference plugin for display / panel / footer patterns.

Plugin-specific notes:

- Parameters: `Source/Params.*` (IDs are permanent — never rename them). `kMaxBands` = 2 today; IDs follow ST Sat
  (`xover_<n>`, `b<n>_<suffix>`) so more split points can be added without renaming. `kMaxSlots` = plug-ins per band.
- DSP: `Source/dsp/Engine.*` — input gain → `st::dsp::Crossover` → per band: chain (`BandProcessor`), dry delayed by
  the chain latency, mix / level / mute-solo → per-band alignment delay to the slowest chain → sum → output gain.
  Offline-testable; tests use a fake `BandProcessor`.
- Hosting: `Source/host/ChainHost.*`. The chain lives in `apvts.state` (`CHAINS/BAND/SLOT` + `PLUGIN` description +
  base64 `state`); every edit is an undoable tree change and `reconcile()` turns the tree into live instances
  (kept by slot uid). Audio thread: `ChainHost::AudioScope` try-locks the published snapshot for one block; edits
  swap the snapshot under that lock and destroy removed instances only afterwards. Never call into a hosted
  instance from the audio thread outside `AudioScope`, and never block the audio thread on the lock.
- Plug-in list: `Source/host/PluginCatalog.*` (process-wide `SharedResourcePointer`): AUs from the component
  registry (`AudioUnitList.cpp`, no instantiation), VST3 background scan with a dead man's pedal. ST Split never
  lists itself.
- Look: Logic Pro's dark appearance. Theme tokens are set in the processor constructor; `Source/ui/Style.*` turns them
  into surfaces (toolbar strips, panels, wells, flat buttons, M / S toggles, meters). Use those instead of the shell's
  warm hardware helpers (`paint::greyButton`, `pillHandle`, `tray`), and keep new UI flat, neutral and hairline-edged.
- UI: `Source/ui/SplitDisplay.*` (display), `Source/ui/BandRack.*` (rack + slot rows, drag & drop between bands),
  `Source/ui/PluginWindow.*` (hosted editors; the editor closes them on `slotRemoved` before instances die),
  `Source/ui/PluginMenu.*` (native plug-in menu), `Source/ui/Footer.*`, `Source/PluginEditor.*`.
- Verify: `StSplitPluginTests` (engine null tests, Apple AU + ST Sat VST3 hosting, undo, state, A/B, block sizes),
  `StSplitPluginTests --snapshot <dir>` (look at the PNGs), `auval -v aufx Stsp StAu`.
- Anything reusable belongs in st-shell, not here.
