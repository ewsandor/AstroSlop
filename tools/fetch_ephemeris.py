#!/usr/bin/env python3
"""Download the public VSOP87D series and JPL Horizons minor-body elements."""

import argparse
import csv
import gzip
import io
import urllib.parse
import urllib.request
from datetime import date, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "ephemeris"
VSOP_URL = "https://raw.githubusercontent.com/ctdk/vsop87/master/VSOP87D."
HORIZONS_URL = "https://ssd.jpl.nasa.gov/api/horizons.api"
PLANETS = ("mer", "ven", "ear", "mar", "jup", "sat", "ura", "nep")
MINORS = (("Vesta", "4;"), ("Juno", "3;"), ("Ceres", "1;"), ("Pallas", "2;"))


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def fetch_vsop():
    directory = DATA / "vsop87d"
    directory.mkdir(parents=True, exist_ok=True)
    for planet in PLANETS:
        print(f"Fetching VSOP87D {planet}...", flush=True)
        content = fetch(VSOP_URL + planet)
        if not content.startswith(b" VSOP87 VERSION D4"):
            raise ValueError(f"Unexpected VSOP87D file for {planet}")
        (directory / f"VSOP87D.{planet}.gz").write_bytes(gzip.compress(content, mtime=0))


def fetch_minor(name, command, start_year, end_year, step):
    stop = date(end_year + 1, 1, 1) + timedelta(days=step)
    params = {
        "format": "text", "COMMAND": f"'{command}'", "OBJ_DATA": "'NO'",
        "MAKE_EPHEM": "'YES'", "EPHEM_TYPE": "'ELEMENTS'", "CENTER": "'500@10'",
        "START_TIME": f"'{start_year}-01-01'", "STOP_TIME": f"'{stop.isoformat()}'",
        "STEP_SIZE": f"'{step} d'", "REF_PLANE": "'ECLIPTIC'",
        "REF_SYSTEM": "'J2000'", "OUT_UNITS": "'AU-D'", "CSV_FORMAT": "'YES'",
    }
    text = fetch(HORIZONS_URL + "?" + urllib.parse.urlencode(params)).decode("utf-8")
    if "$$SOE" not in text or "$$EOE" not in text:
        raise ValueError(f"JPL Horizons failed for {name}: {text[:500]}")
    table = text.split("$$SOE", 1)[1].split("$$EOE", 1)[0].strip()
    result = []
    for fields in csv.reader(io.StringIO(table), skipinitialspace=True):
        if len(fields) < 14:
            raise ValueError(f"Malformed JPL Horizons element row for {name}")
        result.append((name, float(fields[0]), float(fields[11]), float(fields[2]),
                       float(fields[4]), float(fields[5]), float(fields[6]),
                       float(fields[9])))
    if not result:
        raise ValueError(f"JPL Horizons returned no elements for {name}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start-year", type=int, default=2026)
    parser.add_argument("--end-year", type=int, default=2036)
    parser.add_argument("--epoch-step-days", type=int, default=60)
    args = parser.parse_args()
    if args.end_year < args.start_year or args.epoch_step_days < 1:
        parser.error("end year must not precede start year and step must be positive")
    fetch_vsop()
    rows = []
    for name, command in MINORS:
        print(f"Fetching JPL Horizons elements for {name}...", flush=True)
        rows.extend(fetch_minor(name, command, args.start_year, args.end_year,
                                args.epoch_step_days))
    path = DATA / "minor_planets.csv"
    with path.open("w", newline="", encoding="utf-8") as output:
        output.write("# JPL Horizons, heliocentric osculating elements, J2000 ecliptic\n")
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(("body", "jd_tdb", "a_au", "e", "incl_deg",
                         "node_deg", "peri_deg", "mean_deg"))
        writer.writerows(rows)
    print(f"Wrote {len(rows)} element sets to {path}")


if __name__ == "__main__":
    main()
