# Third-party components

SECMotionModel GUI is distributed under GPL-3.0-or-later. The following
third-party components are used:

| Component | Version / pin | License |
|---|---|---|
| [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp) | via WebGPU-distribution pin | MIT |
| [WebGPU-distribution](https://github.com/eliemichel/WebGPU-distribution) | commit `17dcd42a7683355e7a40ac4e97e77f36dff5b5ab` | MIT |
| [wgpu-native](https://github.com/gfx-rs/wgpu-native) | `v24.0.3.1` (prebuilt static) | MIT OR Apache-2.0 |
| [GLFW](https://github.com/glfw/glfw) | tag `3.4` | Zlib |
| [glfw3webgpu](https://github.com/eliemichel/glfw3webgpu) | commit `8f14534e79c590e8a7f7795ea42e01bedea9e137` | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | tag `v3.11.3` | MIT |
| [stb](https://github.com/nothings/stb) | commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897` | MIT / public domain |
| Earth texture `assets/earth_1024.jpg` | derived from the three.js `earth_atmos_2048.jpg` example (NASA Blue Marble imagery) | public domain (NASA) |

Build-time only, not linked into the final binaries:

| Component | Version | License |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | pinned tag, added in Phase 3 | MIT |
| [glm](https://github.com/g-truc/glm) | pinned tag, added in Phase 3 | MIT |
