# OpenQLab

**OpenQLab** is a free and open-source show-control application inspired by QLab, built with C++20 and Qt6. It is designed for live theatre, concerts, and events — letting you build and fire cue lists with precision timing, audio/video playback, and flexible automation.

> **This project is open to external contributions.** Whether you're a developer, sound designer, or live-events professional, your input is welcome.

---

## Features

- **Audio cues** — playback via QMediaPlayer; per-cue volume, loop count, fade in/out curves, trim start/end, channel routing (stereo / L / R)
- **Video cues** — fullscreen output on a dedicated window; loop support
- **Waveform view** — zoomable waveform with visual trim handles, fade envelope, and per-point volume automation
- **Fade cues** — smooth volume interpolation on any audio/video cue; optional auto-stop at end of fade
- **Stop / Pause / Play / Speed cues** — full transport control over any running cue
- **Microphone cues** — live audio passthrough from any input device to output
- **Group cues** — collapsible organisational containers; collapse a group to a single row in the cue list
- **Label cues** — visual-only markers for annotating your cue list
- **Pre-wait / post-wait / auto-continue / auto-follow** — flexible timing chains
- **Undo / redo** — full snapshot-based undo stack
- **Recent files** — quick access to recent workspaces
- **Project settings** — show-level defaults for fades and cue numbering
- **Drag-and-drop** reordering and target assignment in the cue list

---

## Screenshots

*Coming soon.*

---

## Building

### Requirements

- CMake ≥ 3.20
- Qt 6.4+ (Core, Widgets, Multimedia, MultimediaWidgets)
- A C++20-capable compiler (GCC 11+, Clang 14+, MSVC 2022+)

### Linux / macOS

```bash
git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/openqlab
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
│   ├── AudioCue           QMediaPlayer-based audio playback
│   ├── VideoCue           Video playback with QVideoWidget
│   ├── ControlCues        Stop, Fade, Pause, Speed, Play
│   ├── MicCue             Live microphone passthrough
│   ├── GroupCue           Collapsible cue container
│   ├── LabelCue           Visual-only label
│   ├── CueList            Flat cue list with playhead and autofollow
│   ├── Workspace          File I/O (.oqlab JSON format)
│   └── AppSettings        Per-user settings via QSettings
└── ui/              # Qt Widgets UI
    ├── MainWindow         Main application window
    ├── CueListView        Custom table view with drag-and-drop
    ├── CueListModel       QAbstractTableModel with visible-row mapping
    ├── InspectorPanel     Per-cue property editor
    ├── WaveformView       Waveform rendering and editing
    ├── ActiveCuesPanel    Live view of currently playing cues
    ├── CueInfoBar         Bottom status bar
    ├── VideoOutputWindow  Dedicated fullscreen video output
    └── SettingsDialog     Project and app settings
```

---

## Workspace Format

Workspaces are saved as `.oqlab` files (JSON). The format is intentionally simple and human-readable — you can inspect and edit them with any text editor.

---

## Roadmap

- [ ] MIDI / OSC trigger support
- [ ] Multi-output audio routing
- [ ] Network sync between multiple instances
- [ ] Scripted cues (Lua or QJSEngine)
- [ ] macOS / Windows packaging
- [ ] Dark theme polish and HiDPI improvements
- [ ] Inspector combo for assigning cues to groups

---

## Contributing

**OpenQLab is actively looking for contributors.** All skill levels and backgrounds are welcome.

### Ways to contribute

- **Bug reports** — open an [issue](https://github.com/teonactl/oql/issues) with steps to reproduce
- **Feature requests** — describe the use case in an issue before writing code
- **Code** — fork the repo, work on a branch, open a pull request
- **Testing** — try it on your platform and report what breaks
- **Documentation** — improve this README, add inline comments, write a wiki page
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

OpenQLab is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Acknowledgements

Inspired by [QLab](https://qlab.app/) by Figure 53 — the gold standard for show control. OpenQLab is an independent project with no affiliation to Figure 53.
