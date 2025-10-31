#!/bin/bash
# bench_10gb.sh - run scalar & AVX scans on a big file, compute throughput, verify JSON equality, send WhatsApp summary
set -euo pipefail

FILE="big_sample_10GB.bin"
SIG="signatures.txt"
OUTDIR="./bench_out"
mkdir -p "$OUTDIR"

# ---------------------------
# Check binaries
# ---------------------------
if [ ! -x ./scalar_scan ]; then
  echo "Error: scalar_scan binary not found. Build it first."
  exit 1
fi

if [ ! -x ./avx_scan ]; then
  echo "Warning: avx_scan binary not found. AVX run will be skipped."
  DO_AVX=0
else
  DO_AVX=1
fi

# ---------------------------
# File size
# ---------------------------
FILESIZE_BYTES=$(stat -c%s "$FILE")
FILESIZE_MB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024}')

echo "File: $FILE ($FILESIZE_MB MB)"

# ---------------------------
# Run scalar scan
# ---------------------------
echo "Running scalar scan..."
/usr/bin/time -f "SCALAR_TIME %e" -o "$OUTDIR/scalar_time.txt" \
  ./scalar_scan "$SIG" "$FILE" 0 "$FILESIZE_BYTES" "$OUTDIR/out_scalar.json"

cat "$OUTDIR/scalar_time.txt"
SCALAR_TIME=$(awk '/SCALAR_TIME/ {print $2}' "$OUTDIR/scalar_time.txt")
SCALAR_THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$SCALAR_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')
echo "Scalar throughput: $SCALAR_THROUGHPUT MB/s"

# ---------------------------
# Run AVX scan (if available)
# ---------------------------
if [ "$DO_AVX" -eq 1 ]; then
  echo "Running AVX scan..."
  /usr/bin/time -f "AVX_TIME %e" -o "$OUTDIR/avx_time.txt" \
    ./avx_scan "$SIG" "$FILE" 0 "$FILESIZE_BYTES" "$OUTDIR/out_avx.json"

  cat "$OUTDIR/avx_time.txt"
  AVX_TIME=$(awk '/AVX_TIME/ {print $2}' "$OUTDIR/avx_time.txt")
  AVX_THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$AVX_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')
  echo "AVX throughput: $AVX_THROUGHPUT MB/s"

  # Speedup (time-based)
  SPEEDUP=$(awk -v s="$SCALAR_TIME" -v a="$AVX_TIME" 'BEGIN{printf "%.2f", s/a}')
  echo "Speedup (scalar/AVX): ${SPEEDUP}x (time-based)"
fi

# ---------------------------
# Verify outputs match
# ---------------------------
if [ "$DO_AVX" -eq 1 ]; then
  python3 - <<PY
import json
s=json.load(open("$OUTDIR/out_scalar.json"))
a=json.load(open("$OUTDIR/out_avx.json"))
print("Scalar == AVX:", s==a)
for name in s:
    if name=="meta": continue
    print(name, "matches:", len(s[name]))
PY
else
  echo "AVX not run; verifying scalar produced output"
  jq 'keys' "$OUTDIR/out_scalar.json" | sed -n '1,20p'
fi

# ---------------------------
# Signature counts
# ---------------------------
python3 - <<PY
import json
s=json.load(open("$OUTDIR/out_scalar.json"))
found = {k: len(v) for k,v in s.items() if k!='meta'}
print("Signatures found (scalar):")
for k,v in found.items():
    print(f"  {k}: {v}")
PY

# ---------------------------
# Send summary via Mudslide
# ---------------------------
TO_PHONE="918855997288"

SUMMARY=$(cat <<EOF
Attack Detected, using our script — Group -14
File: $FILE
Scalar time: ${SCALAR_TIME}s, throughput: ${SCALAR_THROUGHPUT} MB/s
AVX time:    ${AVX_TIME:-N/A}s, throughput: ${AVX_THROUGHPUT:-N/A} MB/s
Speedup (time-based): ${SPEEDUP:-N/A}x
Signatures found:
$(python3 - <<PY
import json
d=json.load(open("$OUTDIR/out_scalar.json"))
for k,v in d.items():
    if k=='meta': continue
    print(f"  {k}: {len(v)}")
PY
)
Bench output dir: $OUTDIR
EOF
)

# Print summary for verification
echo "------- SUMMARY -------"
echo "$SUMMARY"
echo "-----------------------"

# Send via Mudslide
#if command -v mudslide >/dev/null 2>&1; then
#  echo "Sending summary via mudslide to $TO_PHONE..."
#  #mudslide send "$TO_PHONE" "$SUMMARY" && echo "mudslide send OK" || echo "mudslide send FAILED"
#   mudslide send me "$SUMMARY"
#else
#  echo "mudslide CLI not found. Install and login to mudslide first."
#fi

#if command -v mudslide >/dev/null 2>&1; then
#  echo "Sending summary via Mudslide to $TO_PHONE..."
#  mudslide send me "$SUMMARY" && echo "✅ Mudslide send initiated successfully."
  
#  echo "Waiting for Mudslide to finish sending (please wait 10 seconds)..."
#  # Give Mudslide enough time to deliver the message before the script exits
#  for i in {10..1}; do
#    printf "\r⏳ %2ds remaining..." "$i"
#    sleep 1
#  done
#  echo -e "\n✔ Mudslide should now be done sending."
#else
#  echo "⚠️ Mudslide CLI not found. Please install and login first."
#fi


# Email settings (edit sender/recipient addresses below)
FROM_EMAIL="omshingare120@gmail.com"
TO_EMAIL="pcproject@coeptech.ac.in"
EMAIL_SUBJECT="[Group-14] Bench Results - AVX vs Scalar"

# ---------------------------
if [ -z "${EMAIL_APP_PASS:-}" ]; then
  echo "ERROR: EMAIL_APP_PASS environment variable not set. Export it before running, e.g.:"
  echo "  export EMAIL_APP_PASS='your-app-password-here'"
  echo "Skipping email send."
else
  echo "Sending email summary to $TO_EMAIL..."
  # Avoid exposing the password in the command line by using env var inside python call
  python3 send_email.py "$FROM_EMAIL" "$TO_EMAIL" "$EMAIL_SUBJECT" "$SUMMARY" || echo "Email send failed"
fi

echo "Bench done. Results are in $OUTDIR"
