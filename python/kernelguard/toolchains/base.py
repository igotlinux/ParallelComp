"""
Base compiler toolchain adapter interface
"""

import abc
import subprocess
import time
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class CompileResult:
    success: bool
    command: List[str]
    stdout: str
    stderr: str
    output_artifact: Optional[str] = None
    compiler_version: str = "Unknown"
    time_taken_sec: float = 0.0
    skipped: bool = False
    skip_reason: Optional[str] = None


class ToolchainAdapter(abc.ABC):
    def __init__(self, name: str):
        self.name = name

    @abc.abstractmethod
    def is_available(self) -> bool:
        pass

    @abc.abstractmethod
    def get_version(self) -> str:
        pass

    @abc.abstractmethod
    def compile_kernel(
        self,
        source_path: str,
        output_path: str,
        opt_level: str,
        fp_mode: str,
        sanitizer: Optional[str] = None,
        extra_flags: Optional[List[str]] = None
    ) -> CompileResult:
        pass

    def run_command(self, cmd: List[str]) -> CompileResult:
        t0 = time.time()
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            elapsed = time.time() - t0
            return CompileResult(
                success=(res.returncode == 0),
                command=cmd,
                stdout=res.stdout,
                stderr=res.stderr,
                compiler_version=self.get_version(),
                time_taken_sec=elapsed,
            )
        except Exception as e:
            elapsed = time.time() - t0
            return CompileResult(
                success=False,
                command=cmd,
                stdout="",
                stderr=str(e),
                compiler_version=self.get_version(),
                time_taken_sec=elapsed,
            )
