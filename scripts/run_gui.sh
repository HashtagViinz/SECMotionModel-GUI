#!/usr/bin/env bash
#
# Convenience launcher for the SECMotionModel GUI.
#
#   secgui                  minimal scene
#   secgui --naval          exported naval scene (760 satellites), if present
#   secgui path/scene.json  explicit scene, optional tasks as second argument
#   secgui --backend vulkan extra flags are forwarded to the application
#
# Environment overrides:
#   SECGUI_BIN            explicit executable
#   SECGUI_NAVAL_SCENE    directory containing manifest.json/tasks.jsonl
#
set -euo pipefail

SOURCE="${BASH_SOURCE[0]}"
while [ -L "$SOURCE" ]; do
	DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
	SOURCE="$(readlink "$SOURCE")"
	[[ "$SOURCE" != /* ]] && SOURCE="$DIR/$SOURCE"
done
REPO="$(cd -P "$(dirname "$SOURCE")/.." && pwd)"

usage() {
	cat <<'EOF'
Usage: secgui [options] [scene] [tasks]

With no arguments, opens the minimal example scene.

  --minimal          use examples/minimal (default)
  --naval            use the exported 760-satellite naval scene
  -h, --help         show this help

Any other argument is forwarded to secmotion-gui. A bare first argument is
treated as the scene path; a bare second argument as the tasks path.

Examples:
  secgui
  secgui --naval
  secgui examples/minimal/scene.json
  secgui --scene /path/manifest.json --tasks /path/tasks.jsonl --camera vessel
  secgui --naval --time 10 --camera vessel --target V-ATLANTIC

Environment:
  SECGUI_BIN            explicit executable to run
  SECGUI_NAVAL_SCENE    naval scene directory (default: /tmp/opencode/naval-scene)
EOF
}

find_naval_scene() {
	local candidates=(
		"${SECGUI_NAVAL_SCENE:-}"
		"$REPO/examples/generated/naval"
		"/tmp/opencode/naval-scene"
	)
	local candidate
	for candidate in "${candidates[@]}"; do
		if [ -n "$candidate" ] && [ -f "$candidate/manifest.json" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

find_binary() {
	if [ -n "${SECGUI_BIN:-}" ] && [ -x "$SECGUI_BIN" ]; then
		printf '%s\n' "$SECGUI_BIN"
		return 0
	fi
	local appimage
	appimage="$(find "$REPO/dist" -maxdepth 1 -type f -name 'SECMotionModel-GUI-*-x86_64.AppImage' -printf '%T@ %p\n' 2>/dev/null \
		| sort -n | tail -n 1 | cut -d' ' -f2-)"
	if [ -n "$appimage" ] && [ -x "$appimage" ]; then
		printf '%s\n' "$appimage"
		return 0
	fi
	if [ -x "$REPO/build-gui/secmotion-gui" ]; then
		printf '%s\n' "$REPO/build-gui/secmotion-gui"
		return 0
	fi
	return 1
}

mode="minimal"
filtered=()
for arg in "$@"; do
	case "$arg" in
		--minimal) mode="minimal" ;;
		--naval) mode="naval" ;;
		-h|--help) usage; exit 0 ;;
		*) filtered+=("$arg") ;;
	esac
done

has_scene=false
for arg in "${filtered[@]}"; do
	if [ "$arg" = "--scene" ]; then
		has_scene=true
		break
	fi
done

if ! $has_scene; then
	if [ ${#filtered[@]} -gt 0 ] && [[ "${filtered[0]}" != -* ]]; then
		scene="${filtered[0]}"
		if [ ${#filtered[@]} -gt 1 ] && [[ "${filtered[1]}" != -* ]]; then
			tasks="${filtered[1]}"
			rest=("${filtered[@]:2}")
		else
			tasks=""
			rest=("${filtered[@]:1}")
		fi
		scene_args=(--scene "$scene")
		if [ -n "$tasks" ]; then
			scene_args+=(--tasks "$tasks")
		fi
		filtered=("${scene_args[@]}" "${rest[@]}")
	elif [ "$mode" = "naval" ]; then
		if ! naval="$(find_naval_scene)"; then
			echo "error: naval scene not found." >&2
			echo "Regenerate it from the core repository:" >&2
			echo "  cd /home/vinz/SEC/SECMotionModel" >&2
			echo "  python3 tools/parse_naval_log.py --log result/naval_full_run_2026-11-10/naval_full_in_network.log --out /tmp/naval-tasks.jsonl" >&2
			echo "  python3 tools/export_gui_scene.py --config data/naval_configurations_AP_optimal.json --core-config config/config.json5 --trace /tmp/naval-tasks.jsonl --out /tmp/opencode/naval-scene --start 0 --end 20" >&2
			echo "Or set SECGUI_NAVAL_SCENE to a directory containing manifest.json." >&2
			exit 1
		fi
		filtered=(--scene "$naval/manifest.json" --tasks "$naval/tasks.jsonl" --camera vessel --target V-ATLANTIC "${filtered[@]}")
	else
		filtered=(--scene "$REPO/examples/minimal/scene.json" --tasks "$REPO/examples/minimal/tasks.jsonl" "${filtered[@]}")
	fi
fi

binary="$(find_binary)" || {
	echo "error: no GUI binary found." >&2
	echo "Download the release AppImage into $REPO/dist or build with:" >&2
	echo "  cmake --preset linux-release && cmake --build --preset linux-release" >&2
	exit 1
}

case "$binary" in
	*.AppImage)
		# Native FUSE first; fall back to extraction when FUSE is unavailable
		# or the mount is blocked (common in containers).
		if "$binary" "${filtered[@]}"; then
			exit 0
		fi
		status=$?
		echo "AppImage direct launch failed (exit $status); retrying with extraction..." >&2
		APPIMAGE_EXTRACT_AND_RUN=1 "$binary" "${filtered[@]}"
		;;
	*)
		exec "$binary" "${filtered[@]}"
		;;
esac
