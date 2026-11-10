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
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"
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

# Use a single Xvfb for the whole script. Starting a second `xvfb-run` right
# after the headless run is racy under Wine: the new server may not be ready
# and Wine reports "no driver could be loaded".
XVFB_PID=""
if [ -z "${DISPLAY:-}" ]; then
  DISPLAY_NUMBER=99
  while [ -e "/tmp/.X${DISPLAY_NUMBER}-lock" ]; do
    DISPLAY_NUMBER=$((DISPLAY_NUMBER + 1))
  done
  Xvfb ":${DISPLAY_NUMBER}" -screen 0 1280x720x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
  XVFB_PID=$!
  export DISPLAY=":${DISPLAY_NUMBER}"
  sleep 1
fi
cleanup() {
  if [ -n "$XVFB_PID" ]; then
    kill "$XVFB_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT
echo "display: $DISPLAY"

run_wine() {
  timeout "${WINE_TIMEOUT:-300}" "$WINE_BIN" "$@"
}

echo "wine: $("$WINE_BIN" --version 2>/dev/null || echo unknown)"
# Do not run `wineboot -u` explicitly: Wine 11 initializes the prefix on the
# first app launch. The explicit wineboot path was observed to hang under
# Xvfb with WINEDLLOVERRIDES, while the automatic path is reliable.

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
# The Vulkan backend compiles WGSL through naga and does not need
# d3dcompiler_47; do not install redistributables in CI.

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

echo "--- windowed smoke test (Xvfb + Wine Win32 surface)"
set +e
run_wine "$APP" --backend vulkan --strict \
  --scene "$SCENE_WIN" \
  --window-frames 60 > /tmp/wine-window.log 2>&1
WINDOW_STATUS=$?
set -e
cat /tmp/wine-window.log
if [ "$WINDOW_STATUS" -ne 0 ]; then
  echo "error: windowed smoke test failed with status $WINDOW_STATUS" >&2
  exit "$WINDOW_STATUS"
fi
echo "windows smoke test passed"
