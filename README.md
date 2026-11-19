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
assets/      embedded assets (Earth texture, 2048x1024)
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
# open an exported simulation
secmotion-gui --scene path/simulation.secsim

# one deterministic screenshot
secmotion-gui --headless --scene path/simulation.secsim \
  --screenshot out/frame.png --frame 0
```

The full JSONL protocol, recording and replay are documented in
[docs/commands.md](docs/commands.md).

### Launcher

```bash
secgui                              # cached TLE base scene
secgui path/simulation.secsim       # exported simulation
secgui --empty                      # empty workspace with Import panel
```

Create the simulation files in the core repository:

```bash
python main.py --gui-export
```

This writes `<result>/gui_scene/simulation.secsim` together with
`manifest.json`, `frames/` and the optional `tasks.jsonl`, `loads.jsonl` and
`traffic.jsonl`. The core README explains each file.

Open the GUI, choose **Scenarios... -> Import...** and select
`simulation.secsim`. There is no folder to choose: the descriptor contains the
paths of the scene files. You can also pass it directly:

```bash
secgui path/to/simulation.secsim
```

The GUI is independent of the core at runtime: it never runs Python or searches
for a core checkout. For one-step export + launch use the core
`tools/run_gui.sh` wrapper; the GUI launcher rejects `--config`/`--run` on
purpose.

With no arguments `secgui` opens the cached TLE base scene. If it is missing,
generate it once from the core with `tools/export_base_scene.py`; the GUI prints
the exact command. **Scenarios...** lists cached exported scenes and
**Import...** opens the file browser.

Shortcuts: `Space` play/pause, `←`/`→` step, `1`–`5` camera mode,
`G/I/D/O/K` layers, `L` layers panel, `T` telemetry, `Ctrl+K` command palette,
`F12` screenshot, `R` reset, `Ctrl+Q` quit. Drag to orbit, two-finger trackpad
scroll (or wheel) to zoom, `Shift` for fine zoom, `+`/`-` as keyboard zoom,
click to select an entity.

## Rendering

The globe is a UV sphere textured with a vivid equirectangular Earth map. The
2048x1024 JPEG is embedded in the binary (`assets/earth_2048.jpg`, generated
into `src/render/EarthTextureData.cpp` by `tools/embed_texture.py`), so the
AppImage and the Windows `.exe` need no external asset and never download
anything at runtime. `RGBA8Unorm`, linear filtering and a repeat/clamp sampler
keep the seam and poles clean. The texture is the Solar System Scope Earth day
map (CC BY 4.0), credited in `THIRD_PARTY.md`. Desert areas are graded to a
sandy tone so the near-white satellite markers stay readable over land.

- **globe**: opaque textured sphere with a balanced, saturation-preserving
  tone curve, a soft terminator and a small blue rim. Can be disabled with
  `--layer globe=off`, which restores the wireframe look.
- **atmosphere**: a slightly larger additive shell (`earth_radius * 1.016`)
  whose back faces produce a thin fresnel halo. Toggle with
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

## Relationship with the core

The GUI is a submodule but has no runtime dependency on the simulator. The core
exporter writes `simulation.secsim`; the GUI reads that descriptor and the files
it references. See the core README for `python main.py --gui-export` and the file
layout. Positions are exported in ECEF/ITRS, so the globe is earth-fixed.

## License

GPL-3.0-or-later. Third-party components are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
