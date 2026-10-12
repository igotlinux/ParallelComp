"""
Unit tests for Compiler Remarks and Disassembly Parsers
"""

import unittest
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "python")))

from kernelguard.analysis.remarks import parse_gcc_remarks, parse_clang_remarks, parse_compiler_remarks
from kernelguard.analysis.disasm import DisasmReport


SAMPLE_GCC_REMARKS = """
src/kernels/cpu/vec_add.cpp:16:20: optimized: loop vectorized using 32 byte vectors
src/kernels/cpu/vec_add.cpp:30:5: missed: statement clobbers memory: vst1q_f32
"""

SAMPLE_CLANG_REMARKS = """
src/kernels/cpu/saxpy.cpp:25:5: remark: vectorized loop (vectorization width: 4, interleaved count: 2) [-Rpass=loop-vectorize]
src/kernels/cpu/saxpy.cpp:40:5: remark: loop not vectorized: could not determine number of loop iterations [-Rpass-missed=loop-vectorize]
"""


class TestRemarksParser(unittest.TestCase):
    def test_parse_gcc_remarks(self):
        res = parse_gcc_remarks(SAMPLE_GCC_REMARKS)
        self.assertTrue(res.vectorized)
        self.assertEqual(res.vector_width, 8)  # 32 bytes // 4 = 8 floats
        self.assertEqual(len(res.optimized_remarks), 1)
        self.assertEqual(len(res.missed_remarks), 1)

    def test_parse_clang_remarks(self):
        res = parse_clang_remarks(SAMPLE_CLANG_REMARKS)
        self.assertTrue(res.vectorized)
        self.assertEqual(res.vector_width, 4)
        self.assertEqual(res.interleave_count, 2)
        self.assertEqual(len(res.missed_remarks), 1)

    def test_parse_compiler_remarks_dispatch(self):
        res_gcc = parse_compiler_remarks("gcc", SAMPLE_GCC_REMARKS)
        self.assertTrue(res_gcc.vectorized)

        res_clang = parse_compiler_remarks("clang++", SAMPLE_CLANG_REMARKS)
        self.assertTrue(res_clang.vectorized)


if __name__ == "__main__":
    unittest.main()
