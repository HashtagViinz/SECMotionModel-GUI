#!/usr/bin/env bash
#
# Builds a Linux AppImage from a secmotion-gui binary.
#
#   scripts/package_appimage.sh <binary> <output-dir>
#
set -euo pipefail

BIN="${1:?usage: package_appimage.sh <binary> <output-dir>}"
OUT="${2:-dist}"
VERSION="${VERSION:-0.1.0}"
APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"

if [ ! -x "$BIN" ]; then
  echo "error: binary not found or not executable: $BIN" >&2
  exit 1
fi

WORKDIR="$(mktemp -d)"
APPDIR="$WORKDIR/AppDir"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BIN" "$APPDIR/usr/bin/secmotion-gui"

python3 - "$APPDIR/usr/share/icons/hicolor/256x256/apps/secmotion-gui.png" <<'PY'
import struct
import sys
import zlib

width = height = 256
rows = bytearray()
for y in range(height):
    rows.append(0)  # PNG filter: None
    for x in range(width):
        t = x / (width - 1)
        s = y / (height - 1)
        r = int(10 + 70 * (1.0 - s))
        g = int(25 + 190 * t * (1.0 - 0.35 * s))
        b = int(70 + 160 * (1.0 - t))
        rows += bytes((r & 0xFF, g & 0xFF, b & 0xFF, 255))

def chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
       + chunk(b"IEND", b""))
with open(sys.argv[1], "wb") as handle:
    handle.write(png)
PY

cat > "$APPDIR/usr/share/applications/secmotion-gui.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=SECMotionModel GUI
Comment=Satellite edge computing visualizer
Exec=secmotion-gui
Icon=secmotion-gui
Categories=Science;Engineering;
Terminal=false
EOF

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/secmotion-gui" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# appimagetool looks for the desktop file and the icon both in the standard
# XDG locations and at the AppDir root.
cp "$APPDIR/usr/share/applications/secmotion-gui.desktop" "$APPDIR/secmotion-gui.desktop"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/secmotion-gui.png" "$APPDIR/secmotion-gui.png"
cp "$APPDIR/secmotion-gui.png" "$APPDIR/.DirIcon"

mkdir -p "$OUT"
TARGET="$OUT/SECMotionModel-GUI-${VERSION}-x86_64.AppImage"

# appimagetool itself may be an AppImage; APPIMAGE_EXTRACT_AND_RUN lets it
# work in containers without FUSE.
ARCH=x86_64 "$APPIMAGETOOL" --no-appstream "$APPDIR" "$TARGET"

echo "AppImage written: $TARGET"
