"""
NVIDIA CUDA and PTX Compiler Toolchain Adapter
"""

import shutil
import subprocess
from typing import List, Optional
from .base import ToolchainAdapter, CompileResult


class CudaAdapter(ToolchainAdapter):
    def __init__(self, compiler: str = "nvcc", target_arch: str = "sm_75"):
        super().__init__("cuda")
        self.compiler_bin = shutil.which(compiler) or compiler
        self.target_arch = target_arch

    def is_available(self) -> bool:
        return shutil.which(self.compiler_bin) is not None

    def get_version(self) -> str:
        if not self.is_available():
            return "NVCC not found"
        try:
            out = subprocess.check_output([self.compiler_bin, "--version"], text=True)
            for line in out.splitlines():
                if "release" in line:
                    return line.strip()
            return out.splitlines()[0]
        except Exception:
            return "NVCC (unknown version)"

    def compile_ptx(
        self,
        cuda_source_path: str,
        ptx_output_path: str,
        target_arch: Optional[str] = None,
        extra_flags: Optional[List[str]] = None
    ) -> CompileResult:
        if not self.is_available():
            return CompileResult(
                success=False,
                command=[],
                stdout="",
                stderr="NVIDIA CUDA Compiler (nvcc) not found in PATH",
                skipped=True,
                skip_reason="NVIDIA CUDA Compiler (nvcc) is not installed or not found in system PATH. CUDA/PTX compilation requires NVIDIA CUDA Toolkit.",
            )

        arch = target_arch or self.target_arch
        cmd = [
            self.compiler_bin,
            "-ptx",
            f"-arch={arch}",
            "-O3",
            cuda_source_path,
            "-o",
            ptx_output_path,
        ]

        if extra_flags:
            cmd.extend(extra_flags)

        res = self.run_command(cmd)
        if res.success:
            res.output_artifact = ptx_output_path
        return res

    def compile_kernel(
        self,
        source_path: str,
        output_path: str,
        opt_level: str = "-O3",
        fp_mode: str = "strict",
        sanitizer: Optional[str] = None,
        extra_flags: Optional[List[str]] = None
    ) -> CompileResult:
        # For CUDA, the primary compiler artifact tested without hardware is PTX
        return self.compile_ptx(source_path, output_path, extra_flags=extra_flags)
