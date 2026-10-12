"""
Interactive HTML Report Generator for KernelGuard
"""

from typing import Dict, Any


def generate_html_report(report_data: Dict[str, Any], output_path: str = "reports/report.html"):
    total = report_data.get("total_tests", 0)
    passed = report_data.get("passed_tests", 0)
    failed = report_data.get("failed_tests", 0)
    skipped = report_data.get("skipped_tests", 0)
    pass_pct = (passed / total * 100.0) if total > 0 else 0.0

    profile = report_data.get("profile", "quick")
    timestamp = report_data.get("timestamp", "N/A")
    host_info = report_data.get("host", {})

    results = report_data.get("results", [])

    rows_html = []
    for r in results:
        status = r.get("status", "PASS")
        badge_class = "badge-pass" if status == "PASS" else ("badge-fail" if status == "FAIL" else "badge-skip")
        vec_status = "Yes" if r.get("vectorized", False) else ("N/A" if status == "SKIPPED" else "No")
        repro_link = f"<a href='../repro_bundles/{r.get('failure_id')}' class='repro-link'>repro bundle</a>" if r.get('failure_id') else "-"

        rows_html.append(f"""
        <tr>
            <td class="mono font-semibold">{r.get('name', '-')}</td>
            <td><span class="badge {badge_class}">{status}</span></td>
            <td class="mono">{r.get('toolchain', '-')}</td>
            <td>{r.get('opt_level', '-')}</td>
            <td>{r.get('fp_mode', '-')}</td>
            <td>{vec_status}</td>
            <td class="mono">{r.get('verdict_type', '-')}</td>
            <td class="text-sm text-dim">{r.get('details', '-')}</td>
            <td>{repro_link}</td>
        </tr>
        """)

    table_rows = "\n".join(rows_html)

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>KernelGuard Verification Report</title>
    <style>
        :root {{
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #21262d;
            --border: #30363d;
            --text: #f0f6fc;
            --text-dim: #8b949e;
            --accent: #58a6ff;
            --success: #3fb950;
            --danger: #f85149;
            --warning: #d29922;
        }}
        * {{ margin: 0; padding: 0; box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }}
        body {{ background: var(--bg); color: var(--text); padding: 32px 24px; line-height: 1.5; }}
        .container {{ max-width: 1200px; margin: 0 auto; }}
        header {{ margin-bottom: 28px; border-bottom: 1px solid var(--border); padding-bottom: 20px; }}
        h1 {{ font-size: 26px; font-weight: 700; color: #fff; display: flex; align-items: center; gap: 10px; }}
        .subtitle {{ color: var(--text-dim); font-size: 14px; margin-top: 4px; }}
        
        .metrics-grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 28px; }}
        .card {{ background: var(--surface); border: 1px solid var(--border); border-radius: 8px; padding: 18px 20px; }}
        .card-label {{ font-size: 12px; text-transform: uppercase; letter-spacing: 0.5px; color: var(--text-dim); font-weight: 600; }}
        .card-val {{ font-size: 28px; font-weight: 700; margin-top: 6px; }}
        .card-val.success {{ color: var(--success); }}
        .card-val.danger {{ color: var(--danger); }}
        .card-val.warning {{ color: var(--warning); }}

        .table-card {{ background: var(--surface); border: 1px solid var(--border); border-radius: 8px; overflow: hidden; margin-bottom: 30px; }}
        .table-header {{ padding: 16px 20px; border-bottom: 1px solid var(--border); font-weight: 600; display: flex; justify-content: space-between; align-items: center; }}
        table {{ width: 100%; border-collapse: collapse; text-align: left; font-size: 13px; }}
        th {{ background: #1c2128; color: var(--text-dim); padding: 12px 16px; font-weight: 600; text-transform: uppercase; font-size: 11px; letter-spacing: 0.5px; }}
        td {{ padding: 12px 16px; border-bottom: 1px solid var(--border); }}
        tr:hover td {{ background: var(--surface-hover); }}
        
        .badge {{ display: inline-block; padding: 2px 8px; border-radius: 12px; font-size: 11px; font-weight: 700; text-transform: uppercase; }}
        .badge-pass {{ background: rgba(63, 185, 80, 0.15); color: var(--success); border: 1px solid rgba(63, 185, 80, 0.4); }}
        .badge-fail {{ background: rgba(248, 81, 73, 0.15); color: var(--danger); border: 1px solid rgba(248, 81, 73, 0.4); }}
        .badge-skip {{ background: rgba(210, 153, 34, 0.15); color: var(--warning); border: 1px solid rgba(210, 153, 34, 0.4); }}
        
        .mono {{ font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace; }}
        .font-semibold {{ font-weight: 600; }}
        .text-dim {{ color: var(--text-dim); }}
        .text-sm {{ font-size: 12px; }}
        .repro-link {{ color: var(--accent); text-decoration: none; }}
        .repro-link:hover {{ text-decoration: underline; }}

        footer {{ text-align: center; color: var(--text-dim); font-size: 12px; margin-top: 40px; }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>🛡️ KernelGuard Verification Report</h1>
            <div class="subtitle">Profile: <strong>{profile}</strong> | Generated at: <strong>{timestamp}</strong> | Host: <strong>{host_info.get('os_name', 'unknown')} ({host_info.get('machine', 'unknown')})</strong></div>
        </header>

        <div class="metrics-grid">
            <div class="card">
                <div class="card-label">Total Verification Tests</div>
                <div class="card-val">{total}</div>
            </div>
            <div class="card">
                <div class="card-label">Passed</div>
                <div class="card-val success">{passed} ({pass_pct:.1f}%)</div>
            </div>
            <div class="card">
                <div class="card-label">Failed</div>
                <div class="card-val danger">{failed}</div>
            </div>
            <div class="card">
                <div class="card-label">Skipped (Hardware / Toolchain)</div>
                <div class="card-val warning">{skipped}</div>
            </div>
        </div>

        <div class="table-card">
            <div class="table-header">
                <span>Kernel Verification Results</span>
                <span class="text-dim text-sm">{len(results)} configurations evaluated</span>
            </div>
            <table>
                <thead>
                    <tr>
                        <th>Kernel Name</th>
                        <th>Status</th>
                        <th>Toolchain</th>
                        <th>Opt Level</th>
                        <th>FP Mode</th>
                        <th>Vectorized</th>
                        <th>Verdict</th>
                        <th>Details</th>
                        <th>Reproducer</th>
                    </tr>
                </thead>
                <tbody>
                    {table_rows}
                </tbody>
            </table>
        </div>

        <footer>
            KernelGuard v1.0.0 — Production-Quality Compiler Verification Framework
        </footer>
    </div>
</body>
</html>
"""
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html_content)
