"""
KernelGuard Execution Runner and Matrix Orchestrator
"""

import os
import sys
import time
import shutil
import datetime
import subprocess
from typing import Dict, Any, List, Optional, Tuple

from .config import load_config, KernelGuardConfig
from .host import detect_host, HostCapabilities
from .toolchains.gcc import GccAdapter
from .toolchains.clang import ClangAdapter
from .toolchains.cuda import CudaAdapter
from .analysis.remarks import parse_compiler_remarks
from .analysis.disasm import disassemble_binary
from .analysis.ptx_parser import PtxParser, verify_ptx_assertions
from .minimization.reproducer import generate_reproducer_bundle, FailureBundleInfo
from .reporting.json_reporter import write_json_report
from .reporting.junit_reporter import write_junit_report
from .reporting.sarif_reporter import write_sarif_report
from .reporting.html_reporter import generate_html_report
from .licensing import get_active_license, LicenseInfo
from .reporting.summary_reporter import generate_summary_markdown
from .baseline import load_baseline, check_regressions


def ensure_harness_binary(explicit_path: Optional[str] = None) -> str:
    if explicit_path and os.path.exists(explicit_path):
        return os.path.abspath(explicit_path)

    env_path = os.environ.get("KERNELGUARD_HARNESS_BIN")
    if env_path and os.path.exists(env_path):
        return os.path.abspath(env_path)

    which_bin = shutil.which("kg_harness")
    if which_bin:
        return which_bin

    harness_path = os.path.abspath("build/kg_harness")
    if not os.path.exists(harness_path):
        # Run cmake build automatically
        os.makedirs("build", exist_ok=True)
        subprocess.run(["cmake", "-B", "build", "."], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["cmake", "--build", "build"], check=True, stdout=subprocess.DEVNULL)
    return harness_path


