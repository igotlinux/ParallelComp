"""
GitHub Pull Request Markdown Summary Generator
"""

from typing import Dict, Any


def generate_summary_markdown(report_data: Dict[str, Any], output_path: str = "reports/summary.md"):
    total = report_data.get("total_tests", 0)
    passed = report_data.get("passed_tests", 0)
    failed = report_data.get("failed_tests", 0)
    skipped = report_data.get("skipped_tests", 0)
    profile = report_data.get("profile", "quick")
    host_info = report_data.get("host", {})

    status_icon = "✅" if failed == 0 else "❌"

    lines = [
        f"## {status_icon} KernelGuard Verification Summary (`{profile}` profile)",
        "",
        f"- **Host Platform:** `{host_info.get('os_name', 'unknown')}` ({host_info.get('machine', 'unknown')})",
        f"- **Tests Evaluated:** **{total}** | **Passed:** {passed} | **Failed:** {failed} | **Skipped:** {skipped}",
        "",
        "### Key Verification Results",
        "| Kernel | Status | Toolchain | Opt | FP Mode | Vectorized | Verdict | Details |",
        "| :--- | :---: | :--- | :---: | :--- | :---: | :--- | :--- |",
    ]

    for r in report_data.get("results", []):
        badge = "✅ PASS" if r.get("status") == "PASS" else ("❌ FAIL" if r.get("status") == "FAIL" else "⚠️ SKIP")
        vec = "Yes" if r.get("vectorized") else "-"
        lines.append(
            f"| `{r.get('name')}` | {badge} | `{r.get('toolchain')}` | `{r.get('opt_level')}` | `{r.get('fp_mode')}` | {vec} | `{r.get('verdict_type')}` | {r.get('details', '')} |"
        )

    lines.append("")
    lines.append("> Generated automatically by [KernelGuard](https://github.com/igotlinux/kernal-guard)")

    content = "\n".join(lines) + "\n"
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(content)
