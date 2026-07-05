# OQL

**OQL** is a free and open-source show-control application inspired by QLab, built with C++20 and Qt6. It is designed for live theatre, concerts, and events — letting you build and fire cue lists with precision timing, audio/video playback, and flexible automation.

> **This project is open to external contributions.** Whether you're a developer, sound designer, or live-events professional, your input is welcome.

---

## Features

- **Audio cues** — playback via QMediaPlayer; per-cue volume, loop count, fade in/out curves, trim start/end, channel routing (stereo / L / R), waveform editor with per-slice loop/rate control
- **Video cues** — fullscreen output on a dedicated window; loop support
- **Text cues** — fullscreen text overlay on a dedicated output window
- **Waveform view** — zoomable waveform with visual trim handles, fade envelope, and per-point volume automation; configurable detail level (1000–16000 buckets)
- **Recording cues** — capture microphone input to a WAV file; first GO starts recording, second GO stops and links the result to a new Audio cue automatically
- **Microphone cues** — live audio passthrough from any input device to output; VU meter, input level and RecordCue source selector
- **Fade cues** — smooth volume interpolation on any audio/video cue; optional auto-stop at end of fade
- **Built-in audio effects** — 10 cross-platform effects with no external dependencies: Gain, Delay, Reverb (Freeverb), EQ 3-band, Compressor, Limiter, Chorus, Tremolo, Phaser, Stereo Widener; configurable per-cue in a floating chain editor
- **Plugin effects (VST2 / LV2)** — third-party plugin support; VST2 native GUI editor on Linux/Windows; custom search paths configurable in Settings
- **Stop / Pause / Play / Speed / Effect / Reset Effect cues** — full transport and DSP control over any running cue
- **Script cues** — JavaScript automation via QJSEngine; inline editor with syntax highlighting
- **Group cues** — collapsible organisational containers; collapse to a single row
- **Label cues** — visual-only markers for annotating your cue list
- **Pre-wait / post-wait / auto-continue / auto-follow** — flexible timing chains
- **Show Mode** — dedicated performance mode that hides editing UI; configurable keyboard shortcut
- **Web remote** — built-in HTTP server with a touch-friendly web interface (GO, STOP, HOME, active cue display)
- **Multi-language UI** — Italian, English, Spanish, French; switching is live (no restart needed)
- **Configurable shortcuts** — all add-cue shortcuts (Ctrl+1–5 etc.) and transport shortcuts are user-assignable in Settings
- **Undo / redo** — full snapshot-based undo stack
- **Recent files** — quick access to recent workspaces
- **Project settings** — show-level defaults for fades, cue numbering, row height, font, and column layout

---

## Screenshots

*Coming soon.*

---

## Building

### Requirements

- CMake ≥ 3.20
- Qt 6.4+ (Core, Widgets, Multimedia, MultimediaWidgets, Svg, Network, Qml, LinguistTools)
- A C++20-capable compiler (GCC 11+, Clang 14+, MSVC 2022+)
- lilv (LV2 plugin host)
- suil (LV2 UI loader, Linux only)

### Linux

```bash
git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/oql
```

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.logicalcpu)
open build/oql.app
```

### Windows

Install Qt6 via the [Qt online installer](https://www.qt.io/download-open-source), then:

```bash
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

---

## Project Structure

```
src/
├── engine/          # Core logic — no UI dependencies
│   ├── Cue.h/cpp          Base cue class
│   ├── AudioCue           QMediaPlayer-based audio playback + slice system
│   ├── VideoCue           Video playback with QVideoWidget
│   ├── TextCue            Text output cue
│   ├── ControlCues        Stop, Fade, Pause, Speed, Play, Effect, ResetEffect
│   ├── MicCue             Live microphone passthrough with VU meter
│   ├── RecordCue          Microphone → WAV recording → AudioCue link
│   ├── ScriptCue          QJSEngine JavaScript automation
│   ├── GroupCue           Collapsible cue container
│   ├── LabelCue           Visual-only label
│   ├── CueList            Flat cue list with playhead and autofollow
│   ├── Workspace          File I/O (.oql JSON format)
│   └── AppSettings        Per-user settings via QSettings
└── ui/              # Qt Widgets UI
    ├── MainWindow         Main application window + toolbar + Show Mode
    ├── CueListView        Custom table view with drag-and-drop
    ├── CueListModel       QAbstractTableModel with visible-row mapping
    ├── InspectorPanel     Per-cue property editor
    ├── WaveformView       Waveform rendering and editing (configurable resolution)
    ├── ActiveCuesPanel    Live view of currently playing cues
    ├── CueInfoBar         Bottom status bar
    ├── VideoOutputWindow  Dedicated fullscreen video output
    ├── TextOutputWindow   Dedicated fullscreen text output
    ├── WebServer          Built-in HTTP server for web remote
    ├── ScriptEditorDialog JavaScript editor for Script cues
    └── SettingsDialog     Project and app settings (shortcuts, language, waveform)
translations/
    oql_en.ts / oql_es.ts / oql_fr.ts   Qt Linguist translation files
resources/
    webui.html             Touch-friendly web remote interface
```

---

## Workspace Format

Workspaces are saved as `.oql` files (JSON). The format is intentionally simple and human-readable — you can inspect and edit them with any text editor.

---

## Roadmap

- [ ] MIDI / OSC trigger support
- [ ] Multi-output audio routing
- [ ] Network sync between multiple instances
- [x] macOS / Windows packaging (beta builds available)
- [x] Built-in cross-platform audio effects (Gain, Delay, Reverb, EQ, Compressor, Limiter, Chorus, Tremolo, Phaser, Stereo Widener)
- [ ] Video effects (planned — requires QVideoSink frame-interception refactor)
- [ ] Dark theme polish and HiDPI improvements
- [ ] Inspector combo for assigning cues to groups
- [ ] Inline editing in the cue list

---

## Contributing

**OQL is actively looking for contributors.** All skill levels and backgrounds are welcome.

### Ways to contribute

- **Bug reports** — open an [issue](https://github.com/teonactl/oql/issues) with steps to reproduce
- **Feature requests** — describe the use case in an issue before writing code
- **Code** — fork the repo, work on a branch, open a pull request
- **Testing** — try it on your platform and report what breaks
- **Documentation** — improve this README or write a wiki page
- **Design** — UI/UX feedback and mockups are very valuable

### Code style

- C++20, Qt6 idioms
- No raw owning pointers — use `std::unique_ptr` or Qt parent-child ownership
- Engine code (`src/engine/`) must stay UI-free
- Keep commits focused; one logical change per commit

### Getting started

```bash
# Fork on GitHub, then:
git clone https://github.com/<your-username>/oql.git
cd oql
cmake -B build && cmake --build build -j$(nproc)
```

Open an issue or start a discussion if you want to talk through an idea before diving in.

---

## License

OQL is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Acknowledgements

Inspired by [QLab](https://qlab.app/) by Figure 53 — the gold standard for show control. OQL is an independent project with no affiliation to Figure 53.
