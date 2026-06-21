# Getting Started

## Installation

### Linux (recommended)

Build from source and install with the provided script (no root required):

```bash
git clone https://github.com/teonactl/oql.git
cd oql
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./install.sh
```

`install.sh` copies the binary to `~/.local/bin`, installs the icon and creates a `.desktop` launcher for your desktop environment (KDE, GNOME, XFCE, MATE, Cinnamon, LXQt, Budgie, …).

> **Requirements:** CMake ≥ 3.20, Qt 6.4+, GCC 11+ or Clang 14+.
> See [Building from Source](Building) for the full dependency list.

### macOS / Windows

Build from source — see [Building from Source](Building).
Binary packages are not yet available; they are on the [roadmap](https://github.com/teonactl/oql/blob/main/README.md#roadmap).

---

## First launch

When OQL opens you see three main areas:

```
┌─────────────────────────────────────────────────────────┐
│  Menu bar                                               │
├──────────────────────┬──────────────────────────────────┤
│                      │                                  │
│   Cue List           │   Inspector Panel                │
│   (left)             │   (right)                        │
│                      │                                  │
├──────────────────────┴──────────────────────────────────┤
│  Active Cues Panel (bottom)    │  Cue Info Bar          │
└─────────────────────────────────────────────────────────┘
```

| Area | Purpose |
|---|---|
| **Cue List** | Your show's sequence of cues. Click a row to select it, Space/Go to fire. |
| **Inspector Panel** | Edit the properties of the currently selected cue. |
| **Active Cues Panel** | Live view of all currently playing/active cues with progress bars. |
| **Cue Info Bar** | Shows number, name and state of the next cue to fire. |

---

## Basic workflow

### 1. Create a new workspace

`File → New` — starts an empty show. Save immediately with `Ctrl+S` to a `.oqlab` file.

### 2. Add cues

Right-click in the cue list or use the **Add** menu:

| Shortcut | Cue |
|---|---|
| `Ctrl+Shift+A` | Audio cue |
| `Ctrl+Shift+V` | Video cue |
| `Ctrl+Shift+T` | Text cue |
| `Ctrl+Shift+M` | Microphone cue |
| `Ctrl+Shift+G` | Group cue |
| `Ctrl+Shift+L` | Label cue |

Control cues (Stop, Fade, etc.) are available from the Add menu.

### 3. Configure a cue

Click a cue to select it. The **Inspector Panel** on the right shows all editable parameters for that cue type. For Audio cues this includes the waveform view with trim handles and fade envelopes.

### 4. Fire cues

- **Space** or the **Go** button fires the selected/next cue.
- Click the **▶ Play** button in the Inspector to test a single cue.
- During a show, press Space repeatedly to advance through the list.

### 5. Timing and chaining

Each cue has:

| Field | Effect |
|---|---|
| **Pre-wait** | Pause before the cue starts (seconds) |
| **Post-wait** | Pause after the cue finishes before triggering the next |
| **Auto-continue** | Fire the next cue as soon as this one *starts* |
| **Auto-follow** | Fire the next cue as soon as this one *ends* |

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| `Space` | Go (fire next cue) |
| `Escape` | Stop all active cues |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save as |
| `Delete` | Delete selected cue(s) |
| `Ctrl+D` | Duplicate selected cue |
| `F2` | Rename selected cue inline |
| `Ctrl+G` | Group selected cues |

---

## Saving and opening

Workspaces are saved as `.oqlab` files (plain JSON). The format is human-readable; see [Workspace Format](Workspace-Format).

Recent files appear under `File → Recent Files`.
