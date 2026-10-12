"""
Command-Line Interface (CLI) for KernelGuard
"""

import argparse
import sys
import os
import json
from .runner import run_verification
from .baseline import update_baseline, load_baseline
from .licensing import generate_license_token, verify_license_token, get_active_license


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
    run_parser.add_argument("--harness", default=None,
                            help="Path to pre-built kg_harness binary")
    run_parser.add_argument("--license-key", default=None,
                            help="Commercial License Key (or set KERNELGUARD_TOKEN env var)")

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

    # Command: license
    license_parser = subparsers.add_parser("license", help="Manage and verify commercial license tokens")
    license_sub = license_parser.add_subparsers(dest="license_action")

    # kg license issue
    issue_parser = license_sub.add_parser("issue", help="Issue a signed commercial license token (Vendor command)")
    issue_parser.add_argument("--customer", required=True, help="Customer or organization name")
    issue_parser.add_argument("--email", required=True, help="Customer primary billing/contact email")
    issue_parser.add_argument("--plan", choices=["pro", "enterprise"], default="pro", help="Subscription tier")
    issue_parser.add_argument("--seats", type=int, default=5, help="Number of licensed developer seats")
    issue_parser.add_argument("--days", type=int, default=365, help="Subscription duration in days")

    # kg license verify
    verify_parser = license_sub.add_parser("verify", help="Verify a license token string")
    verify_parser.add_argument("token", nargs="?", default=None, help="License token to verify")

    # kg license info
    license_sub.add_parser("info", help="Display active license details from environment")

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
            harness_bin_override=args.harness,
            license_key=args.license_key,
        )
        sys.exit(exit_code)

    elif args.command == "license":
        if args.license_action == "issue":
            token = generate_license_token(
                customer=args.customer,
                email=args.email,
                plan=args.plan,
                seats=args.seats,
                valid_days=args.days,
            )
            print("=" * 65)
            print(f"🛡️  KernelGuard License Token Generated ({args.plan.upper()})")
            print("=" * 65)
            print(f"Customer: {args.customer} <{args.email}>")
            print(f"Tier:     {args.plan.upper()} ({args.seats} Seats)")
            print(f"Validity: {args.days} Days")
            print("-" * 65)
            print("LICENSE TOKEN (Give this to customer / set KERNELGUARD_TOKEN):")
            print(f"\n{token}\n")
            print("=" * 65)
            sys.exit(0)

        elif args.license_action == "verify":
            token = args.token or os.environ.get("KERNELGUARD_TOKEN") or os.environ.get("KG_LICENSE_KEY")
            lic = verify_license_token(token)
            print("=" * 60)
            print("KernelGuard License Verification")
            print("=" * 60)
            print(f"Valid:      {lic.is_valid}")
            print(f"Plan:       {lic.plan.upper()}")
            print(f"Customer:   {lic.customer}")
            print(f"Email:      {lic.email}")
            print(f"Seats:      {lic.seats}")
            print(f"Expires:    {lic.expires_at_str}")
            print(f"Expired:    {lic.is_expired}")
            if lic.error_message:
                print(f"Status Msg: {lic.error_message}")
            if lic.features:
                print(f"Features:   {', '.join(lic.features)}")
            print("=" * 60)
            sys.exit(0 if lic.is_valid else 1)

        elif args.license_action == "info":
            lic = get_active_license()
            print("=" * 60)
            print("Active License Status (Environment / Default)")
            print("=" * 60)
            print(f"Plan:       {lic.plan.upper()}")
            print(f"Active:     {lic.is_valid}")
            print(f"Customer:   {lic.customer}")
            if lic.email:
                print(f"Email:      {lic.email}")
            if lic.seats:
                print(f"Seats:      {lic.seats}")
            if lic.expires_at_str:
                print(f"Expires:    {lic.expires_at_str}")
            if lic.error_message:
                print(f"Details:    {lic.error_message}")
            print("=" * 60)
            sys.exit(0)

        else:
            license_parser.print_help()
            sys.exit(0)

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
