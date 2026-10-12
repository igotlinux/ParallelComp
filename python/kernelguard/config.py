"""
Configuration loader and validator for KernelGuard (kernelguard.yaml)
"""

import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any
import yaml


@dataclass
class ContractConfig:
    kind: str  # "exact", "ulp", "bound"
    max_ulp: int = 0
    max_rel_bound: float = 0.0
    max_abs_bound: float = 0.0


@dataclass
class ExpectedVectorization:
    at_opt_levels: List[str] = field(default_factory=lambda: ["-O2", "-O3", "-Ofast"])
    isa: str = "native"


@dataclass
class PtxAssertions:
    expected_instructions: List[str] = field(default_factory=list)
    forbidden_instructions: List[str] = field(default_factory=list)


@dataclass
class KernelConfig:
    name: str
    type: str  # "vec_add", "saxpy", "dot_product"
    source: str
    cuda_source: Optional[str] = None
    contract: ContractConfig = field(default_factory=lambda: ContractConfig(kind="exact"))
    expected_vectorization: ExpectedVectorization = field(default_factory=ExpectedVectorization)
    ptx_assertions: PtxAssertions = field(default_factory=PtxAssertions)


@dataclass
class ProfileConfig:
    name: str
    description: str
    opt_levels: List[str]
    fp_modes: List[str]
    sanitizers: List[Optional[str]]
    test_cases: str  # "quick" or "full"


@dataclass
class ToolchainConfig:
    name: str
    enabled: bool
    binary_c: Optional[str] = None
    binary_cxx: Optional[str] = None
    compiler: Optional[str] = None
    target_arch: Optional[str] = None
    flags: Dict[str, List[str]] = field(default_factory=dict)
    vector_remarks_flag: Optional[str] = None
    vector_missed_flag: Optional[str] = None
    ptx_flags: List[str] = field(default_factory=list)


@dataclass
class RegressionGates:
    vectorization_regression_fatal: bool = True
    max_instruction_count_increase_pct: float = 10.0
    wall_clock_is_advisory: bool = True


@dataclass
class KernelGuardConfig:
    version: int
    profiles: Dict[str, ProfileConfig]
    toolchains: Dict[str, ToolchainConfig]
    kernels: Dict[str, KernelConfig]
    regression_gates: RegressionGates


def load_config(config_path: str = "kernelguard.yaml") -> KernelGuardConfig:
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    with open(config_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    version = data.get("version", 1)

    profiles = {}
    for pname, pdata in data.get("profiles", {}).items():
        profiles[pname] = ProfileConfig(
            name=pname,
            description=pdata.get("description", ""),
            opt_levels=pdata.get("opt_levels", ["-O2"]),
            fp_modes=pdata.get("fp_modes", ["strict"]),
            sanitizers=pdata.get("sanitizers", [None]),
            test_cases=pdata.get("test_cases", "quick"),
        )

    toolchains = {}
    for tname, tdata in data.get("toolchains", {}).items():
        toolchains[tname] = ToolchainConfig(
            name=tname,
            enabled=tdata.get("enabled", True),
            binary_c=tdata.get("binary_c"),
            binary_cxx=tdata.get("binary_cxx"),
            compiler=tdata.get("compiler"),
            target_arch=tdata.get("target_arch"),
            flags=tdata.get("flags", {}),
            vector_remarks_flag=tdata.get("vector_remarks_flag"),
            vector_missed_flag=tdata.get("vector_missed_flag"),
            ptx_flags=tdata.get("ptx_flags", []),
        )

    kernels = {}
    for kname, kdata in data.get("kernels", {}).items():
        cdata = kdata.get("contract", {})
        contract = ContractConfig(
            kind=cdata.get("kind", "exact"),
            max_ulp=cdata.get("max_ulp", 0),
            max_rel_bound=cdata.get("max_rel_bound", 0.0),
            max_abs_bound=cdata.get("max_abs_bound", 0.0),
        )

        vdata = kdata.get("expected_vectorization", {})
        expected_vec = ExpectedVectorization(
            at_opt_levels=vdata.get("at_opt_levels", ["-O2", "-O3"]),
            isa=vdata.get("isa", "native"),
        )

        ptx_data = kdata.get("ptx_assertions", {})
        ptx_assert = PtxAssertions(
            expected_instructions=ptx_data.get("expected_instructions", []),
            forbidden_instructions=ptx_data.get("forbidden_instructions", []),
        )

        kernels[kname] = KernelConfig(
            name=kname,
            type=kdata.get("type", "vec_add"),
            source=kdata.get("source", ""),
            cuda_source=kdata.get("cuda_source"),
            contract=contract,
            expected_vectorization=expected_vec,
            ptx_assertions=ptx_assert,
        )

    gdata = data.get("regression_gates", {})
    gates = RegressionGates(
        vectorization_regression_fatal=gdata.get("vectorization_regression_fatal", True),
        max_instruction_count_increase_pct=gdata.get("max_instruction_count_increase_pct", 10.0),
        wall_clock_is_advisory=gdata.get("wall_clock_is_advisory", True),
    )

    return KernelGuardConfig(
        version=version,
        profiles=profiles,
        toolchains=toolchains,
        kernels=kernels,
        regression_gates=gates,
    )
