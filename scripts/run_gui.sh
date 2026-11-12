#!/usr/bin/env bash
#
# Convenience launcher for the SECMotionModel GUI.
#
#   secgui                  minimal scene
#   secgui --naval          latest naval run (load + traffic), or a legacy scene
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
  --naval            open the latest naval run (load + traffic), or an
                     exported v1/v2 scene if no run is available
  --config <json>    export a core configurations JSON and open it
  --trace <jsonl>    optional task trace to use with --config
  --run <dir>        export a simulation run with load and traffic telemetry
  -h, --help         show this help

Any other argument is forwarded to secmotion-gui. A bare first argument is
treated as the scene path; a bare second argument as the tasks path.
Paths passed to --config and --trace are resolved against the current
directory first, then against the core repository.

Examples:
  secgui
  secgui --naval
  secgui --config data/naval_configurations_AP_optimal.json
  secgui --config data/naval_configurations_AP_optimal.json \
    --trace /tmp/opencode/naval-tasks.jsonl
  secgui --run result/naval/IN_NETWORK/optimal
  secgui examples/minimal/scene.json
  secgui --scene /path/manifest.json --tasks /path/tasks.jsonl --camera vessel
  secgui --naval --time 10 --camera vessel --target V-ATLANTIC

Environment:
  SECGUI_BIN            explicit executable to run
  SECGUI_NAVAL_RUN      naval run directory (default: result/naval/IN_NETWORK/optimal)
  SECGUI_NAVAL_SCENE    legacy naval scene directory; overrides SECGUI_NAVAL_RUN
  SECGUI_CACHE_DIR      scene cache (default: ~/.cache/secgui)
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

