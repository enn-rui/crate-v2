from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import analyze_all
from _common import crate_dir, resolve_root


class AnalysisCliTest(unittest.TestCase):
    def test_out_is_forwarded_to_every_step(self):
        with tempfile.TemporaryDirectory() as temp:
            root = str(Path(temp) / "music")
            out = str(Path(temp) / "sidecars")
            with mock.patch("analyze_all.subprocess.run") as run:
                run.return_value.returncode = 0
                self.assertEqual(analyze_all.main(["--root", root, "--out", out]), 0)
            self.assertEqual(len(run.call_args_list), len(analyze_all.STEPS))
            for call in run.call_args_list:
                command = call.args[0]
                self.assertEqual(command[command.index("--root") + 1], root)
                self.assertEqual(command[command.index("--out") + 1], out)

    def test_default_and_custom_sidecar_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "music"
            custom = Path(temp) / "elsewhere"
            self.assertEqual(crate_dir(root), (root / ".crate").resolve())
            self.assertEqual(crate_dir(root, custom), custom.resolve())

    def test_missing_root_fails_with_exact_command(self):
        with self.assertRaisesRegex(SystemExit, "python analyze_all.py --root"):
            resolve_root()


if __name__ == "__main__":
    unittest.main()
