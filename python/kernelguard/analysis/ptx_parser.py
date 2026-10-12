"""
NVIDIA PTX (Parallel Thread Execution) Parser and Inspection Engine
"""

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


@dataclass
class PtxInstruction:
    opcode: str          # e.g. "fma.rn.f32", "add.f32", "ld.global.f32"
    base_opcode: str     # e.g. "fma", "add", "ld"
    operands: List[str]  # e.g. ["%f3", "%f1", "%f2", "%f4"]
    raw_line: str


@dataclass
class PtxFunction:
    name: str
    is_entry: bool = True
    registers: Dict[str, int] = field(default_factory=dict)  # type -> count, e.g. {".f32": 16}
    instructions: List[PtxInstruction] = field(default_factory=list)
    instruction_counts: Dict[str, int] = field(default_factory=dict)


@dataclass
class PtxReport:
    version: Optional[str] = None
    target_arch: Optional[str] = None
    address_size: Optional[int] = None
    functions: Dict[str, PtxFunction] = field(default_factory=dict)
    total_instructions: int = 0
    instruction_histogram: Dict[str, int] = field(default_factory=dict)
    assertion_results: List[Tuple[str, bool, str]] = field(default_factory=list)  # (assertion, passed, reason)


class PtxParser:
    @staticmethod
    def parse_string(ptx_text: str) -> PtxReport:
        report = PtxReport()
        current_func: Optional[PtxFunction] = None

        lines = ptx_text.splitlines()

        for line in lines:
            line_strip = line.strip()

            # Ignore empty lines and comments
            if not line_strip or line_strip.startswith("//"):
                continue

            # Header directives
            if line_strip.startswith(".version"):
                m = re.search(r"\.version\s+([0-9.]+)", line_strip)
                if m: report.version = m.group(1)
            elif line_strip.startswith(".target"):
                m = re.search(r"\.target\s+([a-zA-Z0-9_,]+)", line_strip)
                if m: report.target_arch = m.group(1)
            elif line_strip.startswith(".address_size"):
                m = re.search(r"\.address_size\s+(\d+)", line_strip)
                if m: report.address_size = int(m.group(1))

            # Entry point or Function definition (handles .visible, .weak, .extern prefixes)
            elif ".entry" in line_strip or ".func" in line_strip:
                is_entry = ".entry" in line_strip
                m = re.search(r"(?:\.entry|\.func)\s+([a-zA-Z0-9_]+)", line_strip)
                if m:
                    fname = m.group(1)
                    current_func = PtxFunction(name=fname, is_entry=is_entry)
                    report.functions[fname] = current_func

            # Register allocations
            elif line_strip.startswith(".reg") and current_func:
                m = re.search(r"\.reg\s+(\.[a-zA-Z0-9]+)\s+([a-zA-Z0-9%_<>]+)", line_strip)
                if m:
                    reg_type = m.group(1)
                    reg_name = m.group(2)
                    # Check for count e.g. %f<32>
                    cm = re.search(r"<(\d+)>", reg_name)
                    count = int(cm.group(1)) if cm else 1
                    current_func.registers[reg_type] = current_func.registers.get(reg_type, 0) + count

            # Instruction lines inside a function
            elif current_func and (line_strip.endswith(";") or ";" in line_strip):
                # Clean labels and brackets
                code = line_strip.split(";")[0].strip()
                if not code or code.startswith("."):
                    continue

                # Tokenize: opcode operands
                tokens = re.split(r"\s+", code, maxsplit=1)
                opcode = tokens[0]
                base_opcode = opcode.split(".")[0]
                operands = []
                if len(tokens) > 1:
                    operands = [op.strip() for op in tokens[1].split(",") if op.strip()]

                insn = PtxInstruction(opcode=opcode, base_opcode=base_opcode, operands=operands, raw_line=line_strip)
                current_func.instructions.append(insn)

                # Update histograms with full opcode, base opcode, and all dotted prefixes
                parts = opcode.split(".")
                prefix = ""
                for part in parts:
                    prefix = part if not prefix else f"{prefix}.{part}"
                    current_func.instruction_counts[prefix] = current_func.instruction_counts.get(prefix, 0) + 1
                    report.instruction_histogram[prefix] = report.instruction_histogram.get(prefix, 0) + 1

                report.total_instructions += 1

        return report

    @staticmethod
    def parse_file(file_path: str) -> PtxReport:
        with open(file_path, "r", encoding="utf-8") as f:
            return PtxParser.parse_string(f.read())


def verify_ptx_assertions(
    report: PtxReport,
    expected_instructions: Optional[List[str]] = None,
    forbidden_instructions: Optional[List[str]] = None
) -> Tuple[bool, List[str]]:
    """
    Evaluates expected and forbidden instruction assertions against a parsed PTX report.
    Returns (all_passed, messages).
    """
    passed = True
    messages = []

    if expected_instructions:
        for expected in expected_instructions:
            count = report.instruction_histogram.get(expected, 0)
            if count == 0:
                # Also try partial matching on opcode (e.g. "fma" matching "fma.rn.f32")
                matches = [k for k, v in report.instruction_histogram.items() if expected in k and v > 0]
                if matches:
                    messages.append(f"Expected PTX instruction '{expected}' found as '{matches[0]}'")
                else:
                    passed = False
                    messages.append(f"FAIL: Expected PTX instruction '{expected}' was not generated in PTX output")
            else:
                messages.append(f"PASS: Expected PTX instruction '{expected}' present ({count} occurrences)")

    if forbidden_instructions:
        for forbidden in forbidden_instructions:
            count = report.instruction_histogram.get(forbidden, 0)
            if count > 0:
                passed = False
                messages.append(f"FAIL: Forbidden PTX instruction '{forbidden}' was found ({count} occurrences)")
            else:
                messages.append(f"PASS: Forbidden PTX instruction '{forbidden}' correctly absent")

    return passed, messages
