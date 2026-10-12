"""
GCC Compiler Toolchain Adapter
"""

import shutil
import subprocess
from typing import List, Optional
from .base import ToolchainAdapter, CompileResult


class GccAdapter(ToolchainAdapter):
    def __init__(self, binary_cxx: str = "g++"):
        super().__init__("gcc")
        self.binary_cxx = shutil.which(binary_cxx) or binary_cxx

    def is_available(self) -> bool:
        if not shutil.which(self.binary_cxx):
            return False
        try:
            out = subprocess.check_output([self.binary_cxx, "--version"], text=True, stderr=subprocess.DEVNULL)
            # On macOS, /usr/bin/g++ is Apple Clang, not GNU GCC
            if "clang" in out.lower():
                return False
            return True
        except Exception:
            return False

    def get_version(self) -> str:
        if not self.is_available():
            return "GCC not found"
        try:
            out = subprocess.check_output([self.binary_cxx, "--version"], text=True, stderr=subprocess.DEVNULL)
            return out.splitlines()[0]
        except Exception:
            return "GCC (unknown version)"

    def compile_kernel(
        self,
        source_path: str,
        output_path: str,
        opt_level: str = "-O2",
        fp_mode: str = "strict",
        sanitizer: Optional[str] = None,
        extra_flags: Optional[List[str]] = None
    ) -> CompileResult:
        if not self.is_available():
            return CompileResult(
                success=False,
                command=[],
                stdout="",
                stderr="GCC binary not available",
                skipped=True,
                skip_reason="GCC compiler binary not installed or found in PATH",
            )

        cmd = [self.binary_cxx, "-std=c++17", "-Iinclude", "-c", source_path, "-o", output_path]
        cmd.append(opt_level)

        # Vectorization remarks
        cmd.extend(["-fopt-info-vec-optimized", "-fopt-info-vec-missed"])

        # FP mode flags
        if fp_mode == "strict":
            cmd.extend(["-fno-fast-math", "-fno-associative-math", "-ffp-contract=off"])
        elif fp_mode == "fast-math":
            cmd.append("-ffast-math")
        elif fp_mode == "contract-fast":
            cmd.append("-ffp-contract=fast")

        # Sanitizer
        if sanitizer:
            cmd.append(f"-fsanitize={sanitizer}")

        if extra_flags:
            cmd.extend(extra_flags)

        res = self.run_command(cmd)
        if res.success:
            res.output_artifact = output_path
        return res
