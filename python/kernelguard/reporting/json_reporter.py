"""
Machine-readable JSON Report Generator
"""

import json
from typing import Dict, Any


def write_json_report(report_data: Dict[str, Any], output_path: str = "reports/kernelguard_report.json"):
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(report_data, f, indent=2)
