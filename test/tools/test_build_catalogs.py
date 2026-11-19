import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_catalogs", ROOT / "tools/build_catalogs.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)

HEADER = """\
# title: Test catalog
# short_title: Test
# provider: fixed
# source: Test source
# license: CC0-1.0
id,name,ra_deg,dec_deg,pm_ra_mas_yr,pm_dec_mas_yr,mag,type
"""


class CatalogBuilderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "catalogs.json").write_text(
            json.dumps({"catalogs": ["test.csv"]}), encoding="utf-8")
        self.catalog = self.root / "test.csv"

    def build(self, content):
        self.catalog.write_text(content, encoding="utf-8")
        output = self.root / "pack.bin"
        builder.build(self.root / "catalogs.json", output)
        return output.read_bytes()

    def test_round_trip_offsets_and_unknown_magnitude(self):
        pack = self.build(HEADER + "A,Alpha,12.5,-30,4,-5,,star\n")
        self.assertEqual(struct.unpack_from("<4sBBH", pack), (b"ASCP", 1, 1, 0))
        offset, length, provider, _, _ = struct.unpack_from("<IIBBH", pack, 8)
        self.assertEqual(provider, 0)
        self.assertEqual(offset + length, len(pack))
        count, size, records, strings, title, _ = struct.unpack_from(
            "<HHIIHH", pack, offset)
        self.assertEqual((count, size), (1, 20))
        record = struct.unpack_from("<iihhhBBHH", pack, offset + records)
        self.assertEqual(record[:7], (45000000, -108000000, 4, -5, -32768, 0, 0))
        table = pack[offset + strings:offset + length]
        self.assertEqual(table[title:].split(b"\0", 1)[0], b"Test catalog")
        self.assertEqual(table[record[7]:].split(b"\0", 1)[0], b"A")
        self.assertEqual(table[record[8]:].split(b"\0", 1)[0], b"Alpha")
        self.assertNotIn(b"Test source", table)

    def test_rejects_invalid_coordinates_and_duplicates(self):
        with self.assertRaisesRegex(ValueError, "ra_deg"):
            self.build(HEADER + "A,,361,0,,,,star\n")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.build(HEADER + "A,,1,0,,,,star\na,,2,0,,,,star\n")

    def test_requires_source_and_rejects_path_traversal(self):
        with self.assertRaisesRegex(ValueError, "missing source"):
            self.build(HEADER.replace("# source: Test source\n", "") +
                       "A,,1,0,,,,star\n")
        (self.root / "catalogs.json").write_text(
            json.dumps({"catalogs": ["../outside.csv"]}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "local CSV"):
            builder.build(self.root / "catalogs.json", self.root / "pack.bin")

    def test_registered_source_metadata_is_used_for_credits(self):
        self.catalog.write_text(HEADER + "A,,1,0,,,,star\n", encoding="utf-8")
        companion = self.root / "generated" / "credits.json"
        document = self.root / "CREDITS.md"
        builder.build(self.root / "catalogs.json", self.root / "pack.bin",
                      companion, document)
        self.assertEqual(json.loads(companion.read_text(encoding="utf-8")), [{
            "title": "Test catalog", "source": "Test source", "license": "CC0-1.0"
        }])
        self.assertIn("| Test catalog | Test source | CC0-1.0 |",
                      document.read_text(encoding="utf-8"))

    def test_rejects_strings_that_do_not_fit_watch_buffers(self):
        with self.assertRaisesRegex(ValueError, "ID or name exceeds"):
            self.build(HEADER + ("A" * 32) + ",,1,0,,,,star\n")
        with self.assertRaisesRegex(ValueError, "source exceeds"):
            self.build(HEADER.replace("Test source", "x" * 128) +
                       "A,,1,0,,,,star\n")


if __name__ == "__main__":
    unittest.main()
