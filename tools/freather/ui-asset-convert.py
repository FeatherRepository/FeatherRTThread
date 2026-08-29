#!/usr/bin/env python3
"""Convert a PNG into an LVGL 9.2 C image descriptor for FeatherTalk."""

import argparse
import re
import struct
import zlib
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert PNG to LVGL 9.2 RGB565 or ARGB8888 C source")
    parser.add_argument("input", type=Path, help="source PNG")
    parser.add_argument("output", type=Path, help="destination .c file")
    parser.add_argument("--name", required=True, help="C symbol name")
    parser.add_argument("--format", choices=("rgb565", "argb8888"), default="argb8888")
    return parser.parse_args()


def paeth(left, above, upper_left):
    estimate = left + above - upper_left
    distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
    return (left, above, upper_left)[distances.index(min(distances))]


def decode_png(path):
    """Decode non-interlaced, 8-bit grayscale/RGB/palette/RGBA PNG."""
    content = path.read_bytes()
    if content[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("input is not a PNG file")
    offset = 8
    image_data = bytearray()
    palette = None
    transparency = b""
    width = height = bit_depth = color_type = interlace = None
    while offset < len(content):
        length = struct.unpack(">I", content[offset:offset + 4])[0]
        chunk_type = content[offset + 4:offset + 8]
        chunk = content[offset + 8:offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif chunk_type == b"tRNS":
            transparency = chunk
        elif chunk_type == b"IDAT":
            image_data.extend(chunk)
        elif chunk_type == b"IEND":
            break
    if bit_depth != 8 or interlace != 0:
        raise ValueError("only non-interlaced 8-bit PNG files are supported")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ValueError(f"unsupported PNG color type {color_type}")
    raw = zlib.decompress(bytes(image_data))
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        encoded = raw[cursor:cursor + stride]
        cursor += stride
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = value + left
            elif filter_type == 2:
                decoded = value + above
            elif filter_type == 3:
                decoded = value + ((left + above) // 2)
            elif filter_type == 4:
                decoded = value + paeth(left, above, upper_left)
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}")
            row[index] = decoded & 0xFF
        rows.append(row)
        previous = row

    pixels = []
    for row in rows:
        for index in range(0, len(row), channels):
            sample = row[index:index + channels]
            if color_type == 0:
                red = green = blue = sample[0]
                alpha = 255
            elif color_type == 2:
                red, green, blue = sample
                alpha = 255
            elif color_type == 3:
                if palette is None or sample[0] >= len(palette):
                    raise ValueError("invalid PNG palette index")
                red, green, blue = palette[sample[0]]
                alpha = transparency[sample[0]] if sample[0] < len(transparency) else 255
            elif color_type == 4:
                red = green = blue = sample[0]
                alpha = sample[1]
            else:
                red, green, blue, alpha = sample
            pixels.append((red, green, blue, alpha))
    return width, height, pixels


def encode_pixels(pixels, pixel_format):
    result = bytearray()
    for red, green, blue, alpha in pixels:
        if pixel_format == "rgb565":
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            result.extend((value & 0xFF, value >> 8))
        else:
            result.extend((blue, green, red, alpha))
    return result


def format_bytes(data):
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    return "\n".join(lines)


def main():
    args = parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.name):
        raise SystemExit("--name must be a valid C identifier")
    try:
        width, height, pixels = decode_png(args.input)
    except (OSError, ValueError, zlib.error) as error:
        raise SystemExit(f"PNG decode failed: {error}") from error
    data = encode_pixels(pixels, args.format)

    bytes_per_pixel = 2 if args.format == "rgb565" else 4
    lv_format = "LV_COLOR_FORMAT_RGB565" if args.format == "rgb565" else "LV_COLOR_FORMAT_ARGB8888"
    source_text = f"""/* Generated by tools/freather/ui-asset-convert.py. */
#include \"lvgl.h\"

static const uint8_t {args.name}_map[] = {{
{format_bytes(data)}
}};

const lv_image_dsc_t {args.name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = {lv_format},
    .header.w = {width},
    .header.h = {height},
    .header.stride = {width * bytes_per_pixel},
    .data_size = sizeof({args.name}_map),
    .data = {args.name}_map,
}};
"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source_text, encoding="utf-8", newline="\n")
    print(f"{args.input} -> {args.output}: {width}x{height} {args.format}, {len(data)} bytes")


if __name__ == "__main__":
    main()
