"""
Instruction Count and Performance Measurement Adapter
"""

import shutil
import subprocess
import os
import re
from dataclasses import dataclass
from typing import Optional


@dataclass
class InstructionCountResult:
    instruction_count: Optional[int]
    is_deterministic: bool
    tool_used: str  # "callgrind", "perf", "none"
    details: str


def measure_instruction_count(command: list, output_dir: str = "/tmp") -> InstructionCountResult:
    # 1. Check for Valgrind Callgrind
    valgrind = shutil.which("valgrind")
    if valgrind:
        out_file = os.path.join(output_dir, "callgrind.out")
        cmd = [valgrind, "--tool=callgrind", f"--callgrind-out-file={out_file}", "-q"] + command
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode == 0 and os.path.exists(out_file):
                # Parse callgrind output: "summary: <N>"
                with open(out_file, "r") as f:
                    for line in f:
                        if line.startswith("summary:"):
                            parts = line.split()
                            if len(parts) >= 2:
                                icount = int(parts[1])
                                return InstructionCountResult(
                                    instruction_count=icount,
                                    is_deterministic=True,
                                    tool_used="callgrind",
                                    details=f"Valgrind callgrind measured {icount} instructions deterministically",
                                )
        except Exception:
            pass

    # 2. Check for Linux perf
    perf = shutil.which("perf")
    if perf:
        cmd = [perf, "stat", "-e", "instructions"] + command
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            for line in res.stderr.splitlines():
                if "instructions" in line:
                    m = re.search(r"([\d,]+)\s+instructions", line)
                    if m:
                        icount = int(m.group(1).replace(",", ""))
                        return InstructionCountResult(
                            instruction_count=icount,
                            is_deterministic=False,  # Hardware perf counters include OS interrupts/noise
                            tool_used="perf",
                            details=f"Linux perf hardware counter measured {icount} instructions (advisory)",
                        )
        except Exception:
            pass

    # 3. Fallback when neither callgrind nor perf is available
    return InstructionCountResult(
        instruction_count=None,
        is_deterministic=False,
        tool_used="none",
        details="Deterministic instruction-counting tool (Valgrind/Callgrind) unavailable on this host. Wall-clock timing remains advisory.",
    )
