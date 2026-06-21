# Architecture

This page describes OQL's internal design for developers who want to contribute or extend the application.

---

## Layered structure

```
┌──────────────────────────────────────────────┐
│  src/ui/          Qt Widgets UI layer         │
│  (MainWindow, InspectorPanel, CueListView…)   │
├──────────────────────────────────────────────┤
│  src/engine/      Core logic (UI-free)        │
│  (Cue, CueList, AudioCue, Workspace…)         │
└──────────────────────────────────────────────┘
```

**Hard rule:** engine code must never include any Qt Widgets headers. The engine communicates upward via Qt signals only.

---

## Engine layer (`src/engine/`)

### Cue hierarchy

```
Cue  (QObject, abstract)
├── AudioCue          — audio playback via miniaudio decoder
├── VideoCue          — video playback via QMediaPlayer
├── TextCue           — text display
├── MicCue            — live microphone passthrough
├── LabelCue          — visual marker, no playback
├── GroupCue          — collapsible container
└── ControlCue        — targets another cue
    ├── StopCue
    ├── PauseCue
    ├── PlayCue
    ├── FadeCue
    ├── SpeedCue
    ├── EffectCue     — applies a plugin chain to a target AudioCue
    └── ResetEffectCue
```

Every cue exposes:
- `go()` — fire/start the cue
- `stop()` — stop and reset to Idle
- `pause()` / `resume()` where applicable
- `state()` — `Idle | Waiting | Playing | Paused`
- `propertyChanged` signal — emitted on any parameter mutation
- `stateChanged(State)` signal — emitted on state transitions
- `finished()` signal — emitted when the cue completes naturally

### CueList

`CueList` owns the cue objects as `std::unique_ptr<Cue>` and provides:
- Ordered cue access
- The **playhead** (index of the next-to-fire cue)
- Auto-continue / auto-follow chaining logic
- JSON serialisation (`toJson` / `fromJson`)

### Workspace

`Workspace` wraps `CueList` and handles:
- File I/O (`.oqlab` JSON format)
- The undo/redo stack (snapshots of the entire `CueList` JSON)
- Modified flag and recent-files list

### AudioEngine

`AudioEngine` is a singleton that owns the miniaudio device. It holds a list of `AudioRenderer*` objects and mixes them in the audio callback.

```
AudioEngine (singleton)
├── miniaudio device (48 kHz, 512 frames/block, f32 stereo)
├── m_renderers: vector<AudioRenderer*>   (guarded by m_mutex)
└── maDeviceCallback() — called from the audio thread ~93×/s
```

`AudioCue` implements `AudioRenderer`. When `go()` is called, it registers with `AudioEngine`; when playback ends or `stop()` is called, it deregisters.

### Audio plugin system

```
AudioPlugin  (abstract)
├── VstPlugin   — VST2 via dlopen + raw AEffect struct
└── Lv2Plugin   — LV2 via lilv

PluginChain
└── vector<unique_ptr<AudioPlugin>>
```

`PluginChain::process()` ping-pongs audio between two internal stereo buffers to avoid aliasing between plugins.

**Thread safety:** `AudioCue` protects its `PluginChain` with `m_chainMtx` (a `std::mutex`). The audio thread holds this lock during `process()`; the main thread holds it during `applyPluginChain()`, `restorePluginSnapshot()`, and `go()`. Additionally, `VstPlugin` has its own `m_effectMtx` to serialise `setParameter` (main thread) against `processReplacing` (audio thread) — `setParam` uses `try_lock` so it never blocks the UI.

---

## UI layer (`src/ui/`)

### MainWindow

Top-level window. Owns:
- `CueListView` (left)
- `InspectorPanel` (right)
- `ActiveCuesPanel` (bottom)
- `CueInfoBar` (status bar)
- `Workspace` instance

### CueListView / CueListModel

`CueListModel` is a `QAbstractTableModel` with a visible-row mapping that handles collapsed groups. `CueListView` is a `QTreeView`-like custom view with:
- Inline editing (F2 / double-click)
- Drag-and-drop reordering
- Multi-selection delete

