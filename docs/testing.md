# Testing

## Layers

1. **Unit tests** (`tests/unit`): SHA-256, CLI parsing; more as modules land.
2. **Schema tests**: `schema/*.json` against the examples.
3. **End-to-end headless** (`tests/e2e`): JSONL scripts, JSON result assertions,
   PNG checks.
4. **Platform smoke tests**: `.exe` under Wine, AppImage.
5. **Golden screenshots**: same-platform exact, cross-platform tolerant.

## Local preflight

```bash
bash scripts/preflight_linux.sh    # Docker: build, ctest, headless, windowed, AppImage
bash scripts/preflight_windows.sh  # Docker: static .exe, Wine headless + windowed
```

Run both before triggering the workflow. macOS is not part of the supported
matrix.

## Headless end-to-end

```bash
secmotion-gui --headless --strict \
  --scene examples/minimal/scene.json \
  --tasks examples/minimal/tasks.jsonl \
  --script examples/minimal/demo.jsonl \
  --output-dir out/e2e
```

Assertions for an agent:

- exit code `0`;
- every stdout line parses as JSON and has `"ok": true`;
- the `screenshot` result has the requested dimensions and `nonblank: true`;
- `state` reports the expected `frame`, `frame_count` and layers;
- the PNG exists at the reported absolute path.

## Windowed smoke test

The interactive window can be exercised without a physical display:

```bash
xvfb-run -a -s "-screen 0 1280x720x24" \
  ./build/linux-release/secmotion-gui --backend vulkan --strict \
    --scene examples/minimal/scene.json \
    --tasks examples/minimal/tasks.jsonl \
    --window-frames 60
```

`--window-frames N` closes the window after N rendered frames, so the command
exits with code `0` and exercises GLFW, the WebGPU surface, Dear ImGui and the
full render loop. The Docker Linux build runs this test on both the native
binary and the packaged AppImage.

## Golden images

- `examples/minimal/golden/` contains Linux/lavapipe references.
- Same platform: compare `sha256` from the `screenshot` result.
- Cross platform: compare pixels with mean absolute error below a small
  tolerance (GPU rasterization differs at the last bit), or SSIM above 0.98.
- Never hardcode a platform-specific hash as a global truth.

## Windows-specific testing

See `.opencode/skills/windows-testing/SKILL.md`: static import checks
(`objdump -p`), Wine + lavapipe, winetricks, Bottles fallback and the failure
matrix.

## CI

`.github/workflows/build.yml` has two jobs (Windows Docker + Wine, Linux
native) each building, smoke testing and uploading one artifact plus
`SHA256SUMS`. It starts as `workflow_dispatch` only; tag triggers are enabled
after the preflight is green.

## Reporting failures

Use the `gh-issues` skill: exact command, commit, backend/adapter line from the
`ready` event, sanitized logs, screenshot, and the scene/script used. Attach
media through the `agent-media` release with `scripts/gh_attach_media.sh`.
