"""
Baseline Lockfile Management and Regression Gatekeeper
"""

import json
import os
from typing import Dict, Any, List, Tuple, Optional


LOCKFILE_PATH = "kernelguard_lock.json"


def load_baseline(path: str = LOCKFILE_PATH) -> Optional[Dict[str, Any]]:
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def update_baseline(report_data: Dict[str, Any], path: str = LOCKFILE_PATH):
    baseline_entries = {}
    for r in report_data.get("results", []):
        key = f"{r.get('name')}:{r.get('toolchain')}:{r.get('opt_level')}:{r.get('fp_mode')}"
        baseline_entries[key] = {
            "name": r.get("name"),
            "toolchain": r.get("toolchain"),
            "opt_level": r.get("opt_level"),
            "fp_mode": r.get("fp_mode"),
            "vectorized": r.get("vectorized", False),
            "instruction_count": r.get("instruction_count"),
        }

    lock_data = {
        "version": 1,
        "updated_at": report_data.get("timestamp", ""),
        "entries": baseline_entries,
    }

    with open(path, "w", encoding="utf-8") as f:
        json.dump(lock_data, f, indent=2)


def check_regressions(report_data: Dict[str, Any], lock_data: Dict[str, Any], threshold_pct: float = 10.0) -> List[str]:
    regressions = []
    baseline_entries = lock_data.get("entries", {})

    lock_arch = lock_data.get("arch", "")
    curr_arch = report_data.get("host_info", {}).get("machine", "")
    same_arch = (not lock_arch or not curr_arch or lock_arch == curr_arch)

    for r in report_data.get("results", []):
        key = f"{r.get('name')}:{r.get('toolchain')}:{r.get('opt_level')}:{r.get('fp_mode')}"
        if key not in baseline_entries or r.get("status") == "SKIPPED":
            continue

        base = baseline_entries[key]

        # 1. Check Vectorization Regression
        if base.get("vectorized") and not r.get("vectorized"):
            regressions.append(
                f"Vectorization regression for {key}: previously vectorized, now scalar"
            )

        # 2. Check Instruction Count Regression (strictly architecture-matched)
        base_icount = base.get("instruction_count")
        curr_icount = r.get("instruction_count")
        if same_arch and base_icount and curr_icount:
            increase_pct = ((curr_icount - base_icount) / base_icount) * 100.0
            if increase_pct > threshold_pct:
                regressions.append(
                    f"Instruction count regression for {key}: increased from {base_icount} to {curr_icount} (+{increase_pct:.1f}% > {threshold_pct}%)"
                )

    return regressions