find_naval_run() {
	local core candidate
	core="$(find_core_root)" || return 1
	local candidates=(
		"${SECGUI_NAVAL_RUN:-}"
		"$core/result/naval/IN_NETWORK/optimal"
		"$core/result/naval/TO_OBSERVER/optimal"
	)
	for candidate in "${candidates[@]}"; do
		if [ -n "$candidate" ] && [ -f "$candidate/gui_run.json" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

find_core_root() {
	local candidate
	for candidate in "$REPO/.." "$REPO/../SECMotionModel" "$REPO/gui/.."; do
		if [ -f "$candidate/tools/export_gui_scene.py" ] && [ -d "$candidate/data" ]; then
			(cd "$candidate" && pwd)
			return 0
		fi
	done
	return 1
}

resolve_input_path() {
	local path="$1" root="$2"
	if [[ "$path" != /* ]]; then
		if [ -e "$path" ]; then
			path="$(cd "$(dirname "$path")" && pwd)/$(basename "$path")"
		else
			path="$root/$path"
		fi
	fi
	printf '%s\n' "$path"
}

export_config_scene() {
	local config="$1" trace="$2" core="$3"
	local stamp cache_root cache tmp trace_args=()
	stamp="$(stat -c '%Y-%s' "$config")"
	if [ -n "$trace" ]; then
		stamp="$stamp-$(stat -c '%Y-%s' "$trace")"
	fi
	cache_root="${SECGUI_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/secgui}"
	cache="$cache_root/$(basename "${config%.json}")-$(printf '%s' "$config|$stamp" | sha256sum | cut -c1-12)"

	if [ ! -f "$cache/manifest.json" ] || [ ! -f "$cache/.stamp" ] \
		|| [ "$(cat "$cache/.stamp")" != "$stamp" ]; then
		tmp="$cache.tmp.$$"
		rm -rf "$tmp"
		mkdir -p "$cache_root"
		echo "secgui: exporting $(basename "$config") -> $cache" >&2
		if [ -n "$trace" ]; then
			trace_args=(--trace "$trace")
		fi
		if ! (cd "$core" && python3 tools/export_gui_scene.py \
			--config "$config" --core-config config/config.json5 \
			--out "$tmp" "${trace_args[@]}") 1>&2; then
			rm -rf "$tmp"
			echo "secgui: export failed" >&2
			return 1
		fi
		printf '%s\n' "$stamp" >"$tmp/.stamp"
		rm -rf "$cache"
		mv "$tmp" "$cache"
	else
		echo "secgui: using cached scene $cache" >&2
	fi
	printf '%s\n' "$cache"
}

export_run_scene() {
	local run_dir="$1" core="$2"
	local meta="$run_dir/gui_run.json"
	local state="$run_dir/gui_state.csv" links="$run_dir/gui_links.csv"
	local stamp cache_root cache tmp
	if [ ! -f "$meta" ]; then
		echo "error: $meta not found" >&2
		return 1
	fi
	stamp="$(stat -c '%Y-%s' "$meta")-$(stat -c '%Y-%s' "$state" 2>/dev/null || echo 0)-$(stat -c '%Y-%s' "$links" 2>/dev/null || echo 0)"
	cache_root="${SECGUI_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/secgui}/runs"
	cache="$cache_root/$(basename "$(dirname "$run_dir")")-$(basename "$run_dir")-$(printf '%s' "$run_dir|$stamp" | sha256sum | cut -c1-12)"

	if [ ! -f "$cache/manifest.json" ] || [ ! -f "$cache/.stamp" ] \
		|| [ "$(cat "$cache/.stamp")" != "$stamp" ]; then
		tmp="$cache.tmp.$$"
		rm -rf "$tmp"
		mkdir -p "$cache_root"
		echo "secgui: exporting run $run_dir -> $cache" >&2
		if ! (cd "$core" && python3 tools/export_gui_scene.py \
			--run-dir "$run_dir" --out "$tmp") 1>&2; then
			rm -rf "$tmp"
			echo "secgui: export failed" >&2
			return 1
		fi
		printf '%s\n' "$stamp" >"$tmp/.stamp"
		rm -rf "$cache"
		mv "$tmp" "$cache"
	else
		echo "secgui: using cached run $cache" >&2
	fi
	printf '%s\n' "$cache"
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

mode="minimal"
filtered=()
config_path=""
trace_path=""
run_path=""
while [ $# -gt 0 ]; do
	case "$1" in
		--minimal) mode="minimal"; shift ;;
		--naval) mode="naval"; shift ;;
		--config)
			[ $# -ge 2 ] || { echo "error: --config requires a path" >&2; exit 2; }
			config_path="$2"; shift 2 ;;
		--trace)
			[ $# -ge 2 ] || { echo "error: --trace requires a path" >&2; exit 2; }
			trace_path="$2"; shift 2 ;;
		--run)
			[ $# -ge 2 ] || { echo "error: --run requires a directory" >&2; exit 2; }
			run_path="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) filtered+=("$1"); shift ;;
	esac
done

if [ -n "$trace_path" ] && [ -z "$config_path" ]; then
	echo "error: --trace requires --config" >&2
	exit 2
fi

if [ -n "$run_path" ]; then
	if [ -n "$config_path" ] || [ "$mode" = "naval" ]; then
		echo "error: --run cannot be combined with --config or --naval" >&2
		exit 2
	fi
	if [ ${#filtered[@]} -gt 0 ] && [[ "${filtered[0]}" != -* ]]; then
		echo "error: pass either --run or a scene path, not both" >&2
		exit 2
	fi
	for arg in "${filtered[@]}"; do
		if [ "$arg" = "--scene" ]; then
			echo "error: pass either --run or --scene, not both" >&2
			exit 2
		fi
	done
	core="$(find_core_root)" || {
		echo "error: core repository not found; cannot run tools/export_gui_scene.py" >&2
		exit 1
	}
	run_path="$(resolve_input_path "$run_path" "$core")"
	if [ ! -d "$run_path" ]; then
		echo "error: run directory not found: $run_path" >&2
		exit 1
	fi
	scene_dir="$(export_run_scene "$run_path" "$core")" || exit 1
	filtered=(--scene "$scene_dir/manifest.json" "${filtered[@]}")
fi

if [ -n "$config_path" ]; then
	if [ "$mode" = "naval" ]; then
		echo "error: --config and --naval are mutually exclusive" >&2
		exit 2
	fi
	if [ ${#filtered[@]} -gt 0 ] && [[ "${filtered[0]}" != -* ]]; then
		echo "error: pass either --config or a scene path, not both" >&2
		exit 2
	fi
	for arg in "${filtered[@]}"; do
		if [ "$arg" = "--scene" ]; then
			echo "error: pass either --config or --scene, not both" >&2
			exit 2
		fi
	done
	core="$(find_core_root)" || {
		echo "error: core repository not found; cannot run tools/export_gui_scene.py" >&2
		echo "Run secgui from the GUI repository or set SECGUI_BIN for a pre-exported scene." >&2
		exit 1
	}
	config_path="$(resolve_input_path "$config_path" "$core")"
	if [ ! -f "$config_path" ]; then
		echo "error: configuration not found: $config_path" >&2
		exit 1
	fi
	if [ -n "$trace_path" ]; then
		trace_path="$(resolve_input_path "$trace_path" "$core")"
		if [ ! -f "$trace_path" ]; then
			echo "error: trace not found: $trace_path" >&2
			exit 1
		fi
	fi
	scene_dir="$(export_config_scene "$config_path" "$trace_path" "$core")" || exit 1
	filtered=(--scene "$scene_dir/manifest.json" "${filtered[@]}")

	vessel_name="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("vessel_name") or "")' "$config_path" 2>/dev/null || true)"
	if [ -n "$vessel_name" ]; then
		has_camera=false
		has_target=false
		for arg in "${filtered[@]}"; do
			[ "$arg" = "--camera" ] && has_camera=true
			[ "$arg" = "--target" ] && has_target=true
		done
		if [ "$has_camera" = false ]; then
			filtered+=(--camera vessel)
		fi
		if [ "$has_target" = false ]; then
			filtered+=(--target "$vessel_name")
		fi
	fi
fi

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
		naval=""
		naval_run=""
		if [ -n "${SECGUI_NAVAL_SCENE:-}" ] && [ -f "$SECGUI_NAVAL_SCENE/manifest.json" ]; then
			naval="$SECGUI_NAVAL_SCENE"
		elif naval_run="$(find_naval_run)"; then
			core="$(find_core_root)"
			scene_dir="$(export_run_scene "$naval_run" "$core")" || exit 1
			filtered=(--scene "$scene_dir/manifest.json" --camera vessel --target V-ATLANTIC "${filtered[@]}")
		elif ! naval="$(find_naval_scene)"; then
			core="$(find_core_root 2>/dev/null || true)"
			echo "error: naval run or scene not found." >&2
			echo "Create a run with the GUI export enabled:" >&2
			echo "  cd ${core:-<SECMotionModel>}" >&2
			echo "  python3 naval_main.py --load-only --run-sim --gui-export --use-case IN_NETWORK" >&2
			echo "Then run: secgui --naval" >&2
			echo "Or open an existing run: secgui --run <run-dir>" >&2
			echo "Or set SECGUI_NAVAL_SCENE to a directory containing manifest.json." >&2
			exit 1
		fi
		if [ -n "$naval" ]; then
			naval_args=(--scene "$naval/manifest.json")
			if [ -f "$naval/tasks.jsonl" ]; then
				naval_args+=(--tasks "$naval/tasks.jsonl")
			fi
			filtered=("${naval_args[@]}" --camera vessel --target V-ATLANTIC "${filtered[@]}")
		fi
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
