#!/bin/bash
# bench_mpi.sh - MPI distributed signature scanning with fault tolerance
set -euo pipefail

FILE="big_sample_10GB.bin"
SIG="signatures.txt"
OUTDIR="./bench_out_mpi"
HOSTFILE="hostfile"
NUM_PROCESSES=4  # Adjust based on your cluster size

mkdir -p "$OUTDIR"

echo "========================================="
echo " MPI Distributed Signature Scanner"
echo " Group-14"
echo "========================================="

# ---------------------------
# Check binaries
# ---------------------------
if [ ! -x ./mpi_avx_scan ]; then
  echo "Error: mpi_avx_scan binary not found."
  echo "Compile it first with:"
  echo "  mpicxx -O3 -mavx2 -std=c++17 mpi_avx_scan.cpp -o mpi_avx_scan"
  exit 1
fi

if [ ! -f "$HOSTFILE" ]; then
  echo "Warning: hostfile not found. Creating default hostfile..."
  echo "localhost slots=4" > "$HOSTFILE"
fi

# ---------------------------
# File size
# ---------------------------
if [ ! -f "$FILE" ]; then
  echo "Error: Input file $FILE not found!"
  exit 1
fi

FILESIZE_BYTES=$(stat -c%s "$FILE")
FILESIZE_MB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024}')
FILESIZE_GB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024/1024}')

echo "File: $FILE ($FILESIZE_GB GB / $FILESIZE_MB MB)"
echo "Hostfile: $HOSTFILE"
echo "Number of processes: $NUM_PROCESSES"
echo ""

# ---------------------------
# Display cluster nodes
# ---------------------------
echo "Cluster Configuration:"
cat "$HOSTFILE"
echo ""

# ---------------------------
# Run MPI scan
# ---------------------------
echo "Starting MPI distributed scan..."
START_TIME=$(date +%s.%N)

/usr/bin/time -f "MPI_TIME %e" -o "$OUTDIR/mpi_time.txt" \
  mpirun -np "$NUM_PROCESSES" -hostfile "$HOSTFILE" \
  ./mpi_avx_scan "$SIG" "$FILE" "$OUTDIR/out_mpi.json" 2>&1 | tee "$OUTDIR/mpi_log.txt"

END_TIME=$(date +%s.%N)

cat "$OUTDIR/mpi_time.txt"
MPI_TIME=$(awk '/MPI_TIME/ {print $2}' "$OUTDIR/mpi_time.txt")
MPI_THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$MPI_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')

echo ""
echo "========================================="
echo " Results Summary"
echo "========================================="
echo "MPI execution time: ${MPI_TIME}s"
echo "MPI throughput: ${MPI_THROUGHPUT} MB/s"
echo ""

# ---------------------------
# Analyze results
# ---------------------------
echo "Analyzing results..."
python3 - <<PY
import json
import sys

try:
    with open("$OUTDIR/out_mpi.json") as f:
        result = json.load(f)
    
    print("\nSignatures found:")
    total_matches = 0
    sig_summary = []
    
    for key, value in result.items():
        if key == 'meta':
            continue
        match_count = len(value) if isinstance(value, list) else 0
        total_matches += match_count
        sig_summary.append(f"  {key}: {match_count} matches")
        print(f"  {key}: {match_count} matches")
    
    print(f"\nTotal matches across all signatures: {total_matches}")
    
    if 'meta' in result:
        print(f"\nMetadata:")
        print(f"  File size: {result['meta'].get('filesize', 'N/A')} bytes")
        print(f"  Chunks processed: {result['meta'].get('num_chunks', 'N/A')}")
        print(f"  Workers used: {result['meta'].get('num_workers', 'N/A')}")
    
    # Save summary for email
    with open("$OUTDIR/signature_summary.txt", "w") as f:
        for line in sig_summary:
            f.write(line + "\n")
        f.write(f"\nTotal matches: {total_matches}\n")
    
except Exception as e:
    print(f"Error analyzing results: {e}", file=sys.stderr)
    sys.exit(1)
PY

# ---------------------------
# Check for failures in log
# ---------------------------
echo ""
if grep -q "FAILED" "$OUTDIR/mpi_log.txt"; then
    echo "⚠️  WARNING: Worker failures detected during execution"
    echo "Failed workers were automatically reassigned. Check log for details."
    FAILURE_INFO=$(grep "FAILED\|Reassigned" "$OUTDIR/mpi_log.txt" | head -5)
else
    echo "✅ All workers completed successfully"
    FAILURE_INFO="No failures detected"
fi

# ---------------------------
# Prepare email summary
# ---------------------------
SIGNATURE_SUMMARY=$(cat "$OUTDIR/signature_summary.txt" 2>/dev/null || echo "No signatures found")

EMAIL_SUMMARY=$(cat <<EOF
==============================================
 MPI Distributed Signature Scan Results
 Group-14 - Attack Detection System
==============================================

Input File: $FILE
File Size: $FILESIZE_GB GB ($FILESIZE_MB MB)
Cluster Nodes: $NUM_PROCESSES processes

--- Performance Metrics ---
Execution Time: ${MPI_TIME}s
Throughput: ${MPI_THROUGHPUT} MB/s

--- Cluster Configuration ---
$(cat "$HOSTFILE")

--- Signature Detection Results ---
$SIGNATURE_SUMMARY

--- Fault Tolerance Status ---
$FAILURE_INFO

--- Output Files ---
Results: $OUTDIR/out_mpi.json
Logs: $OUTDIR/mpi_log.txt

==============================================
Scan completed at: $(date)
==============================================
EOF
)

# Print summary
echo ""
echo "$EMAIL_SUMMARY"

# ---------------------------
# Send email
# ---------------------------
FROM_EMAIL="omshingare120@gmail.com"
TO_EMAIL="pcproject@coeptech.ac.in"
EMAIL_SUBJECT="[Group-14] MPI Distributed Scan Results - $(date +%Y-%m-%d)"

if [ -z "${EMAIL_APP_PASS:-}" ]; then
  echo ""
  echo "⚠️  ERROR: EMAIL_APP_PASS environment variable not set."
  echo "Export it before running, e.g.:"
  echo "  export EMAIL_APP_PASS='your-app-password-here'"
  echo "Skipping email send."
  echo ""
  echo "To send email manually, run:"
  echo "  python3 send_email.py \"$FROM_EMAIL\" \"$TO_EMAIL\" \"$EMAIL_SUBJECT\" \"\$(cat $OUTDIR/email_summary.txt)\""
  
  # Save summary to file for manual sending
  echo "$EMAIL_SUMMARY" > "$OUTDIR/email_summary.txt"
else
  echo ""
  echo "📧 Sending email summary to $TO_EMAIL..."
  echo "$EMAIL_SUMMARY" > "$OUTDIR/email_summary.txt"
  
  if python3 send_email.py "$FROM_EMAIL" "$TO_EMAIL" "$EMAIL_SUBJECT" "$EMAIL_SUMMARY"; then
    echo "✅ Email sent successfully!"
  else
    echo "❌ Email send failed. Summary saved to $OUTDIR/email_summary.txt"
  fi
fi

echo ""
echo "========================================="
echo " Benchmark Complete"
echo "========================================="
echo "All results saved in: $OUTDIR"
echo ""
