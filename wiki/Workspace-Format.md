# Workspace Format

OQL workspaces are saved as `.oqlab` files — plain JSON, human-readable and version-control friendly.

---

## Top-level structure

```jsonc
{
  "version": 1,
  "settings": { … },
  "cues": [ … ]
}
```

| Key | Type | Description |
|---|---|---|
| `version` | integer | Format version (currently `1`) |
| `settings` | object | Project-level settings (see below) |
| `cues` | array | Ordered list of cue objects |

---

## Settings object

```jsonc
"settings": {
  "defaultFadeIn":  0.0,
  "defaultFadeOut": 2.0,
  "autoNumber":     true
}
```

---

## Cue objects

Every cue shares these base fields:

```jsonc
{
  "id":          "a3f2…",   // UUID, stable across renames
  "cueType":     "audio",   // see table below
  "number":      "1",
  "name":        "Opening music",
  "notes":       "",
  "preWait":     0.0,
  "postWait":    0.0,
  "autoContinue": false,
  "autoFollow":   false
}
```

### cueType values

| `cueType` | Cue |
|---|---|
| `audio` | Audio cue |
| `video` | Video cue |
| `text` | Text cue |
| `mic` | Microphone cue |
| `stop` | Stop cue |
| `pause` | Pause cue |
| `play` | Play cue |
| `fade` | Fade cue |
| `speed` | Speed cue |
| `effect` | Effect cue |
| `reseteffect` | Reset Effect cue |
| `group` | Group cue |
| `label` | Label cue |

---

## Type-specific fields

### Audio cue

```jsonc
{
  "cueType":       "audio",
  "filePath":      "/home/user/audio/track.mp3",
  "volume":        1.0,          // linear (1.0 = 0 dB)
  "loopCount":     0,
  "fadeIn":        0.0,
  "fadeOut":       2.0,
  "trimStart":     0.0,
  "trimEnd":       0.0,
  "channelRoute":  0,            // 0=stereo 1=left 2=right
  "volumePoints":  [[0.0, 1.0], [30.0, 0.8]],  // [time_s, level_linear]
  "plugins":       [ … ]         // plugin chain array (see Plugin chain)
}
```

### Video cue

```jsonc
{
  "cueType":   "video",
  "filePath":  "/home/user/video/intro.mp4",
  "volume":    1.0,
  "loopCount": 0
}
```

### Text cue

```jsonc
{
  "cueType":         "text",
  "text":            "Act I, Scene 1",
  "fontFamily":      "Sans Serif",
  "fontSize":        72,
  "bold":            false,
  "italic":          false,
  "textColor":       "#ffffff",
  "backgroundColor": "#000000",
  "alignment":       132          // Qt::AlignCenter bitmask
}
```

### Microphone cue

```jsonc
{
  "cueType":       "mic",
  "inputDeviceId": "",    // empty = default device
  "volume":        1.0
}
```

### Control cues (Stop / Pause / Play / Speed)

```jsonc
{
  "cueType":  "stop",       // or "pause", "play"
  "targetId": "a3f2…"       // UUID of the target cue
}
```

```jsonc
{
  "cueType":  "speed",
  "targetId": "a3f2…",
  "rate":     1.5
}
```

### Fade cue

```jsonc
{
  "cueType":      "fade",
  "targetId":     "a3f2…",
  "targetVolume": 0.0,     // linear
  "fadeDuration": 3.0,
  "stopAtEnd":    true
}
```

### Effect cue

```jsonc
{
  "cueType":  "effect",
  "targetId": "a3f2…",
  "duration": 0.0,         // 0 = permanent
  "chain":    [ … ]        // plugin chain array (see Plugin chain)
}
```

### Reset Effect cue

```jsonc
{
  "cueType":  "reseteffect",
  "targetId": "a3f2…"
}
```

### Group cue

```jsonc
{
  "cueType":   "group",
  "collapsed": false,
  "children":  ["uuid1", "uuid2"]   // ordered list of member cue UUIDs
}
```

### Label cue

```jsonc
{
  "cueType": "label"
}
```

---

## Plugin chain array

Used in Audio cues (`"plugins"`) and Effect cues (`"chain"`).

```jsonc
[
  {
    "type":   "vst2",
    "path":   "/usr/lib/vst/ZynReverb.so",
    "active": true,
    "params": [0.8, 0.4, 0.5, …]   // one float per parameter, in order
  },
  {
    "type":   "vst2",
    "path":   "/usr/lib/vst/ZynAlienWah.so",
    "active": true,
    "chunk":  "BASE64…"   // binary state blob (for chunk-capable plugins)
  },
  {
    "type":   "lv2",
    "uri":    "http://plugin.org/reverb",
    "active": true,
    "params": [0.5, 0.3]
  }
]
```

Plugin state is saved either as `params` (array of floats, one per parameter) or as `chunk` (base64 binary blob). Chunk takes precedence when present — it is used for plugins that report the `effFlagsProgramChunks` VST2 flag (e.g. ZynFX).

---

## Editing the file by hand

`.oqlab` files are safe to edit in any text editor. Useful operations:

- **Rename or move audio files** — update `"filePath"` values.
- **Duplicate a cue** — copy a JSON object and assign a new UUID (`id`).
- **Bulk-adjust volumes** — find/replace `"volume"` in an audio cue section.
- **Strip all effects** — set every `"plugins"` array to `[]`.

Always make a backup before hand-editing. OQL validates the format on open and will report parse errors in the console.
