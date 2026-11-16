#!/usr/bin/env python3
"""Compile offline VSOP87D, Meeus and JPL Horizons inputs into a streaming resource."""

import argparse
import csv
import gzip
import math
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "ephemeris"
PLANETS = ("mer", "ven", "ear", "mar", "jup", "sat", "ura", "nep")
MINORS = ("Vesta", "Juno", "Ceres", "Pallas")
MIN_RADIUS_AU = (0.30, 0.71, 0.98, 1.38, 4.9, 8.9, 18.0, 29.0)
FORMAT_VERSION = 2
HEADER = struct.Struct("<4sBBBBHHHHdIIIHHHH")
DIRECTORY = struct.Struct("<IHH")
TERM = struct.Struct("<ddd")
ELEMENTS = struct.Struct("<ddddddd")
# Meeus chapter 47 tables 47.A/47.B and chapter 37 table 37.A.
MOON_LONGITUDE = struct.Struct("<bbbbii")
MOON_LATITUDE = struct.Struct("<bbbbi")
PLUTO = struct.Struct("<bbbx6d")
MEEUS_TABLES = (
    ("moon_longitude.csv", MOON_LONGITUDE, 4, 60),
    ("moon_latitude.csv", MOON_LATITUDE, 4, 60),
    ("pluto.csv", PLUTO, 3, 43),
)
ARCSECONDS_PER_RADIAN = 180.0 / math.pi * 3600.0
MAX_ERROR_ARCSEC = 1.0


def read_vsop(path):
    series = [[[] for _ in range(6)] for _ in range(3)]
    current = None
    header = re.compile(r"VARIABLE\s+([123]).*\*T\*\*(\d)\s+(\d+)\s+TERMS?")
    expected = 0
    with gzip.open(path, "rt", encoding="ascii") as source:
        for line in source:
            if line.startswith(" VSOP87 VERSION"):
                if current is not None and len(current) != expected:
                    raise ValueError(f"{path}: incorrect number of terms in a series")
                match = header.search(line)
                if not match or int(match.group(2)) >= 6:
                    raise ValueError(f"{path}: invalid VSOP87D series header")
                coordinate, power, expected = map(int, match.groups())
                current = series[coordinate - 1][power]
                if current:
                    raise ValueError(f"{path}: duplicate VSOP87D series")
            elif line.strip():
                if current is None:
                    raise ValueError(f"{path}: term before series header")
                try:
                    values = tuple(float(part) for part in line.split()[-3:])
                except ValueError as exc:
                    raise ValueError(f"{path}: invalid VSOP87D term") from exc
                if len(values) != 3 or not all(math.isfinite(v) for v in values):
                    raise ValueError(f"{path}: invalid VSOP87D term")
                current.append(values)
    if current is None or len(current) != expected or any(not axis[0] for axis in series):
        raise ValueError(f"{path}: incomplete VSOP87D data")
    return series


def truncate(series, cutoff):
    kept = [[[] for _ in range(6)] for _ in range(3)]
    dropped = [[[] for _ in range(6)] for _ in range(3)]
    for axis in range(3):
        for power in range(6):
            for term in series[axis][power]:
                target = kept if abs(term[0]) * 0.1 ** power >= cutoff else dropped
                target[axis][power].append(term)
    return kept, dropped


def sampled_error(dropped, min_radius, samples=101):
    maximum = 0.0
    for index in range(samples):
        tau = -0.1 + 0.2 * index / (samples - 1)
        errors = []
        for axis in range(3):
            value = 0.0
            for power in range(6):
                subtotal = sum(amplitude * math.cos(phase + frequency * tau)
                               for amplitude, phase, frequency in dropped[axis][power])
                value += subtotal * tau ** power
            errors.append(value)
        # Radial error divided by distance is a conservative angular equivalent.
        maximum = max(maximum, math.sqrt(errors[0] ** 2 + errors[1] ** 2 +
                                         (errors[2] / min_radius) ** 2))
    return maximum * ARCSECONDS_PER_RADIAN


def select_terms(series, min_radius):
    for cutoff in (1e-7, 3e-8, 1e-8, 3e-9, 1e-9, 3e-10, 1e-10, 0):
        kept, dropped = truncate(series, cutoff)
        error = sampled_error(dropped, min_radius)
        if error <= MAX_ERROR_ARCSEC:
            return kept, error
    raise ValueError("could not meet the VSOP87D truncation target")


