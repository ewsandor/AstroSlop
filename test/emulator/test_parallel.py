import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import parallel


class ParallelTests(unittest.TestCase):
    def test_worker_isolates_state_and_forwards_selection(self):
        args = argparse.Namespace(tier="comprehensive", case=["horizons-00?"],
                                  group=["weather"])
        command = parallel.worker_command(Path("/workspace"), Path("/home"), Path("/results"),
                                          Path("/sdk"), "chalk", "debug", args)
        self.assertIn("--net", command)
        self.assertIn("--unshare-pid", command)
        self.assertIn("--die-with-parent", command)
        self.assertIn("--ro-bind", command)
        self.assertEqual(command[-4:], ["--case", "horizons-00?", "--group", "weather"])
        self.assertIn("/tmp/workspace/build/parallel-packages/debug.pbw", command)
        self.assertIn("AppRunStateRequest", " ".join(command))
        self.assertNotIn("pebble ping", " ".join(command))

    def test_worker_requires_a_successful_summary(self):
        args = argparse.Namespace(tier="sanity", case=[], group=[])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            base.mkdir()
            scratch = root / "scratch"
            output = root / "output"
            result_path = output / "workers/release/emery"

            def execute(command, stdout, stderr):
                (result_path / "summary.json").write_text(json.dumps({"success": True}))
                return subprocess.CompletedProcess(command, 0)

            with patch.object(parallel.subprocess, "run", side_effect=execute):
                result = parallel.run_worker(base, scratch, output, Path("/sdk"), "4.44",
                                             "release", "emery", args)
            self.assertTrue(result["success"])
            self.assertEqual(result["summary"], "workers/release/emery/summary.json")

            with patch.object(parallel.subprocess, "run",
                              return_value=subprocess.CompletedProcess([], 1)):
                failed = parallel.run_worker(base, scratch, output, Path("/sdk"), "4.44",
                                             "release", "chalk", args)
            self.assertFalse(failed["success"])
            self.assertIn("Worker exited 1", failed["error"])

            with patch.object(parallel.subprocess, "run",
                              return_value=subprocess.CompletedProcess([], 0)):
                missing = parallel.run_worker(base, scratch, output, Path("/sdk"), "4.44",
                                              "release", "flint", args)
            self.assertFalse(missing["success"])
            self.assertIn("without a summary", missing["error"])

            def reported_failure(command, stdout, stderr):
                (output / "workers/release/gabbro/summary.json").write_text(
                    json.dumps({"success": False}))
                return subprocess.CompletedProcess(command, 0)

            with patch.object(parallel.subprocess, "run", side_effect=reported_failure):
                failed_summary = parallel.run_worker(base, scratch, output, Path("/sdk"), "4.44",
                                                     "release", "gabbro", args)
            self.assertFalse(failed_summary["success"])

    def test_workspace_excludes_build_and_current_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            (source / "build").mkdir(parents=True)
            (source / "reports" / "current").mkdir(parents=True)
            (source / "src").mkdir()
            (source / "src" / "main.c").write_text("source")
            parallel.copy_workspace(source, root / "copy", source / "reports/current",
                                    exclude_build=True)
            self.assertFalse((root / "copy/build").exists())
            self.assertFalse((root / "copy/reports/current").exists())
            self.assertEqual((root / "copy/src/main.c").read_text(), "source")


if __name__ == "__main__":
    unittest.main()
