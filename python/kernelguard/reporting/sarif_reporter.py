"""
SARIF Report Generator for GitHub Security and Code Scanning
"""

import json
from typing import Dict, Any


def write_sarif_report(report_data: Dict[str, Any], output_path: str = "reports/results.sarif"):
    sarif = {
        "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
        "version": "2.1.0",
        "runs": [
            {
                "tool": {
                    "driver": {
                        "name": "KernelGuard",
                        "version": "1.0.0",
                        "informationUri": "https://github.com/igotlinux/kernal-guard",
                        "rules": [
                            {
                                "id": "KG001",
                                "name": "NumericalMismatch",
                                "shortDescription": {"text": "Kernel numerical output violated contract"},
                            },
                            {
                                "id": "KG002",
                                "name": "VectorizationRegression",
                                "shortDescription": {"text": "Compiler loop vectorization regression detected"},
                            },
                            {
                                "id": "KG003",
                                "name": "MemoryGuardViolation",
                                "shortDescription": {"text": "Buffer overrun or canary corruption detected"},
                            },
                        ],
                    }
                },
                "results": [],
            }
        ],
    }

    results_list = sarif["runs"][0]["results"]

    for res in report_data.get("results", []):
        if res.get("status") == "FAIL":
            verdict = res.get("verdict_type", "MISMATCH")
            rule_id = "KG001"
            if "CANARY" in verdict or "GUARD" in verdict:
                rule_id = "KG003"
            elif "VEC" in verdict:
                rule_id = "KG002"

            results_list.append({
                "ruleId": rule_id,
                "level": "error",
                "message": {
                    "text": f"Kernel {res.get('name')} failed verification: {res.get('details', '')}"
                },
                "locations": [
                    {
                        "physicalLocation": {
                            "artifactLocation": {
                                "uri": res.get("source_file", "unknown")
                            }
                        }
                    }
                ],
            })

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(sarif, f, indent=2)
