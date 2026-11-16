#!/usr/bin/env python3
"""Import the MIT-licensed lunar and Pluto coefficient tables as ephemeris inputs."""

import csv
import re
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REVISION = "bfbd9ac2c7f709c94f1dee190c633d24a723f628"
BASE = f"https://raw.githubusercontent.com/soniakeys/meeus/{REVISION}/v3/"


def fetch(path):
    with urllib.request.urlopen(BASE + path, timeout=60) as response:
        return response.read().decode("utf-8")


def extract(source, start, count, columns):
    begin = source.find(start)
    if begin < 0:
        raise ValueError(f"Missing upstream coefficient table {start}")
    if start.endswith("[]struct"):
        begin = source.find("}{", begin + len(start))
        if begin >= 0:
            begin += 1
    else:
        begin = source.find("{", begin + len(start))
    end = source.find("\n}", begin)
    if begin < 0 or end < 0:
        raise ValueError(f"Incomplete upstream coefficient table {start}")
    rows = []
    for row in re.findall(r"\{([^{}]+)\}", source[begin:end]):
        values = [value.strip() for value in row.split(",")]
        if len(values) != columns:
            raise ValueError(f"Unexpected column count in {start}")
        for value in values:
            float(value)
        rows.append(values)
    if len(rows) != count:
        raise ValueError(f"Expected {count} rows in {start}, got {len(rows)}")
    return rows


def main():
    moon = fetch("moonposition/moonposition.go")
    pluto = fetch("pluto/pluto.go")
    tables = (
        ("moon_longitude.csv", ("d", "m", "mp", "f", "longitude_1e6_deg", "radius_m"),
         extract(moon, "var ta = [...]tas", 60, 6)),
        ("moon_latitude.csv", ("d", "m", "mp", "f", "latitude_1e6_deg"),
         extract(moon, "var tb = [...]tbs", 60, 5)),
        ("pluto.csv", ("j", "s", "p", "l_sin", "l_cos", "b_sin", "b_cos", "r_sin", "r_cos"),
         extract(pluto, "var t37 = []struct", 43, 9)),
    )
    directory = ROOT / "ephemeris/meeus"
    directory.mkdir(parents=True, exist_ok=True)
    for name, fields, rows in tables:
        path = directory / name
        with path.open("w", newline="", encoding="ascii") as output:
            output.write(f"# Generated from Sonia Keys' meeus revision {REVISION}.\n")
            output.write("# Copyright 2013, 2018 Sonia Keys; MIT license: "
                         "see LICENSES/soniakeys-meeus.txt.\n")
            writer = csv.writer(output, lineterminator="\n")
            writer.writerow(fields)
            writer.writerows(rows)
        print(f"Wrote {path}")

if __name__ == "__main__":
    main()
