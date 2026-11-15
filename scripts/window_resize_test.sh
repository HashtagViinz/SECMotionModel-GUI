#!/usr/bin/env bash
#
# Regression test for window resizing (Linux/X11).
#
# Starts the GUI, resizes its window several times while frames are in flight,
# and fails if WebGPU reports a validation error or the process aborts. This
# covers the swapchain/ImGui viewport race fixed for issue #9.
#
#   scripts/window_resize_test.sh [binary] [scene]
#
# Without DISPLAY the script re-executes itself under xvfb-run.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -z "${DISPLAY:-}" ] && [ -z "${SECGUI_RESIZE_TEST_INNER:-}" ]; then
  SECGUI_RESIZE_TEST_INNER=1 exec xvfb-run -a -s "-screen 0 1280x720x24" \
    "$0" "$@"
fi

BIN="${1:-}"
SCENE="${2:-}"
if [ -z "$SCENE" ]; then
  BASE="${SECGUI_BASE_SCENE:-${SECGUI_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/secgui}/base}"
  if [ -f "$BASE/manifest.json" ]; then
    SCENE="$BASE/manifest.json"
  else
    SCENE="$ROOT/examples/minimal/scene.json"
  fi
fi
if [ -z "$BIN" ]; then
  for candidate in "$ROOT/build-gui/secmotion-gui" "$ROOT/build-linux/secmotion-gui"; do
    if [ -x "$candidate" ]; then
      BIN="$candidate"
      break
    fi
  done
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "error: secmotion-gui binary not found" >&2
  exit 2
fi
if [ ! -f "$SCENE" ]; then
  echo "error: scene not found: $SCENE" >&2
  exit 2
fi

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

"$BIN" --backend vulkan --strict --scene "$SCENE" \
  --window-frames 360 --log-level debug >"$LOG" 2>&1 &
APP_PID=$!

python3 "$ROOT/tests/e2e/resize_window.py" "${DISPLAY:-:0}" "$APP_PID" \
  1200 680 --repeat 3 --delay 0.35

wait "$APP_PID"
STATUS=$?

if grep -Eq "uncaptured error|Validation Error|panicked|fatal runtime error" "$LOG"; then
  echo "error: WebGPU/ImGui resize failure" >&2
  cat "$LOG" >&2
  exit 1
fi
if [ "$STATUS" -ne 0 ]; then
  echo "error: GUI exited with status $STATUS" >&2
  cat "$LOG" >&2
  exit "$STATUS"
fi

RECONFIGURES="$(grep -c "reconfiguring" "$LOG" || true)"
echo "resize smoke test passed (viewport reconfigures: ${RECONFIGURES:-0})"
