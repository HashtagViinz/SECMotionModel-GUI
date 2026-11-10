# Command reference

Every user-facing action exists both as a JSONL command and as a GUI control
with a keyboard shortcut. The command registry is the single source of truth:
GUI widgets dispatch commands through the same `CommandBus` used by the JSONL
driver, and `--record-script` writes GUI actions back as JSONL.

## CLI

| Option | Meaning |
|---|---|
| `--scene PATH` | scene manifest (`scene.json` or a directory containing `manifest.json`) |
| `--tasks PATH` | task trace JSONL, overrides the manifest |
| `--script PATH` | JSONL command script, `-` reads stdin |
| `--output-dir DIR` | base directory for relative screenshot paths |
| `--record-script PATH` | record GUI actions as JSONL (Phase 4) |
| `--headless` | no window, offscreen rendering |
| `--screenshot PATH` | render one frame and exit |
| `--frame N`, `--time S` | frame or time for `--screenshot` |
| `--width N`, `--height N` | screenshot size, default 1280x720 |
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
| `open` | load scene and optional tasks | File → Open | `Ctrl+O` |
| `play` / `pause` | start/stop playback | toolbar | `Space` |
| `seek` | go to `frame` or `time` | timeline scrubber | `←` / `→` |
| `step` | move by `delta` frames (default 1) | step buttons | `Shift+←/→` |
| `speed` | playback speed | speed menu | `[` / `]` |
| `camera` | mode, target, distance | camera menu | `1`–`5` |
| `layer` | show/hide a layer | layer panel | `I`, `D`, `T`, `O` |
| `select` | select an entity | click or search | `Enter` |
| `reset` | back to frame 0, paused | reset button | `R` |
| `state` | current state as JSON | status panel | always visible |
| `screenshot` | render PNG | F12 / Export | `F12` |
| `validate` | re-check the scene | Scene → Validate | — |
| `quit` | exit | File → Quit | `Ctrl+Q` |

Phase 1 implements `open`, `seek`, `step`, `play`, `pause`, `speed`, `camera`,
`layer`, `select`, `reset`, `state`, `screenshot`, `validate`, `quit`.
The renderer body is replaced in Phase 3; the protocol stays stable.

## Example script

```jsonl
{"protocol":1,"cmd":"open","scene":"examples/minimal/scene.json","tasks":"examples/minimal/tasks.jsonl"}
{"cmd":"camera","mode":"vessel","target":"V-ATLANTIC","distance_km":2200}
{"cmd":"layer","name":"isl","visible":true}
{"cmd":"seek","frame":2}
{"cmd":"screenshot","path":"minimal_frame2.png","width":1280,"height":720}
{"cmd":"state"}
{"cmd":"quit"}
```

## Reproducibility

- `seek` before `screenshot` for deterministic output.
- Headless screenshots are rendered offscreen at the requested size, so they do
  not depend on the window size or DPI.
- Same-platform runs must produce the same `sha256`; compare across platforms
  with a perceptual tolerance (see `docs/testing.md`).
