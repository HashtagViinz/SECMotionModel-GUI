#!/usr/bin/env python3
"""Grade the deserts of an equirectangular Earth map to a sandy tone.

The bright cream deserts of the Solar System Scope day map match the
near-white satellite markers, so the markers disappear over Sahara and
Arabia. This script darkens and warms only the desert pixels; oceans,
vegetation and ice are copied unchanged.

Usage:
    python3 tools/grade_deserts.py 2k_earth_daymap.jpg earth_2048.jpg

The committed ``assets/earth_2048.jpg`` is already graded. Regenerate it from
the original Solar System Scope download, then run ``tools/embed_texture.py``
to refresh the embedded header. Requires Pillow and NumPy (development only;
the build itself needs neither).
"""

from __future__ import annotations

import argparse

import numpy as np
from PIL import Image


def grade_deserts(source: Image.Image) -> Image.Image:
    """Return a copy of *source* with only its desert pixels graded to sand."""
    rgb = np.asarray(source.convert("RGB"), dtype=np.float32) / 255.0
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]

    maximum = rgb.max(axis=2)
    minimum = rgb.min(axis=2)
    saturation = np.where(
        maximum > 0.0, (maximum - minimum) / np.maximum(maximum, 1e-6), 0.0
    )
    luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue

    # Warm, bright, low-to-mid saturation pixels are deserts. Neutral clouds
    # and ice (r == g == b) and green vegetation (g > r) are excluded.
    mask = (
        (red > green)
        & (green > blue)
        & (luminance > 0.40)
        & (saturation > 0.08)
        & (saturation < 0.60)
        & ((red - blue) > 0.06)
    )
    # Feather the mask edges so desert coastlines do not show a hard seam.
    feather = np.clip((luminance - 0.40) / 0.18, 0.0, 1.0) * np.clip(
        (saturation - 0.08) / 0.10, 0.0, 1.0
    )
    mask = mask.astype(np.float32) * feather

    shade = luminance[..., None]
    graded = shade + (rgb - shade) * 1.20
    graded[..., 0] *= 1.05
    graded[..., 1] *= 1.00
    graded[..., 2] *= 0.86
    graded = np.clip(graded * 0.90, 0.0, 1.0)

    out = rgb * (1.0 - mask[..., None]) + graded * mask[..., None]
    return Image.fromarray((out * 255.0 + 0.5).astype(np.uint8))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", help="original equirectangular Earth JPEG")
    parser.add_argument("output", help="graded JPEG to write")
    parser.add_argument("--quality", type=int, default=92, help="JPEG quality")
    args = parser.parse_args()

    source = Image.open(args.input)
    graded = grade_deserts(source)
    graded.save(args.output, quality=args.quality, optimize=True)


if __name__ == "__main__":
    main()
