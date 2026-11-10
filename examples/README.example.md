# SECMotionModel GUI — examples

This folder contains everything needed to exercise the GUI, including from
CI or from an autonomous agent.

## Contents

| Path | Purpose |
|---|---|
| `minimal/scene.json` | manifest of a 3-frame, 6-satellite handcrafted scene |
| `minimal/frames/*.json` | frame data in ECEF/ITRS kilometres |
| `minimal/tasks.jsonl` | 3 tasks moving between the vessel, the observer and satellites |
| `minimal/demo.jsonl` | JSONL command script used by the smoke tests |
| `scripts/` | extra command scripts (layers, camera tour, screenshots) |

The minimal scene is synthetic but uses real coordinates: the vessel is
`V-ATLANTIC` at 33.37°N 47.96°W and the observer is Rome. It is small enough to
be committed and fast enough for end-to-end tests.

## Quick start

Interactive:

```bash
secmotion-gui --scene examples/minimal/scene.json --tasks examples/minimal/tasks.jsonl
```

Headless with a command script (recommended for agents):

```bash
secmotion-gui --headless --strict \
  --scene examples/minimal/scene.json \
  --tasks examples/minimal/tasks.jsonl \
  --script examples/minimal/demo.jsonl \
  --output-dir out
```

Single deterministic screenshot:

```bash
secmotion-gui --headless \
  --scene examples/minimal/scene.json \
  --screenshot out/frame2.png --frame 2 --width 1280 --height 720
```

Validate a scene without a GPU (works in any container):

```bash
secmotion-gui --validate-only --scene examples/minimal/scene.json
```

## Expected output

The script above prints one JSON object per command on stdout and writes
`out/minimal_frame2.png`. Example result:

```json
{"protocol":1,"event":"ready","version":"0.1.0","backend":"vulkan","adapter":"llvmpipe"}
{"protocol":1,"seq":1,"cmd":"open","ok":true,"result":{"frame_count":3,"interval_s":2.0,"...":"..."}}
{"protocol":1,"seq":7,"cmd":"screenshot","ok":true,"result":{"path":"/.../out/minimal_frame2.png","width":1280,"height":720,"sha256":"...","nonblank":true}}
```

Exit codes: `0` success, `2` usage, `3` scene, `4` GPU, `5` command (with `--strict`).

## Generating a real scene from the core simulator

The full naval dataset lives in the `SECMotionModel` repository. Regenerate the
GUI scene after a simulation, starting from an existing log (no simulator rerun
needed):

```bash
cd SECMotionModel
python tools/parse_naval_log.py \
  --log result/naval_full_run_2026-11-10/naval_full_in_network.log \
  --out /tmp/trace.jsonl

python tools/export_gui_scene.py \
  --config data/naval_configurations_AP_optimal.json \
  --trace /tmp/trace.jsonl \
  --out ../SECMotionModel-GUI/examples/generated/naval \
  --start 0 --end 150
```

When the instrumented `--trace-jsonl` mode lands in the simulator, pass that
file as `--trace` instead: it also records exact hop durations and failures.

Generated data is ignored by git (`examples/generated/`); it can be large
(457 satellites per frame), so keep it local.

## Golden screenshots and agent testing

- `minimal/golden/` contains small reference PNGs produced on Linux with
  lavapipe. Same-platform runs must be byte-identical; compare across platforms
  with the tolerance-based script in `tests/e2e/compare.py`.
- Agents should assert on the `state` JSON (frame, counts, layers) and on the
  screenshot metadata (`nonblank`, dimensions, `sha256`) instead of parsing
  pixels.
- Use the `gui-automation` skill for the full workflow and the `gh-issues`
  skill to attach screenshots to issues.

## Troubleshooting

| Symptom | Action |
|---|---|
| `no WebGPU adapter available` | install a Vulkan ICD (Mesa lavapipe in CI), or try `--backend vulkan` |
| `scene manifest not found` | run commands from the repository root, or use absolute paths |
| screenshot is black | ensure `--headless` and check the readback support in `windows-testing` skill |
| Wine cannot find `wgpu_native.dll` | the build is not static; rebuild with `Dockerfile.windows` |