def read_minor(path):
    groups = {name: [] for name in MINORS}
    with path.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(line for line in source if not line.startswith("#"))
        for row in reader:
            name = row["body"]
            if name not in groups:
                raise ValueError(f"{path}: unknown minor planet {name!r}")
            fields = ("jd_tdb", "a_au", "e", "incl_deg", "node_deg", "peri_deg", "mean_deg")
            values = tuple(float(row[field]) for field in fields)
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"{path}: non-finite element")
            if values[1] <= 0 or not 0 <= values[2] < 1:
                raise ValueError(f"{path}: invalid elliptic orbit")
            groups[name].append(values)
    count = len(groups[MINORS[0]])
    if count < 2 or count > 65535 or any(len(group) != count for group in groups.values()):
        raise ValueError(f"{path}: inconsistent minor-planet epoch counts")
    first = groups[MINORS[0]][0][0]
    step = groups[MINORS[0]][1][0] - first
    if step != int(step) or not 1 <= step <= 65535:
        raise ValueError(f"{path}: epoch spacing must be a positive whole number of days")
    for name, group in groups.items():
        for index, values in enumerate(group):
            if abs(values[0] - (first + index * step)) > 1e-7:
                raise ValueError(f"{path}: irregular epochs for {name}")
    return groups, first, int(step), count


def read_meeus(path, record, integer_columns, expected):
    rows = []
    with path.open(encoding="ascii", newline="") as source:
        reader = csv.reader(line for line in source if not line.startswith("#"))
        next(reader, None)
        for row in reader:
            if len(row) != len(record.unpack(bytes(record.size))):
                raise ValueError(f"{path}: unexpected column count")
            try:
                values = [int(value) for value in row[:integer_columns]]
                values += [int(value) if record.format[-1] == "i" else float(value)
                           for value in row[integer_columns:]]
            except ValueError as exc:
                raise ValueError(f"{path}: invalid coefficient") from exc
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"{path}: non-finite coefficient")
            try:
                rows.append(record.pack(*values))
            except struct.error as exc:
                raise ValueError(f"{path}: coefficient out of range") from exc
    if len(rows) != expected:
        raise ValueError(f"{path}: expected {expected} terms, got {len(rows)}")
    return rows


def build(data_path, output_path):
    groups, first, step, count = read_minor(data_path / "minor_planets.csv")
    body_series = []
    for planet, radius in zip(PLANETS, MIN_RADIUS_AU):
        series = read_vsop(data_path / "vsop87d" / f"VSOP87D.{planet}.gz")
        kept, error = select_terms(series, radius)
        count_terms = sum(len(terms) for axis in kept for terms in axis)
        print(f"{planet}: {count_terms} terms; sampled maximum error {error:.3f} arcsec",
              flush=True)
        body_series.append(kept)
    directory_count = len(PLANETS) * 3 * 6
    offset = HEADER.size + DIRECTORY.size * directory_count
    directory = bytearray()
    terms_blob = bytearray()
    for body in body_series:
        for axis in body:
            for series in axis:
                if len(series) > 65535:
                    raise ValueError("VSOP87D series exceeds resource format limits")
                directory.extend(DIRECTORY.pack(offset + len(terms_blob), len(series), 0))
                for term in series:
                    terms_blob.extend(TERM.pack(*term))
    minor_offset = offset + len(terms_blob)
    minor_blob = bytearray()
    for name in MINORS:
        for elements in groups[name]:
            minor_blob.extend(ELEMENTS.pack(*elements))
    meeus_offset = minor_offset + len(minor_blob)
    meeus_blob = bytearray()
    meeus_counts = []
    for name, record, integer_columns, expected in MEEUS_TABLES:
        rows = read_meeus(data_path / "meeus" / name, record, integer_columns, expected)
        meeus_counts.append(len(rows))
        for row in rows:
            meeus_blob.extend(row)
    header = HEADER.pack(b"ASEP", FORMAT_VERSION, len(PLANETS), 3, 6, len(MINORS), count,
                         step, 0, first, HEADER.size, minor_offset, meeus_offset,
                         *meeus_counts, 0)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + directory + terms_blob + minor_blob + meeus_blob)
    print(f"Built ephemeris resource ({output_path.stat().st_size} bytes): {output_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=DATA)
    parser.add_argument("--output", type=Path, default=ROOT / "resources/ephemeris.bin")
    args = parser.parse_args()
    try:
        build(args.data, args.output)
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.exit(1, f"ephemeris build failed: {exc}\n")


if __name__ == "__main__":
    main()
