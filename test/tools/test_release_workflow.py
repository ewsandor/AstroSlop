import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")


def job(name):
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [\w-]+:\n|\Z)", WORKFLOW)
    if not match:
        raise AssertionError(f"Missing job: {name}")
    return match.group(1)


class ReleaseWorkflowTests(unittest.TestCase):
    def test_release_version_matches_package_metadata(self):
        package = json.loads((ROOT / "package.json").read_text(encoding="utf-8"))
        lockfile = json.loads((ROOT / "package-lock.json").read_text(encoding="utf-8"))
        self.assertEqual(package["version"], "0.3.2")
        self.assertEqual(lockfile["version"], package["version"])
        self.assertEqual(lockfile["packages"][""]["version"], package["version"])

    def test_appstore_oidc_and_identity_are_configured_consistently(self):
        plan = job("appstore-preflight")
        publish = job("publish")
        self.assertIn("id-token: write", plan)
        self.assertIn("tools/appstore_preflight.py", plan)
        self.assertIn("APPSTORE_API_BASE: https://appstore-api.repebble.com", WORKFLOW)
        self.assertIn("needs: [build, release, store-screenshots]", plan)
        self.assertIn("name: appstore-preflight", plan)
        self.assertIn("if: false", publish)
        self.assertNotIn("isPublished=true", job("release-assets"))

    def test_store_listing_identity_matches_package(self):
        package = json.loads((ROOT / "package.json").read_text(encoding="utf-8"))
        pebble = package["pebble"]
        self.assertEqual(pebble["displayName"], "PushTo Tool")
        self.assertRegex(pebble["uuid"], r"(?i)^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$")

    def test_release_precedes_tests_and_store_work_is_gated(self):
        release = job("release")
        self.assertIn("needs: [build]", release)
        self.assertIn("github.event_name == 'push' && github.ref_type == 'tag'", release)
        self.assertIn("name: pbw-release", release)
        self.assertIn("files: dist/*.pbw", release)
        self.assertIn("generate_release_notes: true", release)
        self.assertIn("needs: [build, test, release]", job("comprehensive"))
        self.assertIn("needs: [build, test, release, comprehensive]", job("comprehensive-gate"))
        self.assertIn("needs: [build, release, comprehensive-gate]", job("store-screenshots"))
        self.assertIn("needs: [build, release, store-screenshots]", job("appstore-preflight"))
        for name in ("comprehensive", "store-screenshots", "appstore-preflight"):
            self.assertNotRegex(job(name), r"(?m)^    if: .*always\(\)")

    def test_release_assets_attach_reports_and_screenshots(self):
        release_assets = job("release-assets")
        self.assertIn("contents: write", release_assets)
        self.assertIn("pattern: comprehensive-release-*", release_assets)
        self.assertIn("name: comprehensive-report", release_assets)
        self.assertIn("pattern: store-screenshots-*", release_assets)
        self.assertIn("gh release upload", release_assets)
        self.assertIn("release-test-assets.zip", release_assets)

    def test_comprehensive_reports_are_uploaded_after_pass_or_failure(self):
        matrix = job("comprehensive")
        self.assertRegex(matrix, r"(?m)^      fail-fast: false$")
        self.assertRegex(matrix, r"(?m)^      - name: Ensure a final comprehensive report exists\n"
                         r"        if: always\(\)$")
        self.assertRegex(matrix, r"(?m)^      - name: Upload comprehensive reports and failure evidence\n"
                         r"        if: always\(\)$")
        self.assertIn("name: comprehensive-release-${{ matrix.platform }}", matrix)
        gate = job("comprehensive-gate")
        self.assertIn("if: always() && !cancelled()", gate)
        self.assertRegex(gate, r"(?m)^      - name: Upload final comprehensive gate report\n"
                         r"        if: always\(\)$")
        self.assertIn("name: comprehensive-report", gate)

    def test_gate_requires_each_prerequisite_and_writes_report(self):
        gate = job("comprehensive-gate")
        for name, dependency in (("BUILD", "build"), ("HOST", "test"),
                                 ("RELEASE", "release"), ("EMULATOR", "comprehensive")):
            self.assertIn(f"{name}: ${{{{ needs.{dependency}.result }}}}", gate)
        script = re.search(r"(?ms)^          python3 - <<'PY'\n(.*?)^          PY$", gate)
        self.assertIsNotNone(script)
        script = textwrap.dedent(script.group(1))
        names = ("BUILD", "HOST", "RELEASE", "EMULATOR")
        for failed in (None, *names):
            for result in (("success",) if failed is None else ("failure", "skipped", "cancelled")):
                checks = {name: result if name == failed else "success" for name in names}
                with self.subTest(failed=failed, result=result), tempfile.TemporaryDirectory() as directory:
                    process = subprocess.run([sys.executable, "-c", script], cwd=directory,
                                             env={**os.environ, **checks}, capture_output=True, text=True)
                    report = json.loads((Path(directory) / "comprehensive-report.json").read_text())
                    self.assertEqual(process.returncode == 0, failed is None)
                    self.assertEqual(report["success"], failed is None)
                    self.assertEqual(report["checks"], {key.lower(): value for key, value in checks.items()})


if __name__ == "__main__":
    unittest.main()
