#!/usr/bin/env bash
#
# Local preflight for Linux: builds in Docker, runs unit tests and the
# headless smoke test, packages the AppImage and re-tests it.
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

mkdir -p dist
docker build -f Dockerfile.linux --target export --output type=local,dest=dist .

echo
echo "artifacts:"
ls -lh dist