### InspectorPanel

Displays and edits the selected cue's properties. Uses a `QStackedWidget` to switch between cue-type-specific sections.

Each `PluginChainWidget` instance is bound to exactly one `PluginChain*` — AudioCue and EffectCue each get their own widget and dialog to prevent cross-contamination.

Key invariant: `setChain(chain)` is a no-op if `chain == m_chain` (same pointer). This prevents the parameter slider area from being rebuilt on every `chainModified` signal, which would reset the selection mid-drag.

### ActiveCuesPanel

Polls `CueList` for active cues and renders a card per cue with a progress bar. For Effect cues with `duration=0`, the card stays visible until the cue is manually stopped.

---

## Data flow: firing an Audio cue

```
User presses Space
  → MainWindow::onGo()
  → CueList::go(index)
  → AudioCue::go()
      lock(m_chainMtx)
      m_chain.prepare(sr, block)
      unlock
      AudioEngine::addRenderer(this)

Audio thread (every 512 frames, ~10 ms):
  AudioEngine::maDeviceCallback()
    → AudioCue::renderAudio()
        decode frames from file
        apply volume envelope
        lock(m_chainMtx)
        m_chain.process(ch, n)      // VST2/LV2 chain
            VstPlugin::process()
                lock(m_effectMtx)
                m_effect->processReplacing()
                unlock(m_effectMtx)
        unlock(m_chainMtx)
        mix into output buffer

Playback ends:
  AudioCue::onRenderFinished()  (called from audio thread, safe)
  → QMetaObject::invokeMethod(…, Qt::QueuedConnection)
  → AudioCue::handleStreamFinished()  (main thread)
  → AudioEngine::removeRenderer(this)
  → setState(Idle) → emit finished()
  → CueList checks autoFollow
```

---

## Adding a new cue type

1. **Engine:** add a class in `src/engine/` inheriting `Cue`. Implement `go()`, `stop()`, `type()`, `typeName()`, `toJson()`, `fromJson()`.
2. **Factory:** add a `case` in `CueList::cueFromJson()` (or wherever cues are constructed from JSON).
3. **UI:** add a section in `InspectorPanel::buildUi()` and `updateMediaSection()` for the new type.
4. **Menu:** wire an action in `MainWindow` to create the new cue type.
5. **ActiveCuesPanel:** the panel renders all `State::Playing` cues generically; no changes needed unless the cue has a custom progress metric.

---

## File layout

```
src/
├── engine/
│   ├── Cue.h / Cue.cpp               Base class
│   ├── AudioCue.h / .cpp             Audio playback + miniaudio decoder
│   ├── VideoCue.h / .cpp
│   ├── TextCue.h / .cpp
│   ├── MicCue.h / .cpp
│   ├── ControlCues.h / .cpp          All control cue types
│   ├── GroupCue.h / .cpp
│   ├── LabelCue.h / .cpp
│   ├── CueList.h / .cpp
│   ├── Workspace.h / .cpp
│   ├── AppSettings.h / .cpp
│   ├── AudioEngine.h / .cpp          miniaudio device + mixer
│   ├── AudioPlugin.h                 Abstract plugin interface
│   ├── PluginChain.h / .cpp          Ordered plugin chain
│   ├── VstPlugin.h / .cpp            VST2 implementation
│   ├── Lv2Plugin.h / .cpp            LV2 implementation
│   └── miniaudio.h                   Single-header audio library
└── ui/
    ├── MainWindow.h / .cpp
    ├── CueListView.h / .cpp
    ├── CueListModel.h / .cpp
    ├── InspectorPanel.h / .cpp
    ├── WaveformView.h / .cpp
    ├── ActiveCuesPanel.h / .cpp
    ├── CueInfoBar.h / .cpp
    ├── PluginChainWidget.h / .cpp
    ├── VideoOutputWindow.h / .cpp
    ├── TextOutputWindow.h / .cpp
    └── SettingsDialog.h / .cpp
```
