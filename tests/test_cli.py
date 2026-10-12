"""
Unit tests for KernelGuard CLI commands and reports
"""

import unittest
import subprocess
import os
import json
import tempfile
import shutil
import sys


class TestCli(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.report_file = os.path.join(self.test_dir, "kernelguard_report.json")
        mock_data = {
            "timestamp": "2026-10-12T00:00:00",
            "profile": "quick",
            "total_tests": 1,
            "passed_tests": 1,
            "failed_tests": 0,
            "skipped_tests": 0,
            "results": [
                {
                    "name": "vec_add",
                    "toolchain": "gcc",
                    "opt_level": "-O3",
                    "flags": "-O3",
                    "status": "PASS",
                    "simd_vectorized": True,
                    "details": "Verified contract"
                }
            ]
        }
        with open(self.report_file, "w", encoding="utf-8") as f:
            json.dump(mock_data, f)

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _get_env(self):
        env = dict(os.environ)
        repo_python = os.path.abspath("python")
        if "PYTHONPATH" in env:
            env["PYTHONPATH"] = f"{repo_python}:{env['PYTHONPATH']}"
        else:
            env["PYTHONPATH"] = repo_python
        return env

    def test_cli_help(self):
        res = subprocess.run(
            [sys.executable, "-m", "kernelguard.cli", "--help"],
            stdout=subprocess.PIPE,
            text=True,
            env=self._get_env()
        )
        self.assertEqual(res.returncode, 0)
        self.assertIn("KernelGuard: Production-Quality Compiler Verification Framework", res.stdout)

    def test_cli_report(self):
        res = subprocess.run(
            [sys.executable, "-m", "kernelguard.cli", "report", "--reports-dir", self.test_dir],
            stdout=subprocess.PIPE,
            text=True,
            env=self._get_env()
        )
        self.assertEqual(res.returncode, 0)
        self.assertIn("KernelGuard Verification Run Report", res.stdout)

    def test_cli_report_json(self):
        res = subprocess.run(
            [sys.executable, "-m", "kernelguard.cli", "report", "--format", "json", "--reports-dir", self.test_dir],
            stdout=subprocess.PIPE,
            text=True,
            env=self._get_env()
        )
        self.assertEqual(res.returncode, 0)
        data = json.loads(res.stdout)
        self.assertEqual(data.get("total_tests"), 1)
        self.assertEqual(data.get("passed_tests"), 1)


if __name__ == "__main__":
    unittest.main()
