import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run
import store_screenshots as store


class StoreScreenshotTests(unittest.TestCase):
    def test_watch_readiness_requires_a_response(self):
        emulator = SimpleNamespace(args=lambda action: ["pebble", action], env={},
                                   commands=io.StringIO(), output=Path("/tmp"))
        with patch.object(store.subprocess, "run",
                          return_value=SimpleNamespace(returncode=0, stdout="", stderr="")):
            with self.assertRaisesRegex(RuntimeError, "did not become ready"):
                store.wait_for_watch(emulator)
        with patch.object(store.subprocess, "run",
                          return_value=SimpleNamespace(returncode=0,
                                                       stdout="STORE_WATCH_READY\n", stderr="")):
            store.wait_for_watch(emulator)

    def test_scenes_resolve_to_distinct_catalog_objects(self):
        objects = run.catalogs()
        selected = [(catalog, store.object_index(objects, catalog, name))
                    for _, catalog, name, _, _ in store.SCENES]
        self.assertEqual(len(selected), len(set(selected)))
        with self.assertRaisesRegex(ValueError, "exactly one"):
            store.object_index(objects, 0, "not an object")

    def test_capture_validation_rejects_bad_state_and_weather(self):
        screen = ("0", "0", "0", "Jupiter", "157.2\u00b0", "59.0\u00b0")
        target = (0, 5, store.TIMESTAMP + 5)
        details = ("0", "0", "0", "12:00 AM", "51\u00b0F", "Set 8:00 AM", "-2.7", "")
        store.validate_capture(screen, target, details, None, 0, 5, "Jupiter", False, False)
        invalid = [
            (screen, (0, 6, target[2]), details),
            (screen, (0, 5, store.TIMESTAMP + 60), details),
            (screen, target, details[:4] + ("",) + details[5:]),
            (screen, target, details[:5] + ("",) + details[6:]),
            (screen, target, details[:2] + ("1",) + details[3:]),
            (screen[:4] + ("---.-\u00b0", screen[5]), target, details),
        ]
        for state, selection, info in invalid:
            with self.subTest(state=state, selection=selection, info=info):
                with self.assertRaises(AssertionError):
                    store.validate_capture(state, selection, info, None, 0, 5,
                                           "Jupiter", False, False)

    def test_scene_wait_restores_location_after_phone_error(self):
        class Emulator:
            def __init__(self):
                self.details = None
                self.messages = []

            def capture(self, values=None):
                self.messages.append(values)
                if values:
                    self.details = ("0", "0", "0", "", "", "", "", "")
                elif len(self.messages) == 1:
                    self.details = ("0", "0", "4", "", "", "", "", "")
                else:
                    self.details = ("0", "0", "0", "", "", "Set 8:00 AM", "", "")
                return (("0", "0", "0", "Jupiter", "157.2\u00b0", "59.0\u00b0"), None, None)

        emulator = Emulator()
        with patch.object(store.time, "sleep"):
            store.wait_for_scene(emulator, {"LOC_LAT_E6": 123, "WX_TEMP_DC": 106}, True)
        self.assertEqual(emulator.messages, [None, {"LOC_LAT_E6": 123}, None])

    def test_banner_notice_is_not_a_listing_screenshot(self):
        screen = ("1", "1", "0", "Saturn", "245.7\u00b0", "21.1\u00b0")
        target = (0, 6, store.TIMESTAMP + 5)
        details = ("0", "0", "0", "12:00 AM", "51\u00b0F", "", "1.2", "")
        store.validate_capture(screen, target, details, "", 0, 6, "Saturn", True, True)
        with self.assertRaisesRegex(AssertionError, "warning"):
            store.validate_capture(screen, target, details, "No objects", 0, 6,
                                   "Saturn", True, True)


if __name__ == "__main__":
    unittest.main()
