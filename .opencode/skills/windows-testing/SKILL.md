---
name: Windows testing
description: Build, statically link, verify and smoke-test the SECMotionModel GUI Windows .exe from Linux using mingw-w64 in Docker, Wine, winetricks and Bottles. Use for any Windows build, distribution or crash investigation.
---

# Windows testing

The Windows artifact is a **statically linked** `secmotion-gui.exe` cross-compiled
on Linux with mingw-w64 inside `Dockerfile.windows`. It is smoke-tested with
Wine (headless, software Vulkan). Bottles is the manual fallback when Wine
cannot provide an adapter.

Repository root contains `Dockerfile.windows`, `cmake/toolchains/mingw-w64.cmake`
and `scripts/preflight_windows.sh`.

## 1. Build the .exe

```bash
docker build -f Dockerfile.windows --target export \
  --output type=local,dest=dist .
```

Output: `dist/secmotion-gui.exe`.

The build stage pins `wgpu-native v24.0.3.1` (`windows-x86_64-gnu-release`) and
configures CMake with `WEBGPU_LINK_TYPE=STATIC`, `SECGUI_STATIC=ON`.

## 2. Verify the static link

Forbidden imports: `wgpu_native.dll`, `libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`.

```bash
x86_64-w64-mingw32-objdump -p dist/secmotion-gui.exe | grep 'DLL Name' | sort -u
```

Allowed imports are Windows system DLLs only (kernel32, user32, advapi32,
ws2_32, bcrypt, ntdll, opengl32, propsys, runtimeobject, d3d12, dxgi, ...).
If a mingw/wgpu DLL appears, the build is rejected: fix the link instead of
shipping the DLL next to the exe.

Also check: `file dist/secmotion-gui.exe` must report `PE32+ executable (console) x86-64`.

## 3. Smoke test with Wine

Inside the `test` stage of the Dockerfile (or manually):

```bash
export WINEPREFIX=/wine WINEARCH=win64 WINEDEBUG=-all
wineboot -u

# Software Vulkan (lavapipe) avoids the need for a GPU under Wine
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json

xvfb-run -a wine /app/secmotion-gui.exe --headless --backend vulkan \
  --scene /app/examples/minimal/scene.json \
  --script /app/examples/minimal/demo.jsonl \
  --output-dir /app/out --strict

test -s /app/out/minimal.png
```

Success criteria:

1. exit code 0;
2. stdout contains one JSON object per command with `"ok":true`;
3. `out/*.png` exists, is a valid PNG and is not a uniform black image;
4. no `wgpu_native.dll` was needed at runtime.

If the adapter is missing:
- try `--backend vulkan` explicitly;
- check `vulkaninfo` on the host container sees lavapipe;
- `winetricks vulkan` only relinks the loader, it does not provide an ICD;
- DX12 under Wine needs vkd3d-proton; do not assume it is present.

## 4. winetricks

`winetricks` is installed in the test image but is **not** required by a static
build. Use it only when a genuine Windows redistributable is missing:

```bash
winetricks -q corefonts   # fonts, for readable UI screenshots
```

Do not install `vcrun*` "just in case": the exe links the C runtime statically.

## 5. Bottles fallback (manual)

Use Bottles only when Wine in a container cannot expose a WebGPU adapter.
Bottles is a Flatpak GUI application, not suitable for CI.

1. `flatpak install -y flathub com.usebottles.bottles`
2. Create a bottle named `secgui` (Windows 10, 64-bit, no DLL overrides).
3. Copy `secmotion-gui.exe` and `examples/` into the bottle drive.
4. In the bottle environment set `WGPU_BACKEND=vulkan` and, if needed,
   `WINEDEBUG=-all`.
5. Run:
   `secmotion-gui.exe --headless --backend vulkan --scene ... --script ...`
6. Collect: exit code, stdout JSON, generated PNG, Bottles logs
   (Bottle → Logs) and a screenshot of the Bottles window.
7. Upload the evidence with the `gh-issues` skill and keep the Windows phase
   issue open until a maintainer confirms the manual run.

## 6. Common failures

| Symptom | Likely cause | Action |
|---|---|---|
| `wine: Bad EXE format` | 32-bit Wine | use 64-bit Wine / `WINEARCH=win64` |
| `Could not get WebGPU adapter` | no Vulkan ICD visible to Wine | install `mesa-vulkan-drivers`, set `VK_ICD_FILENAMES`, force `--backend vulkan` |
| `wgpu_native.dll not found` | dynamic link slipped in | check `WEBGPU_LINK_TYPE=STATIC`, rerun import check |
| screenshot is black | readback/padding issue | verify 256-byte row alignment and `RGBA8Unorm` target |
| crash at startup | missing system import | inspect `objdump -p`, report with `WINEDEBUG=+loaddll` for one run |

Report failures with the `gh-issues` skill: exact command, image tag, Wine
version, adapter line from the startup JSON, and the sanitized log.
