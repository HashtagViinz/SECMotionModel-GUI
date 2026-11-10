# SECMotionModel GUI

Native C++20/WebGPU visualizer for the [SECMotionModel](https://github.com/casalicchio/SECMotionModel) satellite edge-computing simulator.

The GUI renders LEO satellite motion, inter-satellite links, visibility domes and the way tasks travel between satellites. It is designed to be:

- **interactive**, with real-time GPU rendering (wgpu-native via [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp));
- **scriptable**, with a JSONL command protocol where every command has a GUI equivalent;
- **testable by agents**, thanks to deterministic offscreen rendering, PNG screenshots and JSON results on stdout;
- **portable**, with static Windows `.exe` and Linux `.AppImage` builds.

> Status: Phase 3 (renderer, HUD, interactive window) and Phase 4 (timeline,
> layers, telemetry, command palette, recorder) implemented on Linux and
> validated on bare metal. Windows packaging is being finished next.

## Layout

```text
src/         C++ sources (app, cli, scene, render, screenshot, ui, util)
shaders/     WGSL shaders (embedded in src/render/Renderer.cpp)
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
# Debian/Ubuntu: sudo apt install build-essential cmake ninja-build xorg-dev
cmake --preset linux-release
cmake --build --preset linux-release
```

The interactive window needs the X11 development headers (GLFW is built
statically; Dear ImGui is compiled into the binary). Configure with
`-DSECGUI_BUILD_WINDOW=OFF` for a headless-only build.

### Windows (cross-compile with MinGW in Docker)

```bash
docker build -f Dockerfile.windows --target export --output type=local,dest=dist .
```

The resulting `dist/secmotion-gui.exe` is statically linked and includes the
interactive GLFW + Dear ImGui window. It must not import `wgpu_native.dll`,
`libgcc_s_seh-1.dll`, `libstdc++-6.dll` or `libwinpthread-1.dll`. System
imports such as `D3DCOMPILER_47.dll` and `bcryptprimitives.dll` mean Windows
10/11 is required. See `.opencode/skills/windows-testing/SKILL.md`.

## Usage

```bash
# interactive window (HUD, timeline, layers, telemetry, command palette)
secmotion-gui --scene examples/minimal/scene.json --tasks examples/minimal/tasks.jsonl

# record GUI/CLI actions and replay them headless
secmotion-gui --scene examples/minimal/scene.json --record-script out/actions.jsonl
secmotion-gui --headless --script out/actions.jsonl --output-dir out

# headless script with screenshots
secmotion-gui --headless --scene examples/minimal/scene.json \
  --script examples/minimal/demo.jsonl --output-dir out

# single screenshot at an exact time (task pulses included)
secmotion-gui --headless --scene examples/minimal/scene.json \
  --screenshot out/frame.png --time 1.2 --width 1280 --height 720
```

### Launcher

`scripts/run_gui.sh` wraps the AppImage (or a local build) with the default
scene arguments, so the GUI can be started with a single command:

```bash
# install once
ln -sf "$PWD/scripts/run_gui.sh" ~/.local/bin/secgui

secgui                                  # minimal example scene
secgui --naval                          # exported 760-satellite naval scene
secgui --naval --time 10 --camera vessel
secgui path/scene.json path/tasks.jsonl # explicit scene and tasks
secgui --help                           # full usage
```

The launcher prefers `SECGUI_BIN`, then the newest AppImage in `dist/`, then
`build-gui/secmotion-gui`. It falls back to `APPIMAGE_EXTRACT_AND_RUN=1` when
FUSE is unavailable. The naval scene is read from `SECGUI_NAVAL_SCENE`,
`examples/generated/naval/`, or `/tmp/opencode/naval-scene`; if none exists the
launcher prints the two core commands needed to regenerate it.

Shortcuts: `Space` play/pause, `←`/`→` step, `1`–`5` camera mode,
`G/I/D/O/K` layers, `L` layers panel, `T` telemetry, `Ctrl+K` command palette,
`F12` screenshot, `R` reset, `Ctrl+Q` quit. Drag to orbit, wheel to zoom,
click to select an entity.

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
