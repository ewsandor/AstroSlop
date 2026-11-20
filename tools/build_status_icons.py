#!/usr/bin/env python3
"""Regenerate the watch status icons from Pebble/Rebble SVG sources."""

import argparse
from pathlib import Path
from struct import unpack_from
import subprocess
import sys
from tempfile import TemporaryDirectory
import xml.etree.ElementTree as ET

from svg.path import Close, Line, Move, parse_path


ICONS = {
    "location": "icons/25px_Location.svg",
    "location_problem": "icons/25px_Location_problem.svg",
    "alarm_clock": "pebbleos/Pebble_25x25_Alarm_clock.svg",
    "warning": "pebbleos/Pebble_25x25_Generic_warning.svg",
    "backlight": "pebbleos/Pebble_80x80_Backlight.svg",
}
COLORS = {"black": "#000000", "white": "#FFFFFF"}


def subpaths(data):
    """Split SVG move commands so the converter doesn't connect separate strokes."""
    points = []
    for segment in parse_path(data):
        if isinstance(segment, Move):
            if points:
                yield "polyline", points
            points = [segment.end]
        elif isinstance(segment, Close):
            yield "polygon", points
            points = []
        elif isinstance(segment, Line):
            points.append(segment.end)
        else:
            raise ValueError(f"Unsupported SVG path segment: {segment}")
    if points:
        yield "polyline", points


def normalize(source, size):
    original = ET.parse(source).getroot()
    x, y, width, height = map(float, original.attrib["viewBox"].split())
    if (x, y) != (0, 0) or width != height or width <= 0:
        raise ValueError(f"{source}: expected an origin-aligned square viewBox")
    scale = size / width

    def coord(value):
        # svg2pdc --precise uses a 1/8-pixel grid.
        return str(round(float(value) * scale * 8) / 8)

    root = ET.Element("svg", xmlns="http://www.w3.org/2000/svg", viewBox=f"0 0 {size} {size}")
    for element in original.iter():
        tag = element.tag.rsplit("}", 1)[-1]
        if "transform" in element.attrib or "style" in element.attrib:
            raise ValueError(f"{source}: unexpected transform or style on {tag}")
        if tag in ("svg", "g"):
            continue
        attrs = {}
        for key in ("fill", "stroke"):
            value = element.get(key, "none")
            attrs[key] = COLORS.get(value, value)
        if "stroke-width" in element.attrib:
            # Pebble stores stroke widths as whole pixels.
            stroke_width = float(element.attrib["stroke-width"]) * scale
            attrs["stroke-width"] = str(max(1, round(stroke_width)))

        if tag == "path":
            for shape, points in subpaths(element.attrib["d"]):
                coords = " ".join(f"{coord(p.real)},{coord(p.imag)}" for p in points)
                ET.SubElement(root, shape, attrs, points=coords)
        elif tag in ("polygon", "polyline"):
            coords = " ".join(",".join(map(coord, p.split(",")))
                              for p in element.attrib["points"].split())
            ET.SubElement(root, tag, attrs, points=coords)
        elif tag in ("line", "rect"):
            keys = ("x1", "y1", "x2", "y2") if tag == "line" else ("x", "y", "width", "height")
            ET.SubElement(root, tag, attrs, **{key: coord(element.attrib[key]) for key in keys})
        else:
            raise ValueError(f"{source}: unsupported SVG element {tag}")
    return ET.ElementTree(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iconography", type=Path, help="Checkout of pebble-dev/iconography")
    parser.add_argument("converter", type=Path, help="Path to Pebble's svg2pdc.py")
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[1] / "resources" / "icons"
    with TemporaryDirectory() as work:
        svg = Path(work) / "icon.svg"
        pdc = Path(work) / "icon.pdc"
        for name, source in ICONS.items():
            for size in (18, 25):
                normalize(args.iconography / source, size).write(svg, encoding="utf-8")
                subprocess.run([sys.executable, str(args.converter), "--precise",
                                "--output", str(pdc), str(svg)], check=True)
                data = pdc.read_bytes()
                if len(data) < 16 or data[:4] != b"PDCI":
                    raise ValueError(f"{name}_{size}: invalid PDC image")
                length, _, width, height, count = unpack_from("<IHhhH", data, 4)
                if length != len(data) - 8 or (width, height) != (size, size) or not count:
                    raise ValueError(f"{name}_{size}: expected a nonempty {size}x{size} PDC image")
                (output / f"{name}_{size}.pdc").write_bytes(data)
                pdc.unlink()


if __name__ == "__main__":
    main()
