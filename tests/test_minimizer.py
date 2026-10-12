"""
Unit tests for Delta Debugging Minimizer and Reproducer Bundle Generator
"""

import unittest
import os
import shutil
from kernelguard.minimization.ddmin import ddmin
from kernelguard.minimization.reproducer import generate_reproducer_bundle, FailureBundleInfo


class TestMinimizer(unittest.TestCase):
    def test_ddmin_finds_single_bad_element(self):
        # Array of 100 elements, bad element is -999.0 at index 47
        test_array = [float(i) for i in range(100)]
        test_array[47] = -999.0

        def fails_if_contains_bad(sub):
            return -999.0 in sub

        minimized = ddmin(test_array, fails_if_contains_bad)
        self.assertEqual(minimized, [-999.0])

    def test_ddmin_finds_bad_pair(self):
        # Failure requires both 10.0 and 20.0 to be present together
        test_array = [float(i) for i in range(50)]

        def fails_if_both_present(sub):
            return (10.0 in sub) and (20.0 in sub)

        minimized = ddmin(test_array, fails_if_both_present)
        self.assertEqual(sorted(minimized), [10.0, 20.0])

    def test_generate_reproducer_bundle(self):
        test_dir = "build/test_repro_dir"
        shutil.rmtree(test_dir, ignore_errors=True)

        info = FailureBundleInfo(
            failure_id="test_failure_001",
            kernel_name="vec_add",
            failure_classification="MISMATCH",
            details="Numerical difference of 5 ULP at index 2",
            compiler_command=["g++", "-O3", "vec_add.cpp"],
            compiler_version="g++ 15.2.0",
            target_hardware="darwin arm64",
            failing_input=[1.0, 2.0, 3.0],
            expected_output=[2.0, 4.0, 6.0],
            actual_output=[2.0, 4.0, 6.0001],
            observed_ulp=5,
        )

        bundle_path = generate_reproducer_bundle(info, output_dir=test_dir)
        self.assertTrue(os.path.exists(os.path.join(bundle_path, "metadata.json")))
        self.assertTrue(os.path.exists(os.path.join(bundle_path, "repro.cpp")))
        self.assertTrue(os.path.exists(os.path.join(bundle_path, "build.sh")))
        self.assertTrue(os.path.exists(os.path.join(bundle_path, "README.md")))


if __name__ == "__main__":
    unittest.main()
