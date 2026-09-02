#!/usr/bin/env python3
"""Convert constrained FeatherTalk SVG icons into LVGL A8 and vector assets.

The converter intentionally accepts only the small, deterministic SVG subset
used by applications/ui/assets-src.  It has no third-party dependencies and
rejects paths, transforms, filters, embedded images, scripts, and external
references instead of silently producing an incomplete icon.
"""

import argparse
import math
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


SUPPORTED_ELEMENTS = {"svg", "g", "line", "polyline", "polygon", "rect", "circle", "ellipse"}
INHERITED_ATTRIBUTES = {
    "fill",
    "fill-opacity",
    "opacity",
    "stroke",
    "stroke-opacity",
    "stroke-width",
    "stroke-linecap",
    "stroke-linejoin",
}
GEOMETRY_ATTRIBUTES = {
    "line": {"x1", "y1", "x2", "y2"},
    "polyline": {"points"},
    "polygon": {"points"},
    "rect": {"x", "y", "width", "height"},
    "circle": {"cx", "cy", "r"},
    "ellipse": {"cx", "cy", "rx", "ry"},
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert constrained SVG icon sources into LVGL A8 C assets")
    parser.add_argument("input", type=Path, help="directory containing .svg files")
    parser.add_argument("output_c", type=Path, help="generated C source")
    parser.add_argument("output_h", type=Path, help="generated C header")
    parser.add_argument("--sizes", default="24,32,48", help="comma-separated output sizes")
    parser.add_argument("--samples", type=int, default=4, help="supersampling grid per axis")
    parser.add_argument("--vector-output-c", type=Path,
                        help="optional generated vector geometry C source")
    parser.add_argument("--vector-output-h", type=Path,
                        help="optional generated vector geometry C header")
    return parser.parse_args()


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def number(value, default=0.0):
    if value is None:
        return default
    match = re.fullmatch(r"\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))\s*", value)
    if match is None:
        raise ValueError(f"unsupported numeric value: {value!r}")
    return float(match.group(1))


def opacity(value, default=1.0):
    return min(1.0, max(0.0, number(value, default)))


def style_attributes(element):
    result = {}
    style = element.attrib.get("style", "")
    if style:
        for declaration in style.split(";"):
            if not declaration.strip():
                continue
            if ":" not in declaration:
                raise ValueError(f"malformed style declaration: {declaration!r}")
            key, value = declaration.split(":", 1)
            key = key.strip()
            if key not in INHERITED_ATTRIBUTES:
                raise ValueError(f"unsupported SVG style property: {key!r}")
            result[key] = value.strip()
    for key in INHERITED_ATTRIBUTES:
        if key in element.attrib:
            result[key] = element.attrib[key]
    return result


def points(value):
    values = [float(item) for item in re.findall(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)", value or "")]
    if len(values) < 4 or len(values) % 2:
        raise ValueError(f"invalid points list: {value!r}")
    return list(zip(values[0::2], values[1::2]))


def point_segment_distance(px, py, ax, ay, bx, by):
    dx = bx - ax
    dy = by - ay
    length_squared = dx * dx + dy * dy
    if length_squared == 0.0:
        return math.hypot(px - ax, py - ay)
    projection = ((px - ax) * dx + (py - ay) * dy) / length_squared
    projection = min(1.0, max(0.0, projection))
    return math.hypot(px - (ax + projection * dx), py - (ay + projection * dy))


def point_in_polygon(px, py, vertices):
    inside = False
    previous = vertices[-1]
    for current in vertices:
        ax, ay = previous
        bx, by = current
        if (ay > py) != (by > py):
            crossing = (bx - ax) * (py - ay) / (by - ay) + ax
            if px < crossing:
                inside = not inside
        previous = current
    return inside


def polyline_stroke(px, py, vertices, width, closed=False):
    pairs = list(zip(vertices, vertices[1:]))
    if closed:
        pairs.append((vertices[-1], vertices[0]))
    radius = width / 2.0
    return any(point_segment_distance(px, py, *start, *end) <= radius for start, end in pairs)


def shape_alpha(shape, px, py):
    tag, attributes, appearance = shape
    fill = appearance.get("fill", "black") != "none"
    stroke = appearance.get("stroke", "none") != "none"
    common_opacity = opacity(appearance.get("opacity"), 1.0)
    fill_alpha = common_opacity * opacity(appearance.get("fill-opacity"), 1.0)
    stroke_alpha = common_opacity * opacity(appearance.get("stroke-opacity"), 1.0)
    stroke_width = number(appearance.get("stroke-width"), 1.0)
    inside_fill = False
    inside_stroke = False

    if tag == "line":
        inside_stroke = point_segment_distance(
            px, py,
            number(attributes.get("x1")), number(attributes.get("y1")),
            number(attributes.get("x2")), number(attributes.get("y2"))) <= stroke_width / 2.0
    elif tag in {"polyline", "polygon"}:
        vertices = points(attributes.get("points"))
        inside_fill = tag == "polygon" and point_in_polygon(px, py, vertices)
        inside_stroke = polyline_stroke(px, py, vertices, stroke_width, tag == "polygon")
    elif tag == "rect":
        x = number(attributes.get("x"))
        y = number(attributes.get("y"))
        width = number(attributes.get("width"))
        height = number(attributes.get("height"))
        inside_fill = x <= px <= x + width and y <= py <= y + height
        outer = (x - stroke_width / 2.0 <= px <= x + width + stroke_width / 2.0 and
                 y - stroke_width / 2.0 <= py <= y + height + stroke_width / 2.0)
        inner = (x + stroke_width / 2.0 < px < x + width - stroke_width / 2.0 and
                 y + stroke_width / 2.0 < py < y + height - stroke_width / 2.0)
        inside_stroke = outer and not inner
    elif tag in {"circle", "ellipse"}:
        cx = number(attributes.get("cx"))
        cy = number(attributes.get("cy"))
        rx = number(attributes.get("r")) if tag == "circle" else number(attributes.get("rx"))
        ry = rx if tag == "circle" else number(attributes.get("ry"))
        if rx <= 0.0 or ry <= 0.0:
            return 0.0
        normalized = math.hypot((px - cx) / rx, (py - cy) / ry)
        inside_fill = normalized <= 1.0
        normalized_width = stroke_width / (2.0 * min(rx, ry))
        inside_stroke = abs(normalized - 1.0) <= normalized_width
    else:
        raise ValueError(f"unsupported drawable element: {tag}")

    result = 0.0
    if fill and inside_fill:
        result = max(result, fill_alpha)
    if stroke and inside_stroke:
        result = max(result, stroke_alpha)
    return result


def collect_shapes(element, inherited=None):
    inherited = dict(inherited or {})
    inherited.update(style_attributes(element))
    tag = local_name(element.tag)
    if tag not in SUPPORTED_ELEMENTS:
        raise ValueError(f"unsupported SVG element <{tag}>")
    allowed_attributes = set(INHERITED_ATTRIBUTES) | {"style"}
    if tag == "svg":
        allowed_attributes.add("viewBox")
    elif tag != "g":
        allowed_attributes.update(GEOMETRY_ATTRIBUTES[tag])
    unsupported_attributes = set(element.attrib) - allowed_attributes
    if unsupported_attributes:
        names = ", ".join(sorted(unsupported_attributes))
        raise ValueError(f"unsupported attribute(s) on <{tag}>: {names}")
    shapes = []
    if tag not in {"svg", "g"}:
        shapes.append((tag, dict(element.attrib), inherited))
    for child in element:
        shapes.extend(collect_shapes(child, inherited))
    return shapes


def load_svg(path):
    root = ET.parse(path).getroot()
    if local_name(root.tag) != "svg":
        raise ValueError("root element is not <svg>")
    view_box = [float(item) for item in (root.attrib.get("viewBox") or "").replace(",", " ").split()]
    if len(view_box) != 4 or view_box[2] <= 0.0 or view_box[3] <= 0.0:
        raise ValueError("a positive four-number viewBox is required")
    return view_box, collect_shapes(root)


def rasterize(view_box, shapes, size, samples):
    min_x, min_y, width, height = view_box
    data = bytearray()
    sample_count = samples * samples
    for y in range(size):
        for x in range(size):
            accumulated = 0.0
            for sy in range(samples):
                py = min_y + (y + (sy + 0.5) / samples) * height / size
                for sx in range(samples):
                    px = min_x + (x + (sx + 0.5) / samples) * width / size
                    accumulated += max((shape_alpha(shape, px, py) for shape in shapes), default=0.0)
            data.append(round(255.0 * accumulated / sample_count))
    return data


def c_identifier(path):
    identifier = re.sub(r"[^a-z0-9]+", "_", path.stem.lower()).strip("_")
    if not identifier or identifier[0].isdigit():
        raise ValueError(f"SVG filename cannot become a C identifier: {path.name}")
    return identifier


def format_bytes(data):
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    return "\n".join(lines)


def write_outputs(entries, output_c, output_h, sizes, source_root):
    guard = "FEATHERTALK_ICON_ASSETS_H"
    declarations = []
    definitions = []
    total_bytes = 0
    for path, identifier, _view_box, _shapes, rendered in entries:
        relative = path.relative_to(source_root).as_posix()
        for size in sizes:
            symbol = f"ft_icon_asset_{identifier}_{size}"
            map_symbol = f"{symbol}_map"
            data = rendered[size]
            total_bytes += len(data)
            declarations.append(f"extern const lv_image_dsc_t {symbol};")
            definitions.append(
                f"/* Source: {relative} */\n"
                f"static const uint8_t {map_symbol}[] = {{\n{format_bytes(data)}\n}};\n\n"
                f"const lv_image_dsc_t {symbol} = {{\n"
                f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
                f"    .header.cf = LV_COLOR_FORMAT_A8,\n"
                f"    .header.w = {size},\n"
                f"    .header.h = {size},\n"
                f"    .header.stride = {size},\n"
                f"    .data_size = sizeof({map_symbol}),\n"
                f"    .data = {map_symbol},\n"
                f"}};\n")

    header = (
        "/* Generated by tools/freather/ui-svg-icon-convert.py. Do not edit. */\n"
        f"#ifndef {guard}\n#define {guard}\n\n#include \"lvgl.h\"\n\n"
        + "\n".join(declarations)
        + f"\n\n#endif /* {guard} */\n")
    source = (
        "/* Generated by tools/freather/ui-svg-icon-convert.py. Do not edit. */\n"
        "#include \"feathertalk_icon_assets.h\"\n\n"
        + "\n".join(definitions))
    output_c.parent.mkdir(parents=True, exist_ok=True)
    output_h.parent.mkdir(parents=True, exist_ok=True)
    output_c.write_text(source, encoding="utf-8", newline="\n")
    output_h.write_text(header, encoding="utf-8", newline="\n")
    return total_bytes


def vector_shape(shape):
    tag, attributes, appearance = shape
    fill_enabled = appearance.get("fill", "black") != "none" and tag not in {"line", "polyline"}
    stroke_enabled = appearance.get("stroke", "none") != "none"
    flags = (1 if fill_enabled else 0) | (2 if stroke_enabled else 0)

    if tag in {"polyline", "polygon"}:
        vertices = points(attributes.get("points"))
        shape_type = 1 if tag == "polyline" else 2
        values = (0.0, 0.0, 0.0, 0.0)
    elif tag == "line":
        vertices = []
        shape_type = 0
        values = (number(attributes.get("x1")), number(attributes.get("y1")),
                  number(attributes.get("x2")), number(attributes.get("y2")))
    elif tag == "rect":
        vertices = []
        shape_type = 3
        values = (number(attributes.get("x")), number(attributes.get("y")),
                  number(attributes.get("width")), number(attributes.get("height")))
    elif tag in {"circle", "ellipse"}:
        vertices = []
        shape_type = 4
        radius_x = number(attributes.get("r")) if tag == "circle" else number(attributes.get("rx"))
        radius_y = radius_x if tag == "circle" else number(attributes.get("ry"))
        values = (number(attributes.get("cx")), number(attributes.get("cy")),
                  radius_x, radius_y)
    else:
        raise ValueError(f"unsupported vector shape: {tag}")

    common_opacity = opacity(appearance.get("opacity"), 1.0)
    if fill_enabled and common_opacity * opacity(appearance.get("fill-opacity"), 1.0) != 1.0:
        raise ValueError("vector icons require fully opaque fill geometry")
    if stroke_enabled and common_opacity * opacity(appearance.get("stroke-opacity"), 1.0) != 1.0:
        raise ValueError("vector icons require fully opaque stroke geometry")

    stroke_style = None
    if stroke_enabled:
        caps = {"butt": 0, "square": 1, "round": 2}
        joins = {"miter": 0, "bevel": 1, "round": 2}
        cap = appearance.get("stroke-linecap", "butt")
        join = appearance.get("stroke-linejoin", "miter")
        if cap not in caps or join not in joins:
            raise ValueError(f"unsupported vector stroke style: {cap}/{join}")
        stroke_style = (number(appearance.get("stroke-width"), 1.0), caps[cap], joins[join])

    return shape_type, flags, vertices, values, stroke_style


def c_float(value):
    text = f"{value:.6f}".rstrip("0").rstrip(".")
    if "." not in text:
        text += ".0"
    return text + "f"


def float_word(value):
    return struct.unpack("<I", struct.pack("<f", float(value)))[0]


def append_native_command(words, opcode, coordinates=()):
    words.append(opcode)
    words.extend(float_word(value) for value in coordinates)


def append_native_shape(words, shape_type, vertices, values):
    if shape_type == 0:  # line
        append_native_command(words, 0x02, values[0:2])
        append_native_command(words, 0x04, values[2:4])
    elif shape_type in {1, 2}:  # polyline / polygon
        append_native_command(words, 0x02, vertices[0])
        for vertex in vertices[1:]:
            append_native_command(words, 0x04, vertex)
        if shape_type == 2:
            append_native_command(words, 0x01)
    elif shape_type == 3:  # rect
        x, y, width, height = values
        append_native_command(words, 0x02, (x, y))
        append_native_command(words, 0x04, (x + width, y))
        append_native_command(words, 0x04, (x + width, y + height))
        append_native_command(words, 0x04, (x, y + height))
        append_native_command(words, 0x01)
    elif shape_type == 4:  # ellipse
        cx, cy, rx, ry = values
        k = 0.55228475
        append_native_command(words, 0x02, (cx + rx, cy))
        append_native_command(words, 0x08,
                              (cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry))
        append_native_command(words, 0x08,
                              (cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy))
        append_native_command(words, 0x08,
                              (cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry))
        append_native_command(words, 0x08,
                              (cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy))
        append_native_command(words, 0x01)
    else:
        raise ValueError(f"unsupported native vector shape type: {shape_type}")


def native_shape_bounds(shape_type, vertices, values):
    if shape_type == 0:
        coordinates = ((values[0], values[1]), (values[2], values[3]))
    elif shape_type in {1, 2}:
        coordinates = vertices
    elif shape_type == 3:
        x, y, width, height = values
        coordinates = ((x, y), (x + width, y + height))
    elif shape_type == 4:
        cx, cy, rx, ry = values
        coordinates = ((cx - rx, cy - ry), (cx + rx, cy + ry))
    else:
        raise ValueError(f"unsupported native vector shape type: {shape_type}")
    return (min(item[0] for item in coordinates),
            min(item[1] for item in coordinates),
            max(item[0] for item in coordinates),
            max(item[1] for item in coordinates))


def union_bounds(current, addition):
    if current is None:
        return addition
    return (min(current[0], addition[0]), min(current[1], addition[1]),
            max(current[2], addition[2]), max(current[3], addition[3]))


def format_words(values):
    lines = []
    for offset in range(0, len(values), 8):
        rows = ", ".join(f"0x{value:08x}U" for value in values[offset:offset + 8])
        lines.append(f"    {rows},")
    return "\n".join(lines)


def write_vector_outputs(entries, output_c, output_h, source_root):
    declarations = []
    definitions = []
    for path, identifier, view_box, shapes, _rendered in entries:
        relative = path.relative_to(source_root).as_posix()
        fill_words = []
        stroke_words = []
        fill_bounds = None
        stroke_bounds = None
        stroke_styles = set()
        for shape in shapes:
            shape_type, flags, vertices, values, stroke_style = vector_shape(shape)
            if stroke_style is not None:
                stroke_styles.add(stroke_style)
            if flags & 1:
                append_native_shape(fill_words, shape_type, vertices, values)
                fill_bounds = union_bounds(
                    fill_bounds, native_shape_bounds(shape_type, vertices, values))
            if flags & 2:
                append_native_shape(stroke_words, shape_type, vertices, values)
                stroke_bounds = union_bounds(
                    stroke_bounds, native_shape_bounds(shape_type, vertices, values))

        if len(stroke_styles) > 1:
            raise ValueError(f"{path.name} uses multiple stroke styles; split the asset first")
        stroke_width, stroke_cap, stroke_join = next(iter(stroke_styles), (0.0, 0, 0))
        if fill_words:
            fill_words.append(0x00)  # VG-Lite END for a filled compound path.
        fill_symbol = f"ft_icon_vector_{identifier}_fill_path"
        stroke_symbol = f"ft_icon_vector_{identifier}_stroke_path"
        asset_symbol = f"ft_icon_vector_{identifier}"
        arrays = []
        if fill_words:
            arrays.append(
                f"static const uint32_t {fill_symbol}[] = {{\n{format_words(fill_words)}\n}};\n")
        if stroke_words:
            arrays.append(
                f"static const uint32_t {stroke_symbol}[] = {{\n{format_words(stroke_words)}\n}};\n")
        definitions.append(
            f"/* Source: {relative} */\n"
            "\n".join(arrays) +
            f"const ft_icon_vector_asset_t {asset_symbol} = {{\n"
            f"    {fill_symbol if fill_words else '0'}, {len(fill_words) * 4}U,\n"
            f"    {stroke_symbol if stroke_words else '0'}, {len(stroke_words) * 4}U,\n"
            f"    {', '.join(c_float(value) for value in (fill_bounds or (0, 0, 0, 0)))},\n"
            f"    {', '.join(c_float(value) for value in (stroke_bounds or (0, 0, 0, 0)))},\n"
            f"    {c_float(view_box[0])}, {c_float(view_box[1])}, "
            f"{c_float(view_box[2])}, {c_float(view_box[3])},\n"
            f"    {c_float(stroke_width)}, {stroke_cap}U, {stroke_join}U,\n"
            "};\n")
        declarations.append(f"extern const ft_icon_vector_asset_t {asset_symbol};")

    guard = "FEATHERTALK_ICON_VECTOR_ASSETS_H"
    header = (
        "/* Generated by tools/freather/ui-svg-icon-convert.py. Do not edit. */\n"
        f"#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n"
        "typedef struct {\n"
        "    const uint32_t *fill_path;\n    uint32_t fill_path_bytes;\n"
        "    const uint32_t *stroke_path;\n    uint32_t stroke_path_bytes;\n"
        "    float fill_min_x;\n    float fill_min_y;\n"
        "    float fill_max_x;\n    float fill_max_y;\n"
        "    float stroke_min_x;\n    float stroke_min_y;\n"
        "    float stroke_max_x;\n    float stroke_max_y;\n"
        "    float view_x;\n    float view_y;\n    float view_w;\n    float view_h;\n"
        "    float stroke_width;\n    uint8_t stroke_cap;\n    uint8_t stroke_join;\n"
        "} ft_icon_vector_asset_t;\n\n" +
        "\n".join(declarations) +
        f"\n\n#endif /* {guard} */\n")
    source = (
        "/* Generated by tools/freather/ui-svg-icon-convert.py. Do not edit. */\n"
        "#include \"feathertalk_icon_vector_assets.h\"\n\n" +
        "\n".join(definitions))
    output_c.parent.mkdir(parents=True, exist_ok=True)
    output_h.parent.mkdir(parents=True, exist_ok=True)
    output_c.write_text(source, encoding="utf-8", newline="\n")
    output_h.write_text(header, encoding="utf-8", newline="\n")


def main():
    args = parse_args()
    try:
        sizes = sorted({int(item) for item in args.sizes.split(",") if item.strip()})
    except ValueError as error:
        raise SystemExit(f"invalid --sizes: {error}") from error
    if not sizes or any(size <= 0 or size > 256 for size in sizes):
        raise SystemExit("--sizes must contain values in the range 1..256")
    if args.samples < 1 or args.samples > 8:
        raise SystemExit("--samples must be in the range 1..8")
    svg_files = sorted(args.input.rglob("*.svg"))
    if not svg_files:
        raise SystemExit(f"no SVG files found under {args.input}")

    entries = []
    identifiers = set()
    try:
        for path in svg_files:
            identifier = c_identifier(path)
            if identifier in identifiers:
                raise ValueError(f"duplicate generated identifier: {identifier}")
            identifiers.add(identifier)
            view_box, shapes = load_svg(path)
            rendered = {size: rasterize(view_box, shapes, size, args.samples) for size in sizes}
            for size, data in rendered.items():
                if not any(data):
                    raise ValueError(f"{path.name} renders empty at {size}px")
                if all(value == 255 for value in data):
                    raise ValueError(f"{path.name} renders fully opaque at {size}px")
            entries.append((path, identifier, view_box, shapes, rendered))
    except (OSError, ET.ParseError, ValueError) as error:
        raise SystemExit(f"SVG conversion failed: {error}") from error

    total_bytes = write_outputs(entries, args.output_c, args.output_h, sizes, args.input)
    if bool(args.vector_output_c) != bool(args.vector_output_h):
        raise SystemExit("--vector-output-c and --vector-output-h must be used together")
    if args.vector_output_c:
        write_vector_outputs(entries, args.vector_output_c, args.vector_output_h, args.input)
    print(f"Generated {len(entries)} icons x {len(sizes)} sizes, {total_bytes} A8 bytes")


if __name__ == "__main__":
    main()
