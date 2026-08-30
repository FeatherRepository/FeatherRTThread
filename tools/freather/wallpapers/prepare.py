#!/usr/bin/env python3
"""Prepare the licensed FeatherTalk wallpaper set for the 480x800 panel."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps


TARGET_SIZE = (480, 800)
JPEG_QUALITY = 76
MAX_FILE_BYTES = 128 * 1024


def fit(source: Path) -> Image.Image:
    with Image.open(source) as image:
        return ImageOps.fit(
            image.convert("RGB"),
            TARGET_SIZE,
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5),
        )


def metro_facets(source: Path) -> Image.Image:
    image = ImageOps.grayscale(fit(source))
    image = ImageEnhance.Contrast(image).enhance(1.08)
    image = ImageOps.colorize(image, black="#020812", white="#0078D7")
    return ImageEnhance.Brightness(image).enhance(0.76)


def aurora_lines(source: Path) -> Image.Image:
    image = ImageEnhance.Color(fit(source)).enhance(0.88)
    image = ImageEnhance.Contrast(image).enhance(1.10)
    return ImageEnhance.Brightness(image).enhance(0.66)


def night_city(source: Path) -> Image.Image:
    image = ImageEnhance.Color(fit(source)).enhance(0.78)
    image = ImageEnhance.Contrast(image).enhance(1.06)
    return ImageEnhance.Brightness(image).enhance(0.62)


def save(image: Image.Image, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(
        destination,
        format="JPEG",
        quality=JPEG_QUALITY,
        optimize=True,
        progressive=False,
        subsampling=2,
    )
    with Image.open(destination) as check:
        if check.size != TARGET_SIZE or check.mode != "RGB":
            raise RuntimeError(f"invalid output image: {destination}")
    if destination.stat().st_size > MAX_FILE_BYTES:
        raise RuntimeError(f"wallpaper exceeds {MAX_FILE_BYTES} bytes: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()

    jobs = (
        (metro_facets, "geometric-original.jpg", "metro-facets.jpg"),
        (aurora_lines, "gradient-original.jpg", "aurora-lines.jpg"),
        (night_city, "city-night-original.jpg", "night-city.jpg"),
    )
    total = 0
    for transform, source_name, output_name in jobs:
        source = args.source_dir / source_name
        destination = args.output_dir / output_name
        save(transform(source), destination)
        size = destination.stat().st_size
        total += size
        print(f"{output_name}: {TARGET_SIZE[0]}x{TARGET_SIZE[1]}, {size} bytes")
    print(f"total: {total} bytes")


if __name__ == "__main__":
    main()
