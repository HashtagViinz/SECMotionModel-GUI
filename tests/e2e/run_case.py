#!/usr/bin/env python3
"""
End-to-end case runner for the SECMotionModel GUI.

Runs the executable headlessly with a JSONL script, validates every JSON
result on stdout, checks the generated PNGs (dimensions, non-blank flag and,
optionally, a SHA-256) and exits non-zero on the first failure.

Usage:
    python tests/e2e/run_case.py \
        --binary build/linux-release/secmotion-gui \
        --scene examples/minimal/scene.json \
        --script examples/minimal/demo.jsonl \
        --output-dir out/e2e

For the AppImage:
    APPIMAGE_EXTRACT_AND_RUN=1 python tests/e2e/run_case.py \
        --binary dist/SECMotionModel-GUI-0.1.0-x86_64.AppImage ...
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run one GUI end-to-end case")
    parser.add_argument("--binary", required=True, help="executable or AppImage")
    parser.add_argument("--scene", required=True, help="scene manifest")
    parser.add_argument("--script", required=True, help="JSONL command script")
    parser.add_argument("--tasks", default=None, help="optional tasks JSONL")
    parser.add_argument("--output-dir", required=True, help="output directory")
    parser.add_argument("--backend", default="vulkan", help="WebGPU backend")
    parser.add_argument("--timeout", type=float, default=120.0, help="timeout in seconds")
    parser.add_argument("--expect-sha256", default=None, help="optional screenshot hash")
    return parser.parse_args()


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        if handle.read(8) != b"\x89PNG\r\n\x1a\n":
            raise SystemExit(f"{path}: not a PNG")
        length = struct.unpack(">I", handle.read(4))[0]
        if handle.read(4) != b"IHDR":
            raise SystemExit(f"{path}: missing IHDR")
        width, height = struct.unpack(">II", handle.read(8))
        return width, height


def main() -> int:
    args = parse_arguments()
    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit(f"binary not found: {binary}")

    command = [
        str(binary),
        "--headless",
        "--strict",
        "--backend",
        args.backend,
        "--scene",
        args.scene,
        "--script",
        args.script,
        "--output-dir",
        args.output_dir,
    ]
    if args.tasks:
        command += ["--tasks", args.tasks]

    print("$", " ".join(command))
    completed = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
    if completed.returncode != 0:
        print(completed.stdout)
        print(completed.stderr, file=sys.stderr)
        raise SystemExit(f"command failed with exit code {completed.returncode}")

    results = []
    for line in completed.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        document = json.loads(line)
        if document.get("event"):
            continue
        results.append(document)
        if not document.get("ok", False):
            raise SystemExit(f"command failed: {line}")

    screenshots = [item["result"] for item in results if item.get("cmd") == "screenshot"]
    if not screenshots:
        raise SystemExit("script did not produce a screenshot")

    for screenshot in screenshots:
        path = Path(screenshot["path"])
        if not path.exists():
            raise SystemExit(f"missing screenshot: {path}")
        width, height = png_size(path)
        if (width, height) != (screenshot["width"], screenshot["height"]):
            raise SystemExit(
                f"{path}: size {width}x{height} != reported "
                f"{screenshot['width']}x{screenshot['height']}"
            )
        if not screenshot.get("nonblank", False):
            raise SystemExit(f"{path}: screenshot is blank")
        if args.expect_sha256 and screenshot["sha256"] != args.expect_sha256:
            raise SystemExit(
                f"{path}: sha256 {screenshot['sha256']} != expected {args.expect_sha256}"
            )
        print(f"ok screenshot {path} {width}x{height} sha256={screenshot['sha256']}")

    print(f"case passed: {len(results)} commands, {len(screenshots)} screenshot(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
