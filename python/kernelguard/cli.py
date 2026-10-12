"""
Command-Line Interface (CLI) for KernelGuard
"""

import argparse
import sys
import os
import json
from .runner import run_verification
from .baseline import update_baseline, load_baseline


def main():
    parser = argparse.ArgumentParser(
        prog="kg",
        description="KernelGuard: Production-Quality Compiler Verification Framework"
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # Command: run
    run_parser = subparsers.add_parser("run", help="Run verification matrix")
    run_parser.add_argument("--profile", choices=["quick", "full"], default="quick",
                            help="Verification profile (default: quick)")
    run_parser.add_argument("--backend", choices=["cpu", "cuda"], default=None,
                            help="Filter execution backend (cpu or cuda)")
    run_parser.add_argument("--config", default="kernelguard.yaml",
                            help="Path to configuration file")
    run_parser.add_argument("--reports-dir", default="reports",
                            help="Output directory for generated reports")

    # Command: report
    report_parser = subparsers.add_parser("report", help="Display summary of latest verification run")
    report_parser.add_argument("--format", choices=["text", "json", "summary"], default="text",
                               help="Display format")
    report_parser.add_argument("--reports-dir", default="reports",
                               help="Reports directory")

    # Command: repro
    repro_parser = subparsers.add_parser("repro", help="Inspect and run standalone reproducer")
    repro_parser.add_argument("failure_id", help="Failure ID to inspect (e.g. fail_vec_add_gcc_O2_strict)")

    # Command: baseline
    baseline_parser = subparsers.add_parser("baseline", help="Manage baseline regression lockfile")
    baseline_sub = baseline_parser.add_subparsers(dest="baseline_action")
    baseline_sub.add_parser("update", help="Update kernelguard_lock.json with current results")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    if args.command == "run":
        exit_code, _ = run_verification(
            profile_name=args.profile,
            backend_filter=args.backend,
            config_path=args.config,
            reports_dir=args.reports_dir,
        )
        sys.exit(exit_code)

    elif args.command == "report":
        rep_path = os.path.join(args.reports_dir, "kernelguard_report.json")
        if not os.path.exists(rep_path):
            print(f"No existing report found at '{rep_path}'. Run 'kg run' first.", file=sys.stderr)
            sys.exit(3)

        with open(rep_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        if args.format == "json":
            print(json.dumps(data, indent=2))
        elif args.format == "summary":
            sum_path = os.path.join(args.reports_dir, "summary.md")
            if os.path.exists(sum_path):
                with open(sum_path, "r") as sf:
                    print(sf.read())
            else:
                print("summary.md not found.")
        else:
            print("=" * 60)
            print(f"KernelGuard Verification Run Report ({data.get('profile', 'unknown')})")
            print("=" * 60)
            print(f"Timestamp: {data.get('timestamp')}")
            print(f"Total Tests:   {data.get('total_tests')}")
            print(f"Passed Tests:  {data.get('passed_tests')}")
            print(f"Failed Tests:  {data.get('failed_tests')}")
            print(f"Skipped Tests: {data.get('skipped_tests')}")
            print("-" * 60)
            for r in data.get("results", []):
                print(f"  [{r.get('status')}] {r.get('name')} ({r.get('toolchain')} {r.get('opt_level')}) - {r.get('details')}")
            print("=" * 60)
        sys.exit(0)

    elif args.command == "repro":
        bundle_dir = os.path.join("repro_bundles", args.failure_id)
        if not os.path.exists(bundle_dir):
            print(f"No reproducer bundle found for '{args.failure_id}' at '{bundle_dir}'.", file=sys.stderr)
            sys.exit(3)

        print(f"=== Reproducer Bundle: {args.failure_id} ===")
        meta_file = os.path.join(bundle_dir, "metadata.json")
        if os.path.exists(meta_file):
            with open(meta_file, "r") as mf:
                print(mf.read())

        print(f"\nLocation: {bundle_dir}")
        print("To run standalone reproduction:")
        print(f"  cd {bundle_dir} && ./build.sh")
        sys.exit(0)

    elif args.command == "baseline":
        if args.baseline_action == "update":
            rep_path = "reports/kernelguard_report.json"
            if not os.path.exists(rep_path):
                print(f"No report found at '{rep_path}'. Run 'kg run' first to generate results.", file=sys.stderr)
                sys.exit(3)
            with open(rep_path, "r", encoding="utf-8") as f:
                rep_data = json.load(f)
            update_baseline(rep_data)
            print("Updated 'kernelguard_lock.json' with current verification baseline.")
            sys.exit(0)
        else:
            baseline_parser.print_help()
            sys.exit(0)


if __name__ == "__main__":
    main()
