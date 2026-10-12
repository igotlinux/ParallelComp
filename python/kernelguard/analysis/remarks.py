"""
Compiler Optimization Remarks Parser for GCC and Clang
"""

import re
from dataclasses import dataclass, field
from typing import List


@dataclass
class VectorizationRemarks:
    vectorized: bool = False
    vector_width: int = 0
    interleave_count: int = 1
    optimized_remarks: List[str] = field(default_factory=list)
    missed_remarks: List[str] = field(default_factory=list)
    analysis_remarks: List[str] = field(default_factory=list)


def parse_gcc_remarks(stderr_text: str) -> VectorizationRemarks:
    remarks = VectorizationRemarks()

    for line in stderr_text.splitlines():
        line_clean = line.strip()
        if "optimized: loop vectorized" in line_clean or "optimized:  loop vectorized" in line_clean:
            remarks.vectorized = True
            remarks.optimized_remarks.append(line_clean)
            # Try to match vector byte width: e.g. "using 32 byte vectors" -> width = 8 floats
            m = re.search(r"using (\d+) byte vectors", line_clean)
            if m:
                bytes_wide = int(m.group(1))
                remarks.vector_width = bytes_wide // 4
        elif "optimized: " in line_clean:
            remarks.optimized_remarks.append(line_clean)
        elif "missed: " in line_clean:
            remarks.missed_remarks.append(line_clean)

    return remarks


def parse_clang_remarks(stderr_text: str) -> VectorizationRemarks:
    remarks = VectorizationRemarks()

    for line in stderr_text.splitlines():
        line_clean = line.strip()
        if "remark: vectorized loop" in line_clean:
            remarks.vectorized = True
            remarks.optimized_remarks.append(line_clean)

            # Width match: e.g. "vectorization width: 8, interleaved count: 2"
            mw = re.search(r"vectorization width:\s*(\d+)", line_clean)
            if mw:
                remarks.vector_width = int(mw.group(1))
            mi = re.search(r"interleaved count:\s*(\d+)", line_clean)
            if mi:
                remarks.interleave_count = int(mi.group(1))

        elif "remark: loop not vectorized" in line_clean or "remark: not vectorized" in line_clean:
            remarks.missed_remarks.append(line_clean)
        elif "remark: " in line_clean:
            remarks.analysis_remarks.append(line_clean)

    return remarks


def parse_compiler_remarks(compiler_name: str, stderr_text: str) -> VectorizationRemarks:
    if "clang" in compiler_name.lower():
        return parse_clang_remarks(stderr_text)
    return parse_gcc_remarks(stderr_text)
