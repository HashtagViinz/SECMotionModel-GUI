#!/usr/bin/env bash
#
# Standalone launcher for the SECMotionModel GUI.
#
#   secgui                  cached TLE base scene
#   secgui --empty          empty workspace with the Import panel
#   secgui --minimal        minimal example scene
#   secgui --naval          pre-exported naval scene, if available
#   secgui path.secsim      exported simulation file
#   secgui path/scene.json  explicit scene manifest
#
# The GUI reads only exported scenes. It never runs Python and never searches
# for the core repository. Use SECMotionModel/tools/run_gui.sh when you want
# the launcher to export a configuration or a simulation run first.
#
# Environment overrides:
#   SECGUI_BIN            explicit executable
#   SECGUI_BASE_SCENE     cached base scene directory
#   SECGUI_NAVAL_SCENE    exported naval scene directory/file
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

The GUI reads exported scenes only. It never runs Python or needs the core
repository. Export simulations with SECMotionModel/tools/export_gui_scene.py
or with main.py --gui-export.

  --base             cached TLE base scene (default)
  --empty            empty workspace with the Import panel
  --minimal          open examples/minimal
  --naval            open the pre-exported naval scene, if available
  --import <path>    open a .secsim/scene path in the Import panel
  -h, --help         show this help

Any other argument is forwarded to secmotion-gui. A bare first argument is
treated as the scene path; a bare second argument as the tasks path.

Examples:
  secgui
  secgui --empty
  secgui --minimal
  secgui /path/to/simulation.secsim
  secgui --scene /path/to/manifest.json --camera vessel
  secgui --naval --time 10 --camera vessel --target V-ATLANTIC

Environment:
  SECGUI_BIN            explicit executable to run
  SECGUI_BASE_SCENE     TLE base scene directory (default: <cache>/base)
  SECGUI_NAVAL_SCENE    exported naval scene directory or .secsim file
  SECGUI_CACHE_DIR      scene cache (default: ~/.cache/secgui)
EOF
}

cache_root() {
	printf '%s\n' "${SECGUI_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/secgui}"
}

base_scene_dir() {
	printf '%s\n' "${SECGUI_BASE_SCENE:-$(cache_root)/base}"
}

scene_entry() {
	local directory="$1"
	if [ -f "$directory/simulation.secsim" ]; then
		printf '%s\n' "$directory/simulation.secsim"
	else
		printf '%s\n' "$directory/manifest.json"
	fi
}

find_base_scene() {
	local directory
	directory="$(base_scene_dir)"
	if [ -f "$directory/simulation.secsim" ] || [ -f "$directory/manifest.json" ]; then
		printf '%s\n' "$directory"
		return 0
	fi
	return 1
}

find_naval_scene() {
	local candidate
	local candidates=(
		"${SECGUI_NAVAL_SCENE:-}"
		"$(cache_root)/scenes/naval"
		"$REPO/examples/generated/naval"
	)
	for candidate in "${candidates[@]}"; do
		[ -n "$candidate" ] || continue
		if [ -f "$candidate/simulation.secsim" ]; then
			printf '%s\n' "$candidate/simulation.secsim"
			return 0
		fi
		if [ -f "$candidate/manifest.json" ]; then
			printf '%s\n' "$candidate/manifest.json"
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
	local build="$REPO/build-gui/secmotion-gui"
	if [ -x "$build" ] && { [ -z "$appimage" ] || [ "$build" -nt "$appimage" ]; }; then
		printf '%s\n' "$build"
		return 0
	fi
	if [ -n "$appimage" ] && [ -x "$appimage" ]; then
		printf '%s\n' "$appimage"
		return 0
	fi
	return 1
}

mode="base"
filtered=()
while [ $# -gt 0 ]; do
	case "$1" in
		--base) mode="base"; shift ;;
		--empty) mode="empty"; shift ;;
		--minimal) mode="minimal"; shift ;;
		--naval) mode="naval"; shift ;;
		--config|--trace|--run)
			echo "error: $1 belongs to the core export wrapper; use SECMotionModel/tools/run_gui.sh" >&2
			exit 2
			;;
		-h|--help) usage; exit 0 ;;
		*) filtered+=("$1"); shift ;;
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
			echo "error: no exported naval scene found." >&2
			echo "Export one from the core repository first:" >&2
			echo "  SECMotionModel/tools/run_gui.sh --naval" >&2
			echo "or set SECGUI_NAVAL_SCENE to an exported scene." >&2
			exit 1
		fi
		filtered=(--scene "$naval" --camera vessel --target V-ATLANTIC "${filtered[@]}")
	elif [ "$mode" = "minimal" ]; then
		filtered=(--scene "$REPO/examples/minimal/simulation.secsim" "${filtered[@]}")
	elif [ "$mode" = "base" ]; then
		if base="$(find_base_scene)"; then
			filtered=(--scene "$(scene_entry "$base")" --camera orbit \
				--layer isl=off --layer dome=off --layer route=off "${filtered[@]}")
		else
			echo "error: TLE base scene not found at $(base_scene_dir)." >&2
			echo "Generate it once from the core repository:" >&2
			echo "  python3 tools/export_base_scene.py --tle data/tle_data.txt \\" >&2
			echo "    --out \"$(base_scene_dir)\" --interval 5 --duration 300" >&2
			exit 1
		fi
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
		else
			status=$?
		fi
		echo "AppImage direct launch failed (exit $status); retrying with extraction..." >&2
		APPIMAGE_EXTRACT_AND_RUN=1 "$binary" "${filtered[@]}"
		;;
	*)
		exec "$binary" "${filtered[@]}"
		;;
esac
