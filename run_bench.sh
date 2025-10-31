#!/usr/bin/env bash
# ======================================================
#  MPI Distributed Signature Scanner Runner (Group-14)
#  Logs live output + emails results after completion
# ======================================================

set -euo pipefail
IFS=$'\n\t'

# ======= CONFIG =======
FROM_EMAIL="omshingare120@gmail.com"
TO_EMAIL="pcproject@coeptech.ac.in"
EMAIL_SUBJECT="[Group-14][$(hostname)] MPI AVX Scan Results"
LOG_DIR="./logs"
LOG_FILE="$LOG_DIR/bench_run_$(date +%Y%m%d_%H%M%S).log"

# ======= PREP =======
mkdir -p "$LOG_DIR"
echo "🚀 Starting MPI Distributed Signature Scanner..."
echo "📄 Log file: $LOG_FILE"
echo "========================================"

# ======= RUN MPI JOB =======
# NOTE: using your exact mpirun command
mpirun --mca btl tcp,self \
       --mca btl_tcp_if_include 192.168.81.0/24 \
       --mca oob_tcp_if_include 192.168.81.0/24 \
       -np 1 -H tyrell \
       ./mpi_avx_scan signatures.txt big_sample_10GB.bin out.json \
       2>&1 | tee "$LOG_FILE"

EXIT_CODE=${PIPESTATUS[0]}
echo "========================================"
echo "✅ Program exited with code: $EXIT_CODE"
echo "========================================"

# ======= EMAIL LOG FILE =======
if [[ -z "${EMAIL_APP_PASS:-}" ]]; then
  echo "⚠️  EMAIL_APP_PASS not set. Skipping email send."
else
  echo "📨 Sending log file via email..."
  SUMMARY="Run completed on $(hostname) with exit code $EXIT_CODE.
See attached log file for full details."

  python3 send_email.py "$FROM_EMAIL" "$TO_EMAIL" "$EMAIL_SUBJECT" "$SUMMARY" "$LOG_FILE" \
    && echo "✅ Email sent successfully." \
    || echo "❌ Email send failed."
fi

echo "========================================"
echo "🎯 Done. Log saved at: $LOG_FILE"
