import csv
import gzip
import tempfile
import unittest
from pathlib import Path

from tools import build_ephemeris


class EphemerisBuilderTests(unittest.TestCase):
    def test_reads_series_and_bounds_dropped_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "vsop.gz"
            with gzip.open(path, "wt", encoding="ascii") as output:
                for axis in range(1, 4):
                    output.write(f" VSOP87 VERSION D4 EARTH VARIABLE {axis} "
                                 "(LBR) *T**0 2 TERMS\n")
                    output.write("0 1.0 0.0 0.0\n0 0.00000001 0.0 0.0\n")
                output.write(" VSOP87 VERSION D4 EARTH VARIABLE 1 (LBR) "
                             "*T**1 1 TERM\n0 0.1 0.0 0.0\n")
            series = build_ephemeris.read_vsop(path)
            kept, dropped = build_ephemeris.truncate(series, 1e-7)
            self.assertEqual(len(kept[0][0]), 1)
            self.assertEqual(len(dropped[0][0]), 1)
            self.assertEqual(len(kept[0][1]), 1)
            self.assertGreater(build_ephemeris.sampled_error(dropped, 1), 0)

    def test_rejects_irregular_minor_epochs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "minor.csv"
            fields = ("body", "jd_tdb", "a_au", "e", "incl_deg",
                      "node_deg", "peri_deg", "mean_deg")
            with path.open("w", newline="", encoding="utf-8") as output:
                writer = csv.writer(output)
                writer.writerow(fields)
                for body in build_ephemeris.MINORS:
                    writer.writerow((body, 2450000, 2, 0.1, 0, 0, 0, 0))
                    writer.writerow((body, 2450060 if body != "Ceres" else 2450061,
                                     2, 0.1, 0, 0, 0, 0))
            with self.assertRaisesRegex(ValueError, "irregular epochs"):
                build_ephemeris.read_minor(path)

    def test_meeus_table_count_and_range_are_validated(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "moon_latitude.csv"
            path.write_text("# comment\nd,m,mp,f,latitude_1e6_deg\n0,0,0,1,5128122\n",
                            encoding="ascii")
            rows = build_ephemeris.read_meeus(path, build_ephemeris.MOON_LATITUDE, 4, 1)
            self.assertEqual(build_ephemeris.MOON_LATITUDE.unpack(rows[0]),
                             (0, 0, 0, 1, 5128122))
            with self.assertRaisesRegex(ValueError, "expected 2 terms"):
                build_ephemeris.read_meeus(path, build_ephemeris.MOON_LATITUDE, 4, 2)
            path.write_text("d,m,mp,f,latitude_1e6_deg\n0,0,0,300,1\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "out of range"):
                build_ephemeris.read_meeus(path, build_ephemeris.MOON_LATITUDE, 4, 1)


if __name__ == "__main__":
    unittest.main()
