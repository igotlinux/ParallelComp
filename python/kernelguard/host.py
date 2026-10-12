"""
Hardware and system capability detector for KernelGuard
"""

import platform
import subprocess
import shutil
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class HostCapabilities:
    os_name: str
    machine: str
    has_avx2: bool = False
    has_avx512: bool = False
    has_neon: bool = False
    has_sse4: bool = False
    has_cuda: bool = False
    has_gpu: bool = False
    nvcc_path: Optional[str] = None
    cuda_version: Optional[str] = None
    gpu_names: List[str] = field(default_factory=list)
    available_c_compilers: List[str] = field(default_factory=list)
    available_cxx_compilers: List[str] = field(default_factory=list)


def detect_host() -> HostCapabilities:
    os_name = platform.system().lower()
    machine = platform.machine().lower()

    caps = HostCapabilities(
        os_name=os_name,
        machine=machine,
    )

    # 1. Detect CPU Vector Extensions
    if machine in ("arm64", "aarch64"):
        caps.has_neon = True
    elif machine in ("x86_64", "amd64"):
        if os_name == "linux":
            try:
                with open("/proc/cpuinfo", "r") as f:
                    content = f.read().lower()
                    caps.has_avx2 = "avx2" in content
                    caps.has_avx512 = "avx512f" in content
                    caps.has_sse4 = "sse4_1" in content or "sse4_2" in content
            except Exception:
                pass
        elif os_name == "darwin":
            try:
                out = subprocess.check_output(["sysctl", "-a"], text=True, stderr=subprocess.DEVNULL)
                caps.has_avx2 = "AVX2" in out
                caps.has_sse4 = "SSE4.1" in out or "SSE4_1" in out
            except Exception:
                pass

    # 2. Detect C/C++ Compilers
    for cc in ["gcc", "clang", "gcc-14", "gcc-13", "gcc-12", "gcc-15"]:
        p = shutil.which(cc)
        if p and p not in caps.available_c_compilers:
            caps.available_c_compilers.append(cc)

    for cxx in ["g++", "clang++", "g++-14", "g++-13", "g++-12", "g++-15"]:
        p = shutil.which(cxx)
        if p and p not in caps.available_cxx_compilers:
            caps.available_cxx_compilers.append(cxx)

    # 3. Detect CUDA & NVIDIA Hardware
    nvcc_bin = shutil.which("nvcc")
    if nvcc_bin:
        caps.has_cuda = True
        caps.nvcc_path = nvcc_bin
        try:
            ver_out = subprocess.check_output([nvcc_bin, "--version"], text=True, stderr=subprocess.DEVNULL)
            for line in ver_out.splitlines():
                if "release" in line:
                    caps.cuda_version = line.strip()
                    break
        except Exception:
            caps.cuda_version = "Unknown"

    smi_bin = shutil.which("nvidia-smi")
    if smi_bin:
        try:
            smi_out = subprocess.check_output(
                [smi_bin, "--query-gpu=name", "--format=csv,noheader"],
                text=True, stderr=subprocess.DEVNULL
            )
            gpus = [g.strip() for g in smi_out.strip().splitlines() if g.strip()]
            if gpus:
                caps.has_gpu = True
                caps.gpu_names = gpus
        except Exception:
            pass

    return caps
