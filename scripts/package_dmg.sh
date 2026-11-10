#!/usr/bin/env bash
#
# Builds a macOS .dmg from a .app bundle.
#
#   scripts/package_dmg.sh <app-bundle> <output-dir>
#
set -euo pipefail

APP="${1:?usage: package_dmg.sh <app-bundle> <output-dir>}"
OUT="${2:-dist}"
VERSION="${VERSION:-0.1.0}"
ARCH="${ARCH:-arm64}"

if [ ! -d "$APP" ]; then
  echo "error: app bundle not found: $APP" >&2
  exit 1
fi

STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

mkdir -p "$OUT"
TARGET="$OUT/SECMotionModel-GUI-${VERSION}-macos-${ARCH}.dmg"

hdiutil create \
  -volname "SECMotionModel GUI" \
  -srcfolder "$STAGE" \
  -ov -format UDZO \
  "$TARGET"

echo "dmg written: $TARGET"
