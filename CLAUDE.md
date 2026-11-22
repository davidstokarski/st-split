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
  registry (`AudioUnitList.cpp`, no instantiation); VST3s are opened only by the `ST Split Scanner` helper
  (`scanner/Main.cpp`, embedded in each bundle's `Contents/Helpers` by CMake before JUCE re-signs and installs),
  one process per bundle, so a plug-in that crashes while being listed can't take the DAW down. Never add an
  in-process fallback: that crashed Logic (a VST3 crashed in its initialisers on our scan thread). ST Split never
  lists itself.
- Look: the ST family's (ST Sat / ST EQ / ST DC): full-window display with the selected band lit in its colour,
  Sat's dashed split line + tent grip and pill handles, glass band bars with grey buttons on their top edge, matte
  knobs with a ringed hero (Level, band colour), the footer tray. ST Split's identity is a cool slate near-black
  with amber (low) and cyan (high) light pools: tokens in `Source/ui/Style.cpp::applyTheme`, backdrop / tray / glass
  bar in `paintBackdrop` / `paintTray` / `paintGlassBar`. Compare new UI against the siblings' screenshots.
- UX follows FabFilter Saturn / Pro-MB: everything is edited on the display. Band header with S / M that appear on
  hover (hold = momentary, Cmd-click = exclusive); the split's frequency pill (drag, double-click types, Cmd-click
  resets) with its slope button; level buttons (vertical = level, sideways = move the split, double-click types,
  Cmd-click resets); one band panel that glides under the selected band. Plug-ins are compact Logic-style insert
  slots inside each band's column (Option-click bypasses, Cmd-click removes, drag between bands).
- UI: `Source/ui/SplitDisplay.*` (display + gestures), `Source/ui/InsertStack.*` (insert slots per band, drag & drop),
  `Source/ui/BandPanel.*` (gliding band panel), `Source/ui/PluginWindow.*` (hosted editors; the editor closes them
  on `slotRemoved` before instances die), `Source/ui/PluginMenu.*` (native plug-in menu), `Source/ui/Footer.*`,
  `Source/PluginEditor.*` (places the stacks over their columns and glides the panel every frame).
- Verify: `StSplitPluginTests` (engine null tests, Apple AU + ST Sat VST3 hosting, undo, state, A/B, block sizes),
  `StSplitPluginTests --snapshot <dir>` (look at the PNGs), `auval -v aufx Stsp StAu`.
- Anything reusable belongs in st-shell, not here.
