# Command reference

Every user-facing action exists both as a JSONL command and as a GUI control
with a keyboard shortcut. The command registry is the single source of truth:
GUI widgets dispatch commands through the same `CommandBus` used by the JSONL
driver, and `--record-script` writes GUI actions back as JSONL.

## CLI

| Option | Meaning |
|---|---|
| `--scene PATH` | scene manifest (`scene.json` or a directory containing `manifest.json`) |
| `--import PATH` | scene or run directory loaded at startup; runs are exported first |
| `--tasks PATH` | task trace JSONL, overrides the manifest |
| `--script PATH` | JSONL command script, `-` reads stdin |
| `--output-dir DIR` | base directory for relative screenshot paths |
| `--record-script PATH` | record GUI actions as JSONL, replayable headless |
| `--headless` | no window, offscreen rendering |
| `--screenshot PATH` | render one frame and exit |
| `--frame N`, `--time S` | initial frame/time (windowed) or screenshot frame/time |
| `--width N`, `--height N` | screenshot size, default 1280x720 |
| `--window-frames N` | auto-close the interactive window after N frames |
| `--play` | start the windowed timeline immediately |
| `--backend NAME` | `auto`, `vulkan`, `d3d12`, `d3d11`, `metal`, `opengl`, `null` |
| `--camera MODE` | initial camera: `orbit`, `topdown`, `vessel`, `satellite`, `free` |
| `--target NAME` | initial camera target entity |
| `--layer NAME=on\|off` | initial layer state, repeatable |
| `--validate-only` | validate a scene, no GPU required |
| `--strict` | stop with exit code 5 on the first failing command |
| `--log-level LEVEL` | `debug`, `info`, `warn`, `error` |
| `-q`, `--quiet` | keep stdout machine-readable |
| `-v`, `--version`, `-h`, `--help` | version and usage |

Exit codes: `0` success, `2` usage, `3` scene, `4` GPU, `5` command error.

## Protocol

- stdout: one JSON object per line; the first is a `ready` event.
- stderr: human-readable logs.
- A command line is `{"cmd": "...", ...fields}`; `protocol` and `seq` are
  optional on input and echoed on output.
- `--strict` makes the first error fatal.

Result:

```json
{"protocol":1,"seq":4,"cmd":"screenshot","ok":true,"result":{"path":"/abs/out/frame.png","width":1280,"height":720,"bytes":3686400,"sha256":"...","mean":12.4,"std":31.2,"nonblank":true,"frame":2,"time_s":4.0}}
```

Error:

```json
{"protocol":1,"seq":4,"cmd":"seek","ok":false,"error":{"code":"OUT_OF_RANGE","message":"frame 99 is outside [0, 2]"}}
```

Codes: `PARSE_ERROR`, `MISSING_CMD`, `UNKNOWN_COMMAND`, `MISSING_FIELD`,
`INVALID_FIELD`, `OUT_OF_RANGE`, `NO_SCENE`, `SCENE_ERROR`, `INTERNAL_ERROR`.

## Commands and GUI equivalents

| JSONL | Effect | GUI control | Shortcut |
|---|---|---|---|
| `open` | load scene and optional tasks | command palette, Import panel | `Ctrl+K` |
| `play` / `pause` | start/stop playback | timeline toolbar, HUD | `Space` |
| `seek` | go to `frame` or `time` | timeline scrubber | — |
| `step` | move by `delta` frames (default 1) | `<` / `>` buttons | `←` / `→` |
| `speed` | playback speed | timeline slider | `[` / `]` |
| `camera` | mode, target, distance | telemetry camera combo | `1`–`5` |
| `layer` | show/hide a layer (`load`, `traffic`, …) | Layers panel | `G` `I` `D` `O` `K` |
| `select` | select an entity | click satellite/vessel/observer | — |
| `reset` | back to frame 0, paused | Reset button | `R` |
| `state` | current state as JSON | HUD + command palette | always visible |
| `screenshot` | render PNG | Screenshot button | `F12` |
| `validate` | re-check the scene | command palette | — |
| `quit` | exit | window close | `Ctrl+Q` |

The windowed interface dispatches every widget action through the same
`CommandBus` used by the JSONL driver. With `--record-script PATH`, each
successful action is appended to `PATH` as a JSONL command, including the
initial `open`, camera, speed and layer state; replaying that file with
`--headless --script` reproduces the same screenshots.

Without `--scene`, the window starts empty and shows the **Import scene**
panel. It browses directories, loads a scene directly, and exports a simulation
run (`gui_run.json`) through the core Python exporter before loading it. The
same flow is available non-interactively with `--import DIR`.

## Live telemetry layers (scene v2)

A v2 manifest can reference `loads.jsonl` and `traffic.jsonl`. They are loaded
automatically; no extra CLI flag is needed.

- `load` colors each satellite by
  `max((cpu_queue+cpu_service)/cpu_capacity, (net_queue+net_service)/net_capacity)`.
  The color goes from pale blue (idle) through yellow and red to dark red/black
  (saturated). The telemetry panel shows the raw queues and energy.
- `traffic` colors ISL links by recent traffic and draws a moving wave from
  source to destination for each active forward. The wave duration is stretched
  to at least 0.35 s when the real transmission is shorter, so it remains
  visible at 60 FPS.
- Both layers can be toggled from the Layers panel or with
  `--layer load=off` / `--layer traffic=off`.

Mouse: drag to orbit, scroll to zoom, click to select. A two-finger trackpad
scroll zooms smoothly; hold `Shift` for finer steps. `+` and `-` zoom from the
keyboard. Compositors that expose pinch as `Ctrl+wheel` take the same zoom
path; GLFW has no native pinch-gesture API. Panels: `L` toggles Layers, `T`
toggles Telemetry, `Ctrl+K` toggles the JSON command palette.

## Example script

```jsonl
{"cmd":"open"}
{"cmd":"camera","mode":"vessel","target":"V-ATLANTIC","distance_km":2200}
{"cmd":"layer","name":"isl","visible":true}
{"cmd":"seek","frame":2}
{"cmd":"screenshot","path":"minimal_frame2.png","width":1280,"height":720}
{"cmd":"state"}
{"cmd":"quit"}
```

When `open` has no `scene` field it uses the `--scene` path passed on the
command line, so the same script works on every platform and directory.

## Reproducibility

- `seek` before `screenshot` for deterministic output.
- Headless screenshots are rendered offscreen at the requested size, so they do
  not depend on the window size or DPI.
- Same-platform runs must produce the same `sha256`; compare across platforms
  with a perceptual tolerance (see `docs/testing.md`).
