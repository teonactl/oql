# Plugin Effects (VST2 / LV2)

OpenQLab supports audio effect plugins in two formats:

| Format | Typical location (Linux) |
|---|---|
| **VST2** (`.so`) | `/usr/lib/vst/`, `/usr/lib/lxvst/`, `~/.vst/` |
| **LV2** (bundle) | `/usr/lib/lv2/`, `~/.lv2/` |

---

## Where plugins are used

Each **Audio cue** has its own permanent effect chain, editable from the Inspector.

An **Effect cue** carries a separate chain that is temporarily applied to a target Audio cue when fired, then removed automatically after a set duration (or when a Reset Effect Cue fires).

---

## Opening the effect chain editor

1. Select an Audio cue or an Effect cue in the cue list.
2. In the Inspector Panel, click **⚙ Effetti…**.
3. A floating window opens showing the plugin chain for that cue.

Each cue type has its own independent editor window — the Audio cue editor and the Effect cue editor never share state.

---

## Adding a plugin

1. Click **+ Aggiungi effetto** in the chain editor.
2. A dialog shows two tabs:
   - **VST2** — scans `/usr/lib/vst/`, `/usr/lib/lxvst/` and `~/.vst/`. Use the search box to filter.
   - **LV2** — lists all LV2 plugins found by the system's LV2 host libraries.
3. Select a plugin and click **OK**.

The plugin is added to the end of the chain and prepared immediately with the engine's current sample rate and block size (48 kHz / 512 frames by default).

---

## Reordering and removing plugins

| Button | Action |
|---|---|
| **▲ / ▼** | Move the selected plugin up or down in the chain |
| **−** | Remove the selected plugin from the chain |

The chain is processed in top-to-bottom order: the output of each plugin feeds the input of the next.

---

## Editing parameters

Clicking a plugin in the list shows its parameters as sliders below. Drag a slider to change the value in real time — changes apply immediately to live audio.

The **Attivo** checkbox at the top of the parameter list bypasses the plugin without removing it from the chain.

---

## Native GUI editor (VST2)

VST2 plugins that provide a graphical editor show an **Apri editor grafico del plugin** button. Clicking it opens the plugin's own UI in a separate window.

> **Note:** Native VST2 editors require X11. On Wayland, make sure XWayland is running. OpenGL-based editors (e.g. those using JUCE) may require Mesa: `sudo pacman -S mesa` / `sudo apt install mesa-utils`.

---

## Audio engine settings

OpenQLab uses **miniaudio** for low-latency playback with a fixed block size of **512 frames**. This value is also passed to VST2 plugins via `effSetBlockSize`, ensuring the plugin and the engine always agree on buffer size. The sample rate is chosen automatically by the driver.

---

## Saving plugin state

Plugin state is saved as part of the `.oqlab` workspace file.

- **Chunk-based plugins** (those that report `effFlagsProgramChunks` in their VST2 flags — e.g. most ZynFX plugins) save their entire internal state as a base64-encoded binary blob.
- **Parameter-based plugins** save the full array of float parameter values.

State is restored when the workspace is reopened and when an Effect cue is reapplied.

---

## Effect Cue workflow example

```
Cue 10  Audio cue  "Ambient loop"           (no effects initially)
Cue 11  Effect cue "Add reverb"  target→10  duration=0 (permanent)
            └─ chain: ZynReverb  (room size 0.8, damp 0.4)
Cue 12  ResetEffect cue          target→10  (restores clean chain)
```

Firing cue 11 adds ZynReverb to the ambient loop in real time. Firing cue 12 removes it and restores the original chain.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Plugin not listed | `.so` / bundle not in scanned folders | Copy plugin to `~/.vst/` or `~/.lv2/` |
| Buzzing / noise on first use | Block size mismatch | Ensure miniaudio block size = 512 (default) |
| Editor window black | OpenGL unavailable | Install Mesa: `sudo pacman -S mesa` |
| Crash on parameter change | Plugin not thread-safe | Update to latest OpenQLab (mutex fix applied since v0.1) |
| Parameters lost after reload | Plugin uses chunk state | Should save automatically; report as a bug if not |
