#!/usr/bin/env bash
#
# Headless smoke test for the Windows .exe under Wine.
# Used by the `test` stage of Dockerfile.windows.
#
set -euo pipefail

APP="${1:-/app/secmotion-gui.exe}"
SCENE="${SCENE:-/app/examples/minimal/scene.json}"
SCRIPT="${SCRIPT:-/app/examples/minimal/demo.jsonl}"
OUT_DIR="${OUT_DIR:-/app/out}"

export WINEPREFIX="${WINEPREFIX:-/wine}"
export WINEARCH="${WINEARCH:-win64}"
export WINEDEBUG="${WINEDEBUG:--all}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.x86_64.json}"

mkdir -p "$OUT_DIR"

WINE_BIN="$(command -v wine64 2>/dev/null || true)"
if [ -z "$WINE_BIN" ]; then
  WINE_BIN="$(command -v wine 2>/dev/null || true)"
fi
if [ -z "$WINE_BIN" ] && [ -x /opt/wine-stable/bin/wine ]; then
  WINE_BIN=/opt/wine-stable/bin/wine
fi
if [ -z "$WINE_BIN" ]; then
  echo "error: wine not found; candidates:" >&2
  ls -la /usr/bin/wine* /usr/lib/wine* /opt/wine-stable/bin 2>&1 >&2 || true
  exit 1
fi
echo "wine binary: $WINE_BIN"
echo "winepath: $(command -v winepath 2>/dev/null || echo none)"
echo "xvfb-run: $(command -v xvfb-run 2>/dev/null || echo none)"

run_wine() {
  if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "$WINE_BIN" "$@"
  else
    "$WINE_BIN" "$@"
  fi
}

echo "wine: $("$WINE_BIN" --version 2>/dev/null || echo unknown)"
run_wine wineboot -u >/dev/null 2>&1 || true

# Convert Unix paths to Windows paths when winepath is available, so the
# application sees valid paths regardless of the current drive mapping.
to_windows_path() {
  if command -v winepath >/dev/null 2>&1; then
    winepath -w "$1"
  else
    printf 'Z:%s\n' "$1"
  fi
}

SCENE_WIN="$(to_windows_path "$SCENE")"
SCRIPT_WIN="$(to_windows_path "$SCRIPT")"
OUT_WIN="$(to_windows_path "$OUT_DIR")"

echo "--- wine dll diagnostics"
find /usr -iname '*d3dcompiler*' 2>/dev/null | head -20 || true
if ! find /usr -iname '*d3dcompiler_47*' 2>/dev/null | grep -q .; then
  echo "d3dcompiler_47 not found in the Wine prefix, trying winetricks"
  winetricks -q d3dcompiler_47 >/tmp/winetricks.log 2>&1 || true
  tail -5 /tmp/winetricks.log || true
fi

set -x
set +e
export WINEDEBUG="${APP_WINEDEBUG:-err+module}"
run_wine "$APP" --headless --backend vulkan --strict \
  --scene "$SCENE_WIN" \
  --script "$SCRIPT_WIN" \
  --output-dir "$OUT_WIN" > /tmp/wine-app.log 2>&1
WINE_STATUS=$?
set -e
cat /tmp/wine-app.log
echo "wine application status: $WINE_STATUS"
grep -iE "d3dcompiler|err:module|not found|adapter" /tmp/wine-app.log | head -20 || true
if [ "$WINE_STATUS" -ne 0 ]; then
  exit "$WINE_STATUS"
fi

PNG="$OUT_DIR/minimal_frame2.png"
if [ ! -s "$PNG" ]; then
  echo "error: expected screenshot not found: $PNG" >&2
  exit 1
fi

file "$PNG"
echo "windows smoke test passed"
