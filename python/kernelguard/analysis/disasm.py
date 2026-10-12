"""
Disassembly and SIMD Vector Instruction Analyzer
"""

import shutil
import subprocess
import re
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class DisasmReport:
    has_vector_instructions: bool = False
    total_instructions: int = 0
    vector_instructions: int = 0
    vector_ratio: float = 0.0
    detected_vector_instructions: List[str] = field(default_factory=list)
    raw_disassembly: str = ""


# Common SIMD opcodes and register patterns
X86_SIMD_PATTERNS = [
    re.compile(r"\b(vaddps|vsubps|vmulps|vdivps|vfmadd\w+|vfmsub\w+)\b", re.IGNORECASE),
    re.compile(r"\b(addps|subps|mulps|divps)\b", re.IGNORECASE),
    re.compile(r"\b(vmovups|vmovaps|movups|movaps)\b", re.IGNORECASE),
    re.compile(r"%\b(ymm\d+|zmm\d+)\b", re.IGNORECASE),
]

ARM64_NEON_PATTERNS = [
    re.compile(r"\b(fadd|fsub|fmul|fdiv|fmla|fmls)\s+v\d+\.(4s|2s|2d|4h|8h|16b)\b", re.IGNORECASE),
    re.compile(r"\b(ld1|st1)\s+\{v\d+\.(4s|2d)\}", re.IGNORECASE),
    re.compile(r"\bv\d+\.(4s|2d)\b", re.IGNORECASE),
]


def disassemble_binary(object_path: str, symbol_filter: Optional[str] = None) -> DisasmReport:
    # Try objdump, llvm-objdump, or otool
    disassembler = shutil.which("objdump") or shutil.which("llvm-objdump")
    cmd = []

    if disassembler:
        cmd = [disassembler, "-d", object_path]
    elif shutil.which("otool"):
        cmd = ["otool", "-tV", object_path]
    else:
        return DisasmReport(raw_disassembly="No disassembler (objdump/otool) available")

    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        raw_asm = proc.stdout
    except Exception as e:
        return DisasmReport(raw_disassembly=f"Disassembly failed: {e}")

    report = DisasmReport(raw_disassembly=raw_asm)

    # Filter by symbol if specified
    lines = raw_asm.splitlines()
    in_symbol = True if not symbol_filter else False
    active_lines = []

    for line in lines:
        if symbol_filter and symbol_filter in line:
            in_symbol = True
        elif in_symbol and line.endswith(":") and symbol_filter and symbol_filter not in line:
            # End of symbol section
            in_symbol = False

        if in_symbol:
            active_lines.append(line)

    total_insns = 0
    vec_insns = 0
    detected_vec = []

    for line in active_lines:
        line_strip = line.strip()
        # Look for instruction lines (typically contain hex address or tab separator)
        if not line_strip or line_strip.endswith(":"):
            continue

        total_insns += 1
        is_vector = False

        for pat in X86_SIMD_PATTERNS + ARM64_NEON_PATTERNS:
            m = pat.search(line_strip)
            if m:
                is_vector = True
                matched_token = m.group(0)
                if matched_token not in detected_vec:
                    detected_vec.append(matched_token)
                break

        if is_vector:
            vec_insns += 1

    report.total_instructions = total_insns
    report.vector_instructions = vec_insns
    report.has_vector_instructions = (vec_insns > 0)
    report.vector_ratio = (vec_insns / total_insns) if total_insns > 0 else 0.0
    report.detected_vector_instructions = detected_vec

    return report
