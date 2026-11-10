# SECMotionModel GUI

Native C++20/WebGPU visualizer for the [SECMotionModel](https://github.com/casalicchio/SECMotionModel) satellite edge-computing simulator.

The GUI renders LEO satellite motion, inter-satellite links, visibility domes and the way tasks travel between satellites. It is designed to be:

- **interactive**, with real-time GPU rendering (wgpu-native via [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp));
- **scriptable**, with a JSONL command protocol where every command has a GUI equivalent;
- **testable by agents**, thanks to deterministic offscreen rendering, PNG screenshots and JSON results on stdout;
- **portable**, with static Windows `.exe`, Linux `.AppImage` and macOS `.dmg` builds.

> Status: Phase 0 (bootstrap) and Phase 1 (build spike) in progress.
> Phase history is tracked in GitHub issues, one per phase.

## Layout

```text
src/         C++ sources (app, cli, scene, render, screenshot, util)
shaders/     WGSL shaders
schema/      JSON schemas for scene, tasks and command protocol
examples/    minimal scene, JSONL scripts, golden screenshots
docs/        command reference, scene format, testing
scripts/     preflight and packaging helpers
tests/       unit and end-to-end tests
```

## Build

Dependencies are fetched by CMake at configure time (WebGPU-distribution with
wgpu-native, GLFW, glfw3webgpu, nlohmann/json, stb).

### Linux

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

### Windows (cross-compile with MinGW in Docker)

```bash
docker build -f Dockerfile.windows --target export --output type=local,dest=dist .
```

The resulting `dist/secmotion-gui.exe` is statically linked: it must not import
`wgpu_native.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll` or
`libwinpthread-1.dll`. See `.opencode/skills/windows-testing/SKILL.md`.

### macOS

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

## Usage

```bash
# interactive
secmotion-gui --scene examples/minimal/scene.json --tasks examples/minimal/tasks.jsonl

# headless script with screenshots
secmotion-gui --headless --scene examples/minimal/scene.json \
  --script examples/minimal/demo.jsonl --output-dir out

# single screenshot
secmotion-gui --headless --scene examples/minimal/scene.json \
  --screenshot out/frame.png --frame 2 --width 1280 --height 720
```

See [docs/commands.md](docs/commands.md) for the full JSONL protocol and the
GUI mapping, and [examples/README.example.md](examples/README.example.md) for a
guided example.

## Relationship with the core repository

The GUI is a git submodule of `SECMotionModel` at path `gui/`. The Python core
exports GUI-ready data through `tools/export_gui_scene.py`:

```bash
cd ../SECMotionModel
python tools/export_gui_scene.py \
  --config data/naval_configurations_AP_optimal.json \
  --trace result/naval/IN_TRACE.jsonl \
  --out ../SECMotionModel-GUI/examples/generated/naval
```

Positions are exported in ECEF/ITRS, so the globe is earth-fixed and both the
vessel and the observer stay at their geographic coordinates.

## Agent communication

Bug reports and requests between the core and the GUI are exchanged through
GitHub issues, using the `gh-issues` skill available in both repositories:

- core: `.opencode/skills/gh-issues/SKILL.md`
- GUI: `.opencode/skills/gh-issues/SKILL.md`

Titles use the `[from-core]` / `[from-gui]` prefixes and media evidence is
attached as release/branch links.

## License

GPL-3.0-or-later. Third-party components are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
