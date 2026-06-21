# OQL Wiki

**OQL** is a free, open-source show-control application for live theatre, concerts and events — inspired by QLab, built with C++20 and Qt6.

---

## Documentation

For an illustrated introduction with screenshots and step-by-step guidance, see the **[online documentation](https://teonactl.github.io/oql/docs)**.

---

## Contents

| Page | Description |
|---|---|
| [Getting Started](Getting-Started) | Installation, first launch, basic workflow |
| [Cue Types](Cue-Types) | Reference for every cue type and its parameters |
| [Plugin Effects (VST2 / LV2)](Plugin-Effects) | Loading and using audio effects plugins |
| [Workspace Format](Workspace-Format) | `.oqlab` JSON file format reference |
| [Building from Source](Building) | Build instructions for Linux, macOS, Windows |
| [Architecture](Architecture) | Internal design for contributors |

---

## Quick start

```bash
git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/oql
# — or install with icon/launcher —
./install.sh
```

---

## Feature overview

- **Audio cues** — miniaudio-based stereo playback; per-cue volume, loops, fade in/out, trim, channel routing, volume automation curve
- **Video cues** — fullscreen video output on a secondary window
- **Text cues** — on-screen text display with full typography control
- **Microphone cues** — live audio passthrough from any input device
- **Control cues** — Stop, Pause, Play, Fade, Speed targeting any other cue
- **Effect cues** — apply a VST2/LV2 plugin chain to an Audio cue for a fixed duration or permanently; auto-reset with ResetEffect cue
- **Group cues** — collapsible containers for organising large cue lists
- **Label cues** — visual-only section markers
- **Timing chains** — pre-wait, post-wait, auto-continue, auto-follow
- **Undo/redo** — full snapshot-based undo stack
- **VST2 and LV2 plugin support** — per-cue effect chains with parameter editing and native GUI editors

---

## License

GPL-3.0 — see [LICENSE](https://github.com/teonactl/oql/blob/main/LICENSE).
