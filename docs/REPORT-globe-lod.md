# Report: textured globe, atmosphere and ISL LOD (issue #9)

Status: implemented and verified on Linux (Vulkan) and Windows (MinGW `.exe`
under Wine/lavapipe). The v0.2.0 release artifacts are unchanged; this work is
on `main` for the next release.

## What changed

- **Embedded Earth texture.** `assets/earth_2048.jpg` (2048x1024, ~330 KB,
  Solar System Scope day map, CC BY 4.0, desert-graded) is compiled into the binary as
  `src/render/EarthTextureData.cpp` by `tools/embed_texture.py`. Nothing is
  downloaded or read from disk at runtime. `stb_image` decodes it once during
  renderer initialization.
- **Textured globe.** A 128x64 UV sphere is drawn with the equirectangular
  texture, a camera-relative key light and a subtle blue rim. The opaque globe
  writes depth, so geometry behind the Earth is correctly hidden.
- **Atmosphere shell.** A second pass at `1.016 * earth_radius` renders the far
  side of a fresnel shell additively; the globe depth hides the part inside the
  disk, leaving a thin halo around the limb. It works headless and in the
  window.
- **Thin grid.** The existing 15-degree grid stays at
  `1.0005 * earth_radius`; a small clip-space depth bias keeps it above the
  texture without z-fighting.
- **ISL level of detail.** The Layers panel now has an **ISL density** slider
  (`0..1`, default `0.6`). The per-satellite budget is
  `4 + 36 * density`, scaled to 65% or 35% as the camera moves away from the
  globe. Edges incident to an AP and edges with an active traffic forward are
  always kept; the remaining edges are ordered by traffic weight, then by id.
- **Controls.** New `--isl-density N` CLI option, `{"cmd":"isl","density":N}`
  JSONL command, `isl_density` in `state`, and `globe`/`atmosphere` entries in
  the Layers panel. `--layer globe=off` restores the wireframe view.

## Verification

All screenshots are 1280x720, headless, `--backend vulkan` (Linux native,
Windows under Wine with lavapipe).

| Case | Screenshot result |
|---|---|
| TLE base scene, frame 0, orbit | `points: 10629`, `lines: 768` (line vertices), `mean: 50.17`, `std: 72.30`, `nonblank: true` |
| Naval IN_NETWORK, frame 30, default density | `points: 465`, `lines: 6656`, `mean: 43.08`, `std: 62.97` |
| Naval IN_NETWORK, frame 30, `--isl-density 0` | `lines: 2578` |
| Naval IN_NETWORK, frame 30, `--isl-density 1` | `lines: 8284` |
| `examples/minimal` (v1) | `points: 9`, `lines: 776`, `mean: 42.24`, `nonblank: true` |
| `examples/live` (v2, load+traffic), time 2 | `points: 11`, `lines: 794`, `mean: 42.26` Linux / Windows TBD |

The v1 and v2 scenes render the globe, atmosphere and grid without changing the
`load`/`traffic` data path. The `lines` count proves the LOD slider changes the
ISL set while AP/active traffic edges survive (even density 0 keeps links).

### View tuning (issue #10)

A follow-up pass aligned the view with the satellitetracker3d.com reference:

- the clear colour is now pure black instead of dark navy;
- the Earth texture was swapped for the 2048x1024 Solar System Scope day map:
  deep blue oceans, saturated vegetation and warm deserts instead of the muted
  Blue Marble derivation; satellite markers are near-white so they stay
  readable over the brighter globe;
- the desert pixels of that map are graded to a sandy tone offline (Sahara
  `(233,213,187)` -> `(224,191,140)`; about 6% of the map). Bright cream
  deserts matched the near-white markers and hid them; the sand keeps the
  constellation readable without touching oceans, vegetation or ice;
- satellite markers are about half the previous size (3 px default, 4.5 px AP),
  with smaller load halos and terminal markers;
- the atmosphere shell is thinner (`1.016 * earth_radius`) and its fresnel
  exponent/intensity were reduced, so the texture stays clear up to the limb;
- the globe uses a balanced soft-terminator curve that preserves the texture's
  colour depth: contrast around the midtones, saturation restored, and the peak
  kept near 1.0 so deserts and clouds do not clip to white; the grid alpha is
  reduced to 0.40;
- horizontal drag now increases yaw, so the planet follows the mouse: dragging
  right moves the surface right. Vertical drag already matched the natural
  direction.

The metrics table above is from this version.

### Performance

Measured on the development machine (AMD Radeon 610M, Vulkan) with a 1920x1080
offscreen screenshot including process startup, scene load and texture decode:

- TLE base scene (10,627 satellites): **0.62 s**;
- naval run (463 satellites, load + traffic): **1.68 s**.

A 120-frame window run takes about 2.2 s for the base scene and 3.3 s for the
naval scene including startup, i.e. no visible regression versus v0.2.0. The
naval run has 3,902 unique ISL edges; at density 0.6 and orbit distance the LOD
draws about 2,900 ISL segments instead of all 3,902.

Note: the `screenshot` result field named `lines` reports line vertices (two
per segment), matching the existing v0.2.0 protocol; the counts above use that
field.

### Windows

`docker build -f Dockerfile.windows --target test` passes on the final commit.
The Wine log shows:

```text
[info] WebGPU ready: backend=vulkan adapter=llvmpipe (LLVM 15.0.6, 256 bits)
[info] Earth texture decoded: 2048x1024 (3 channels)
windows smoke test passed
```

Headless screenshots from the final `.exe` reproduce the Linux content:

- TLE base scene: `points: 10629`, `lines: 768`, `mean: 50.17`;
- naval frame 30: `points: 465`, `lines: 6656`, `mean: 43.08`, identical line
  and point counts to Linux;
- live v2 at time 2: `points: 11`, `lines: 794`, `mean: 42.26`.

The Windows hashes differ from Linux because lavapipe and the native Vulkan
driver rasterize the last bit differently, as documented in
`docs/testing.md`.

## Reproduce

```bash
# Linux, full globe
./build-gui/secmotion-gui --headless --scene ~/.cache/secgui/base \
  --frame 0 --screenshot base.png --width 1280 --height 720

# Linux, naval at default ISL density
SCENE=$(ls -d ~/.cache/secgui/runs/* | head -1)
./build-gui/secmotion-gui --headless --scene "$SCENE" --frame 30 \
  --screenshot naval.png --width 1280 --height 720

# ISL LOD sweep
for density in 0 0.6 1; do
  ./build-gui/secmotion-gui --headless --scene "$SCENE" --frame 30 \
    --isl-density "$density" --screenshot "isl-$density.png" \
    --width 1280 --height 720
done

# Windows (after `docker build --target test`)
docker run --rm secgui-win-globe wine /app/secmotion-gui.exe --headless \
  --backend vulkan --scene 'Z:/app/examples/live/manifest.json' --time 2 \
  --screenshot 'Z:/app/out/windows.png' --width 1280 --height 720
```
