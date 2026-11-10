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

WINE_BIN="$(command -v wine64 || command -v wine)"
if [ -z "$WINE_BIN" ]; then
  echo "error: wine not found" >&2
  exit 1
fi

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

set -x
run_wine "$APP" --headless --backend vulkan --strict \
  --scene "$SCENE_WIN" \
  --script "$SCRIPT_WIN" \
  --output-dir "$OUT_WIN"

PNG="$OUT_DIR/minimal_frame2.png"
if [ ! -s "$PNG" ]; then
  echo "error: expected screenshot not found: $PNG" >&2
  exit 1
fi

file "$PNG"
echo "windows smoke test passed"
