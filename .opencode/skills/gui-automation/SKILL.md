---
name: GUI automation
description: Drive SECMotionModel GUI headlessly with the JSONL command protocol, take deterministic screenshots, validate the JSON results and attach golden-image tests. Use for automated agent testing of the GUI.
---

# GUI automation

The GUI is built so agents can test it without a desktop: `--headless` never
opens a window, renders offscreen with WebGPU, writes PNG screenshots and
prints one JSON result per command on stdout.

## Quick start

```bash
secmotion-gui --headless --strict \
  --scene examples/minimal/scene.json \
  --tasks examples/minimal/tasks.jsonl \
  --script examples/minimal/demo.jsonl \
  --output-dir out
```

Exit codes: `0` success, `2` invalid arguments, `3` scene error,
`4` GPU/render error, `5` command error under `--strict`.

## Protocol

- stdout: one JSON object per line (plus a `ready` line at startup).
- stderr: human logs.
- Each line is a command `{"cmd": "...", ...}`; the result echoes `seq`, `cmd`
  and adds `ok` plus a `result` or `error` object.

```jsonl
{"cmd":"open","scene":"examples/minimal/scene.json","tasks":"examples/minimal/tasks.jsonl"}
{"cmd":"seek","frame":2}
{"cmd":"camera","mode":"vessel","target":"V-ATLANTIC","distance_km":2200}
{"cmd":"layer","name":"isl","visible":true}
{"cmd":"screenshot","path":"out/minimal.png","width":1280,"height":720}
{"cmd":"state"}
{"cmd":"quit"}
```

Screenshot results include `path`, `width`, `height`, `sha256`, `mean`, `std`,
`nonblank`. Prefer `state` for assertions on visible counts instead of parsing
images.

## Recording from the interactive GUI

```bash
secmotion-gui --scene ... --record-script out/session.jsonl
```

Every user action is recorded as the equivalent JSONL command. Replay it
headless to reproduce a GUI session:

```bash
secmotion-gui --headless --scene ... --script out/session.jsonl
```

## Golden tests

1. Run the script headless and collect screenshots.
2. Compare with `tests/e2e/compare.py` (mean absolute error or SSIM tolerance).
3. Same-platform runs must be byte-identical; cross-platform runs use the
   tolerance because GPU rasterization differs slightly.

Never compare hashes across Linux, macOS and Windows.

## Evidence for issues

Attach the generated PNG, stdout JSON and the exact scene/script to the issue
using the `gh-issues` skill. Redact absolute paths and hostnames when needed.
