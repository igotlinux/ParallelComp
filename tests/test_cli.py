"""
Unit tests for KernelGuard CLI commands and reports
"""

import unittest
import subprocess
import os
import json


class TestCli(unittest.TestCase):
    def test_cli_help(self):
        res = subprocess.run(["./kg", "--help"], stdout=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("KernelGuard: Production-Quality Compiler Verification Framework", res.stdout)

    def test_cli_report(self):
        res = subprocess.run(["./kg", "report"], stdout=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("KernelGuard Verification Run Report", res.stdout)

    def test_cli_report_json(self):
        res = subprocess.run(["./kg", "report", "--format", "json"], stdout=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0)
        data = json.loads(res.stdout)
        self.assertIn("total_tests", data)
        self.assertIn("results", data)


if __name__ == "__main__":
    unittest.main()
