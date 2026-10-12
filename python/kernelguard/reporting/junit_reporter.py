"""
JUnit XML Report Generator for CI Integration
"""

import xml.etree.ElementTree as ET
from typing import Dict, Any


def write_junit_report(report_data: Dict[str, Any], output_path: str = "reports/junit_report.xml"):
    testsuites = ET.Element("testsuites")

    testsuite = ET.SubElement(
        testsuites,
        "testsuite",
        name="KernelGuard",
        tests=str(report_data.get("total_tests", 0)),
        failures=str(report_data.get("failed_tests", 0)),
        skipped=str(report_data.get("skipped_tests", 0)),
        time=str(report_data.get("total_time_sec", 0.0)),
    )

    for res in report_data.get("results", []):
        tc = ET.SubElement(
            testsuite,
            "testcase",
            classname=f"kernelguard.{res.get('toolchain', 'unknown')}",
            name=res.get("name", "test"),
            time=str(res.get("time_sec", 0.0)),
        )

        status = res.get("status", "PASS")
        if status == "FAIL":
            failure = ET.SubElement(
                tc,
                "failure",
                message=res.get("failure_reason", "Verification failure"),
                type=res.get("verdict_type", "MISMATCH"),
            )
            failure.text = res.get("details", "")
        elif status == "SKIPPED":
            skipped = ET.SubElement(
                tc,
                "skipped",
                message=res.get("skip_reason", "Hardware or toolchain unsupported"),
            )

    tree = ET.ElementTree(testsuites)
    tree.write(output_path, encoding="utf-8", xml_declaration=True)
