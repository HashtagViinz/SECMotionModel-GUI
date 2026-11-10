#!/usr/bin/env bash
#
# Uploads a file to the `agent-media` prerelease and prints the public URL.
# Used by the gh-issues skill so screenshots and logs can be linked from
# cross-repo issues.
#
set -euo pipefail

REPO="${REPO:-HashtagViinz/SECMotionModel-GUI}"
FILE="${1:?usage: gh_attach_media.sh <file>}"

if [ ! -f "$FILE" ]; then
  echo "error: file not found: $FILE" >&2
  exit 1
fi

gh auth status >/dev/null 2>&1 || {
  echo "error: gh is not authenticated" >&2
  exit 1
}

if ! gh release view agent-media --repo "$REPO" >/dev/null 2>&1; then
  gh release create agent-media --repo "$REPO" --prerelease \
    --title "Agent media" \
    --notes "Screenshots, logs and data attached to agent issues."
fi

gh release upload agent-media "$FILE" --repo "$REPO" --clobber

NAME="$(basename "$FILE")"
echo "https://github.com/${REPO}/releases/download/agent-media/${NAME}"