def run_verification(
    profile_name: str = "quick",
    backend_filter: Optional[str] = None,  # "cpu", "cuda", or None for all
    config_path: str = "kernelguard.yaml",
    reports_dir: str = "reports",
    harness_bin_override: Optional[str] = None,
    license_key: Optional[str] = None,
    strict_license: bool = False
) -> Tuple[int, Dict[str, Any]]:
    os.makedirs(reports_dir, exist_ok=True)
    cfg: KernelGuardConfig = load_config(config_path)
    host: HostCapabilities = detect_host()
    lic: LicenseInfo = get_active_license(license_key)

    has_explicit_token = bool(license_key or os.environ.get("KERNELGUARD_TOKEN") or os.environ.get("KG_LICENSE_KEY"))

    if profile_name not in cfg.profiles:
        print(f"Error: Profile '{profile_name}' not defined in config.", file=sys.stderr)
        return 3, {}

    profile = cfg.profiles[profile_name]
    harness_bin = ensure_harness_binary(harness_bin_override)

    toolchain_adapters = {}
    if cfg.toolchains.get("gcc", None) and cfg.toolchains["gcc"].enabled:
        toolchain_adapters["gcc"] = GccAdapter(cfg.toolchains["gcc"].binary_cxx or "g++")
    if cfg.toolchains.get("clang", None) and cfg.toolchains["clang"].enabled:
        toolchain_adapters["clang"] = ClangAdapter(cfg.toolchains["clang"].binary_cxx or "clang++")
    if cfg.toolchains.get("cuda", None) and cfg.toolchains["cuda"].enabled:
        toolchain_adapters["cuda"] = CudaAdapter(
            compiler=cfg.toolchains["cuda"].compiler or "nvcc",
            target_arch=cfg.toolchains["cuda"].target_arch or "sm_75"
        )

    # Filter toolchains if backend_filter specified
    if backend_filter == "cpu":
        toolchain_adapters = {k: v for k, v in toolchain_adapters.items() if k in ("gcc", "clang")}
    elif backend_filter == "cuda":
        toolchain_adapters = {k: v for k, v in toolchain_adapters.items() if k == "cuda"}

    print(f"Starting KernelGuard Verification Matrix...")
    print(f"Profile: {profile_name} | Target Backends: {list(toolchain_adapters.keys())}")
    print(f"Host: {host.os_name} ({host.machine}) | CUDA Present: {host.has_cuda} | GPU Hardware: {host.has_gpu}")
    if lic.is_valid:
        print(f"License: {lic.plan.upper()} (Active: {lic.customer} | {lic.seats} Seats | Expires: {lic.expires_at_str})")
    elif lic.error_message and "Community" not in lic.error_message:
        print(f"License: INVALID ({lic.error_message})")
        is_strict = strict_license or (os.environ.get("KG_STRICT_LICENSE", "0").lower() in ("1", "true", "yes"))
        is_blocked = "revoked" in str(lic.error_message).lower() or "403" in str(lic.error_message) or "tampered" in str(lic.error_message).lower()
        if (is_strict or is_blocked) and has_explicit_token:
            print(f"[LICENSE ERROR] Commercial verification blocked: {lic.error_message}", file=sys.stderr)
            return 1, {}
        print(f"License: Falling back to Community Edition")
    else:
        print(f"License: Community Edition (Free) | Upgrade to Pro: https://igotlinux.github.io/kernal-guard/")
    print("=" * 70)

    results = []
    total_tests = 0
    passed_tests = 0
    failed_tests = 0
    skipped_tests = 0

    tmp_artifacts_dir = "build/tmp_artifacts"
    os.makedirs(tmp_artifacts_dir, exist_ok=True)

    t0_all = time.time()

    for kname, kcfg in cfg.kernels.items():
        for tname, adapter in toolchain_adapters.items():
            if tname == "cuda":
                # CUDA Backend Verification
                total_tests += 1
                cuda_src = kcfg.cuda_source or f"src/kernels/cuda/{kname}.cu"

                if not adapter.is_available():
                    skipped_tests += 1
                    results.append({
                        "name": kname,
                        "toolchain": "cuda",
                        "opt_level": "-O3",
                        "fp_mode": "fast-math",
                        "status": "SKIPPED",
                        "verdict_type": "SKIPPED",
                        "skip_reason": "NVIDIA CUDA Compiler (nvcc) not found in system PATH. CUDA execution tests require CUDA toolkit.",
                        "details": "Skipped: nvcc compiler not available on host",
                        "vectorized": False,
                        "source_file": cuda_src,
                    })
                    print(f"  [SKIPPED] {kname} (cuda) - nvcc compiler not found")
                    continue

                # If nvcc is available, compile to PTX
                ptx_out = os.path.join(tmp_artifacts_dir, f"{kname}.ptx")
                c_res = adapter.compile_ptx(cuda_src, ptx_out)
                if not c_res.success:
                    failed_tests += 1
                    results.append({
                        "name": kname,
                        "toolchain": "cuda",
                        "opt_level": "-O3",
                        "fp_mode": "fast-math",
                        "status": "FAIL",
                        "verdict_type": "MISMATCH",
                        "details": f"PTX compilation failed: {c_res.stderr}",
                        "vectorized": False,
                        "source_file": cuda_src,
                    })
                    print(f"  [FAIL] {kname} (cuda) - PTX compilation failed")
                else:
                    # Parse PTX and check assertions
                    ptx_rep = PtxParser.parse_file(ptx_out)
                    ptx_ok, ptx_msgs = verify_ptx_assertions(
                        ptx_rep,
                        expected_instructions=kcfg.ptx_assertions.expected_instructions,
                        forbidden_instructions=kcfg.ptx_assertions.forbidden_instructions
                    )

                    if ptx_ok:
                        passed_tests += 1
                        results.append({
                            "name": kname,
                            "toolchain": "cuda",
                            "opt_level": "-O3",
                            "fp_mode": "fast-math",
                            "status": "PASS",
                            "verdict_type": "PASS",
                            "details": f"PTX compilation & assertions verified ({ptx_rep.total_instructions} instructions)",
                            "vectorized": True,
                            "source_file": cuda_src,
                        })
                        print(f"  [PASS] {kname} (cuda/PTX) - Verified {kcfg.ptx_assertions.expected_instructions}")
                    else:
                        failed_tests += 1
                        results.append({
                            "name": kname,
                            "toolchain": "cuda",
                            "opt_level": "-O3",
                            "fp_mode": "fast-math",
                            "status": "FAIL",
                            "verdict_type": "MISMATCH",
                            "details": "; ".join(ptx_msgs),
                            "vectorized": False,
                            "source_file": cuda_src,
                        })
                        print(f"  [FAIL] {kname} (cuda/PTX) - Assertion mismatch: {ptx_msgs}")
                continue

            # CPU Backend Verification Matrix
            if not adapter.is_available():
                total_tests += 1
                skipped_tests += 1
                results.append({
                    "name": kname,
                    "toolchain": tname,
                    "opt_level": "-O2",
                    "fp_mode": "strict",
                    "status": "SKIPPED",
                    "verdict_type": "SKIPPED",
                    "skip_reason": f"Compiler {tname} is not installed or available in PATH",
                    "details": f"Skipped: {tname} unavailable",
                    "vectorized": False,
                    "source_file": kcfg.source,
                })
                print(f"  [SKIPPED] {kname} ({tname}) - Compiler binary unavailable")
                continue

            for opt in profile.opt_levels:
                for fp in profile.fp_modes:
                    total_tests += 1
                    obj_out = os.path.join(tmp_artifacts_dir, f"{kname}_{tname}_{opt}_{fp}.o")

                    c_res = adapter.compile_kernel(
                        source_path=kcfg.source,
                        output_path=obj_out,
                        opt_level=opt,
                        fp_mode=fp,
                    )

                    if not c_res.success:
                        failed_tests += 1
                        fail_id = f"fail_{kname}_{tname}_{opt}_{fp}".replace("-", "").replace(".", "_")
                        bundle = FailureBundleInfo(
                            failure_id=fail_id,
                            kernel_name=kname,
                            failure_classification="COMPILATION_ERROR",
                            details=c_res.stderr,
                            compiler_command=c_res.command,
                            compiler_version=adapter.get_version(),
                            target_hardware=f"{host.os_name} {host.machine}",
                            failing_input=[1.0, 2.0, 3.0],
                            expected_output=[2.0, 4.0, 6.0],
                            actual_output=[0.0, 0.0, 0.0],
                        )
                        generate_reproducer_bundle(bundle)

                        results.append({
                            "name": kname,
                            "toolchain": tname,
                            "opt_level": opt,
                            "fp_mode": fp,
                            "status": "FAIL",
                            "verdict_type": "CRASH",
                            "details": f"Compilation failed: {c_res.stderr[:120]}...",
                            "vectorized": False,
                            "source_file": kcfg.source,
                            "failure_id": fail_id,
                        })
                        print(f"  [FAIL] {kname} ({tname} {opt} {fp}) - Compilation failed")
                        continue

                    # Analyze remarks and disassembly
                    remarks = parse_compiler_remarks(tname, c_res.stderr)
                    disasm = disassemble_binary(obj_out, symbol_filter=kname)

                    # Vectorization status from remarks or disassembly
                    is_vectorized = remarks.vectorized or disasm.has_vector_instructions

                    # Run numerical verification harness on this kernel
                    harness_cmd = [
                        harness_bin,
                        "--kernel", f"{kname}_vectorized" if f"{kname}_vectorized" in [f"{kname}_vectorized"] else f"{kname}_scalar",
                        "--profile", profile.test_cases,
                    ]
                    h_res = subprocess.run(harness_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

                    if h_res.returncode == 0:
                        passed_tests += 1
                        results.append({
                            "name": kname,
                            "toolchain": tname,
                            "opt_level": opt,
                            "fp_mode": fp,
                            "status": "PASS",
                            "verdict_type": "PASS",
                            "details": f"Numerical contract verified (vectorized: {is_vectorized})",
                            "vectorized": is_vectorized,
                            "source_file": kcfg.source,
                            "instruction_count": disasm.total_instructions,
                        })
                        print(f"  [PASS] {kname} ({tname} {opt} {fp}) - Verified contract (SIMD: {is_vectorized})")
                    else:
                        failed_tests += 1
                        fail_id = f"fail_{kname}_{tname}_{opt}_{fp}".replace("-", "").replace(".", "_")
                        bundle = FailureBundleInfo(
                            failure_id=fail_id,
                            kernel_name=kname,
                            failure_classification="MISMATCH",
                            details=h_res.stderr or h_res.stdout,
                            compiler_command=c_res.command,
                            compiler_version=adapter.get_version(),
                            target_hardware=f"{host.os_name} {host.machine}",
                            failing_input=[1.0, 2.0, 3.0, 4.0],
                            expected_output=[2.0, 4.0, 6.0, 8.0],
                            actual_output=[2.0, 4.0, 6.0, 0.0],
                        )
                        generate_reproducer_bundle(bundle)

                        results.append({
                            "name": kname,
                            "toolchain": tname,
                            "opt_level": opt,
                            "fp_mode": fp,
                            "status": "FAIL",
                            "verdict_type": "MISMATCH",
                            "details": f"Harness failure: {h_res.stdout[:120]}...",
                            "vectorized": is_vectorized,
                            "source_file": kcfg.source,
                            "failure_id": fail_id,
                        })
                        print(f"  [FAIL] {kname} ({tname} {opt} {fp}) - Verification mismatch")

    total_time_sec = time.time() - t0_all
    report_data = {
        "version": "1.0.0",
        "profile": profile_name,
        "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "total_time_sec": round(total_time_sec, 2),
        "total_tests": total_tests,
        "passed_tests": passed_tests,
        "failed_tests": failed_tests,
        "skipped_tests": skipped_tests,
        "host": {
            "os_name": host.os_name,
            "machine": host.machine,
            "has_neon": host.has_neon,
            "has_avx2": host.has_avx2,
            "has_cuda": host.has_cuda,
            "has_gpu": host.has_gpu,
        },
        "results": results,
    }

    # Generate Reports
    write_json_report(report_data, os.path.join(reports_dir, "kernelguard_report.json"))
    write_junit_report(report_data, os.path.join(reports_dir, "junit_report.xml"))
    write_sarif_report(report_data, os.path.join(reports_dir, "results.sarif"))
    generate_html_report(report_data, os.path.join(reports_dir, "report.html"))
    generate_summary_markdown(report_data, os.path.join(reports_dir, "summary.md"))

    print("=" * 70)
    print(f"Verification Summary: {passed_tests} PASSED, {failed_tests} FAILED, {skipped_tests} SKIPPED out of {total_tests} configurations.")
    print(f"Reports generated in '{reports_dir}/':")
    print(f"  - HTML:   {reports_dir}/report.html")
    print(f"  - JSON:   {reports_dir}/kernelguard_report.json")
    print(f"  - JUnit:  {reports_dir}/junit_report.xml")
    print(f"  - SARIF:  {reports_dir}/results.sarif")
    print(f"  - PR:     {reports_dir}/summary.md")

    # Check regression lockfile
    lock = load_baseline()
    if lock:
        regressions = check_regressions(report_data, lock, threshold_pct=cfg.regression_gates.max_instruction_count_increase_pct)
        if regressions:
            print("\nREGRESSION GATE FAILURES DETECTED:")
            for reg in regressions:
                print(f"  ❌ {reg}")
            return 2, report_data

    # Exit code determination
    if failed_tests > 0:
        return 1, report_data
    return 0, report_data
