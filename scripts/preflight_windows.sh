#!/usr/bin/env bash
#
# Local preflight for Windows: cross-compiles the static .exe in Docker,
# verifies imports and runs the Wine smoke test.
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

docker build -f Dockerfile.windows --target test -t secgui-windows-test .

mkdir -p dist
docker build -f Dockerfile.windows --target export --output type=local,dest=dist .

echo
echo "artifacts:"
ls -lh dist
