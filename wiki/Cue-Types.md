# Cue Types

Every cue shares a common set of fields, then has type-specific parameters.

---

## Common fields (all cues)

| Field | Description |
|---|---|
| **Number** | Alphanumeric identifier shown in the cue list (e.g. `1`, `1.5`, `Q10`) |
| **Name** | Human-readable label |
| **Notes** | Free-text notes, visible only in the Inspector |
| **Pre-wait** | Seconds to wait after the cue is triggered before it actually starts |
| **Post-wait** | Seconds to wait after the cue finishes before firing the next one |
| **Auto-continue** | Immediately fire the next cue when *this* cue starts |
| **Auto-follow** | Immediately fire the next cue when *this* cue ends |

---

## Audio Cue

Plays an audio file through the system output.

**Supported formats:** MP3, WAV, FLAC, AAC, OGG and anything supported by the system's FFmpeg/GStreamer backend.

| Parameter | Description |
|---|---|
| **File** | Path to the audio file. Click the browse button `…` to pick a file. |
| **Volume** | Playback level in dB (default 0 dB) |
| **Loop count** | Number of additional repeats after the first play (0 = play once) |
| **Channel** | Stereo / Left only / Right only |
| **Fade in** | Duration in seconds of a linear fade-in at the start |
| **Fade out** | Duration in seconds of a linear fade-out at the end |
| **Trim start / end** | Skip the first/last N seconds of the file |
| **Volume automation** | Click the waveform to add/drag volume envelope points |
| **Effects chain** | Click *Effetti…* to add VST2 or LV2 plugins (see [Plugin Effects](Plugin-Effects)) |

### Waveform view

The waveform view shows the decoded audio. You can:

- **Drag the left/right trim handles** to set trim-start and trim-end.
- **Click on the fade-in/fade-out region** to drag the fade duration.
- **Click in the body** to add a volume automation point; drag points to adjust level.
- **Right-click a point** to remove it.

---

## Video Cue

Plays a video file on a dedicated fullscreen output window.

| Parameter | Description |
|---|---|
| **File** | Path to the video file |
| **Volume** | Audio level of the video's soundtrack (dB) |
| **Loop count** | Number of additional repeats |

The video output window opens on the secondary display (if available) or in a floating window. It is always on top and borderless during playback.

---

## Text Cue

Displays formatted text on a dedicated output window (useful for supertitles, lyrics, or presentation slides).

| Parameter | Description |
|---|---|
| **Text** | The content to display |
| **Font / Size** | Typography settings |
| **Bold / Italic** | Style toggles |
| **Text colour** | Foreground colour |
| **Background colour** | Window background |
| **Alignment** | Left / Centre / Right |

The text window behaves like the video output window — fullscreen-capable, stays on top.

---

## Microphone Cue

Passes live audio from an input device to the output.

| Parameter | Description |
|---|---|
| **Input device** | Select from detected audio input devices (or Default) |
| **Volume** | Gain applied to the live signal (dB) |

The mic cue stays active until explicitly stopped.

---

## Stop Cue

Stops a target cue immediately.

| Parameter | Description |
|---|---|
| **Target** | The cue to stop (selected from the cue list) |

---

## Pause Cue

Pauses a playing cue. Fire again or use a Play cue to resume.

| Parameter | Description |
|---|---|
| **Target** | The cue to pause |

---

## Play Cue

Resumes a paused cue, or restarts a stopped one.

| Parameter | Description |
|---|---|
| **Target** | The cue to resume / restart |

---

## Fade Cue

Smoothly interpolates the volume of a target cue to a new level.

| Parameter | Description |
|---|---|
| **Target** | The Audio or Video cue whose volume to fade |
| **Target volume** | Destination level in dB |
| **Fade duration** | How long the fade takes (seconds) |
| **Stop at end** | If checked, stops the target cue when the fade reaches its destination |

---

## Speed Cue

Changes the playback rate of an Audio or Video cue.

| Parameter | Description |
|---|---|
| **Target** | The cue to speed up or slow down |
| **Rate** | Playback multiplier (1.0 = normal, 2.0 = double speed, 0.5 = half speed) |

---

## Effect Cue

Applies a VST2/LV2 plugin chain to a target Audio cue's audio stream.

| Parameter | Description |
|---|---|
| **Target** | The Audio cue to process |
| **Duration** | How long the effect stays active (seconds). `0` = permanent until stopped or reset. |
| **Effects chain** | Click *Effetti…* to build the plugin chain |

**How it works:**

1. When fired, the Effect cue saves a snapshot of the target's current plugin chain.
2. It then applies its own chain (merged into the target's chain).
3. After `duration` seconds (or when stopped), the target's original chain is restored.

A permanent Effect cue (duration = 0) remains visible in the Active Cues panel until manually stopped or until a **Reset Effect Cue** fires.

See [Plugin Effects](Plugin-Effects) for details on building effect chains.

---

## Reset Effect Cue

Restores the target Audio cue's plugin chain to the state it had before the last Effect cue was applied.

| Parameter | Description |
|---|---|
| **Target** | The Audio cue whose chain to reset |

---

## Group Cue

A collapsible container that holds other cues. Collapsing a group replaces its member rows with a single row in the cue list.

Groups fire all their members according to each member's auto-continue / auto-follow settings. A Group cue with no timing on its members fires them all simultaneously.

---

## Label Cue

A visual-only marker with no playback behaviour. Labels help organise long cue lists into sections (e.g. "ACT 1 — Scene 3").

Labels never fire, never appear in the Active Cues panel, and are skipped by the Go button.
