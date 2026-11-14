# SECMotionModel GUI

Native C++20/WebGPU visualizer for the [SECMotionModel](https://github.com/casalicchio/SECMotionModel) satellite edge-computing simulator.

The GUI renders LEO satellite motion, inter-satellite links, visibility domes and the way tasks travel between satellites. It is designed to be:

- **interactive**, with real-time GPU rendering (wgpu-native via [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp));
- **scriptable**, with a JSONL command protocol where every command has a GUI equivalent;
- **testable by agents**, thanks to deterministic offscreen rendering, PNG screenshots and JSON results on stdout;
- **portable**, with static Windows `.exe` and Linux `.AppImage` builds.

> Status: v0.2.0 released for Linux and Windows. Includes the interactive
> window, timeline, layers, telemetry, command palette and recorder, plus live
> per-satellite load and link traffic from real simulation runs. The current
> `main` adds the textured Earth, atmosphere shell and ISL level of detail
> (issue #9). Windows is validated under Wine in CI; macOS is out of scope.

## Layout

```text
src/         C++ sources (app, cli, scene, render, screenshot, ui, util)
shaders/     WGSL shaders (embedded in src/render/Renderer.cpp)
assets/      embedded assets (Earth texture, 1024x512)
tools/       asset embedding helpers
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

secgui                                  # TLE base scene (all satellites)
secgui --empty                          # empty workspace with Import panel
secgui --minimal                        # minimal example scene
secgui --naval                          # latest naval run (load + traffic)
secgui --import result/naval/IN_NETWORK/optimal
secgui --naval --time 10 --camera vessel
secgui --config data/naval_configurations_AP_optimal.json
secgui --config data/naval_configurations_AP_optimal.json \
  --trace /tmp/opencode/naval-tasks.jsonl
secgui --run result/naval/IN_NETWORK/optimal
secgui path/scene.json path/tasks.jsonl # explicit scene and tasks
secgui --help                           # full usage
```

`--config` runs `tools/export_gui_scene.py` on a core configurations JSON and
opens the result directly; the first run takes about 20 s and is cached under
`~/.cache/secgui`, later runs reuse it. Add `--trace` to include task pulses.
The core configuration already contains the per-step TLEs, access points,
elevation, range and neighbors; the exporter propagates the TLEs to ECEF and
writes the GUI frames. The GUI itself does no orbital mechanics.

`--naval` opens the latest exported run automatically, preferring
`result/naval/IN_NETWORK/optimal` and falling back to
`result/naval/TO_OBSERVER/optimal`. Set `SECGUI_NAVAL_RUN` to choose another
run, or `SECGUI_NAVAL_SCENE` to open a legacy pre-exported scene instead.

With no arguments `secgui` opens the **TLE base scene**: every satellite in
the core `data/tle_data.txt` catalog around the globe. The launcher generates
it with `tools/export_base_scene.py` on first use (about 30 s, 58 MB) and caches
it under `~/.cache/secgui/base`; later launches are immediate. Override the
location with `SECGUI_BASE_SCENE`.

Inside the window, **Scenarios...** lists every known simulation with an
**Open** button: the TLE base scene, the naval runs under
`result/naval/*/*`, and previously exported scenes under
`~/.cache/secgui/runs`. Raw runs are exported on first open; **Refresh**
rescans the directories and **Import directory...** opens the free-form
browser.

`secgui --empty` (or `secgui --import DIR`) uses the **Import scene** panel
instead. The panel browses directories, recognizes a scene
(`manifest.json`/`scene.json`) or a simulation run (`gui_run.json`), and loads
it. A raw run is exported to `/tmp/secgui-imports` with the core Python
exporter before loading; the core is found through `SECGUI_CORE` or a sibling
`SECMotionModel` checkout.

`--run` exports a simulation directory containing `gui_run.json`,
`gui_state.csv` and `gui_links.csv`. It adds the real per-satellite load
(`loads.jsonl`) and the real forward events (`traffic.jsonl`) to the scene.
Satellites that are no longer in a frame are dropped from the load data. See
[docs/REPORT-live-traffic.md](docs/REPORT-live-traffic.md) for the v0.2
telemetry report and evidence.

The launcher prefers `SECGUI_BIN`, then the newest AppImage in `dist/`, then
`build-gui/secmotion-gui`. It falls back to `APPIMAGE_EXTRACT_AND_RUN=1` when
FUSE is unavailable. `--naval` reads `SECGUI_NAVAL_RUN` or, by default,
`result/naval/IN_NETWORK/optimal` (then `TO_OBSERVER`); `SECGUI_NAVAL_SCENE`
overrides it with a legacy pre-exported scene. If none exists the launcher
prints the core command needed to create a run.

Shortcuts: `Space` play/pause, `←`/`→` step, `1`–`5` camera mode,
`G/I/D/O/K` layers, `L` layers panel, `T` telemetry, `Ctrl+K` command palette,
`F12` screenshot, `R` reset, `Ctrl+Q` quit. Drag to orbit, two-finger trackpad
scroll (or wheel) to zoom, `Shift` for fine zoom, `+`/`-` as keyboard zoom,
click to select an entity.

See [docs/commands.md](docs/commands.md) for the full JSONL protocol and the
GUI mapping, and [examples/README.example.md](examples/README.example.md) for a
guided example.

## Rendering

The globe is a UV sphere textured with an equirectangular Earth map. The
1024x512 JPEG is embedded in the binary (`assets/earth_1024.jpg`, generated into
`src/render/EarthTextureData.cpp` by `tools/embed_texture.py`), so the AppImage
and the Windows `.exe` need no external asset and never download anything at
runtime. `RGBA8Unorm`, linear filtering and a repeat/clamp sampler keep the
seam and poles clean.

- **globe**: opaque textured sphere with a key light kept towards the camera's
  upper-right, plus a small blue rim. Can be disabled with
  `--layer globe=off`, which restores the wireframe look.
- **atmosphere**: a slightly larger additive shell (`earth_radius * 1.028`)
  whose back faces produce a fresnel halo. Toggle with
  `--layer atmosphere=off`.
- **grid**: the 15-degree geographic grid is drawn at `1.0005 * earth_radius`
  with a small depth bias, so it sits above the texture without z-fighting and
  is occluded correctly behind the horizon.

The opaque globe now writes depth, so ISL links, satellites and task pulses
behind the Earth are hidden. Surface overlays (route, dome, grid) remain visible
through the depth bias.

### ISL level of detail

The naval constellation has around 4,000 unique ISL edges. Drawing all of them
at every zoom is noisy and wasteful. The renderer now keeps a per-satellite
budget derived from the **ISL density** slider (Layers panel) and the camera
distance:

- near the surface the budget is about `4 + 36 * density` links per satellite;
- far away the budget is reduced to 65% (mid) or 35% (whole globe in view);
- edges incident to an access point and edges carrying an active traffic
  forward are **always** drawn, regardless of the budget;
- traffic-weighted edges are kept first, then the remaining edges are chosen by
  satellite id for stable output.

The default density is `0.6`. Set it from the command line with
`--isl-density 0..1`, or at runtime with:

```jsonl
{"cmd":"isl","density":0.35}
```

The current value is reported as `isl_density` in `state` and recorded by
`--record-script`.

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
