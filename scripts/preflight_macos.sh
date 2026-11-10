#!/usr/bin/env bash
#
# Local preflight for macOS: build, smoke test and package the .dmg.
# Must run on macOS.
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

cmake --preset macos-release
cmake --build --preset macos-release -j "$(sysctl -n hw.ncpu)"

APP="build/macos-release/secmotion-gui.app"
./"$APP/Contents/MacOS/secmotion-gui" --headless --strict \
  --scene examples/minimal/scene.json \
  --script examples/minimal/demo.jsonl \
  --output-dir out/macos

bash scripts/package_dmg.sh "$APP" dist

echo
echo "artifacts:"
ls -lh dist
