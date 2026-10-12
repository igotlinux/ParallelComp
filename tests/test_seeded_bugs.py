"""
Unit tests for Seeded Bugs Detection Validation
"""

import unittest
import subprocess
import os


class TestSeededBugs(unittest.TestCase):
    def test_seeded_bugs_detection_rate(self):
        harness_bin = os.environ.get("KERNELGUARD_HARNESS_BIN", os.path.abspath("build/kg_harness"))
        if not os.path.exists(harness_bin):
            subprocess.run(["cmake", "--build", "build"], check=True)

        # Run seeded bugs harness: all bugs detected should return exit code 0
        cmd = [harness_bin, "--seeded-bugs", "--profile", "quick"]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        self.assertEqual(res.returncode, 0, f"Seeded bug runner failed: {res.stdout}\n{res.stderr}")
        self.assertIn("Detection Rate: 100.0%", res.stdout)
        self.assertIn("Missed Bugs:    0", res.stdout)


if __name__ == "__main__":
    unittest.main()
