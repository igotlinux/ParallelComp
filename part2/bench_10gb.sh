#!/bin/bash
# bench_10gb.sh - run scalar & avx scans on the 10GB file, compute throughput and verify JSON equality
set -euo pipefail

FILE="big_sample_10GB.bin"
SIG="signatures.txt"
OUTDIR="./bench_out"
mkdir -p "$OUTDIR"

# confirm binaries exist
if [ ! -x ./scalar_scan ]; then
  echo "Error: scalar_scan binary not found. Build it first."
  exit 1
fi
if [ ! -x ./avx_scan ]; then
  echo "Warning: avx_scan binary not found. Will skip AVX run."
  DO_AVX=0
else
  DO_AVX=1
fi

# get file size in bytes
FILESIZE_BYTES=$(stat -c%s "$FILE")
FILESIZE_MB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024}')

echo "File: $FILE ($FILESIZE_MB MB)"

# run scalar scan
echo "Running scalar scan..."
/usr/bin/time -f "SCALAR_TIME %e" -o "$OUTDIR/scalar_time.txt" \
  ./scalar_scan "$SIG" "$FILE" 0 "$FILESIZE_BYTES" "$OUTDIR/out_scalar.json"

cat "$OUTDIR/scalar_time.txt"
SCALAR_TIME=$(awk '/SCALAR_TIME/ {print $2}' "$OUTDIR/scalar_time.txt")
SCALAR_THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$SCALAR_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')

echo "Scalar throughput: $SCALAR_THROUGHPUT MB/s"

# run avx scan if available
if [ "$DO_AVX" -eq 1 ]; then
  echo "Running AVX scan..."
  /usr/bin/time -f "AVX_TIME %e" -o "$OUTDIR/avx_time.txt" \
    ./avx_scan "$SIG" "$FILE" 0 "$FILESIZE_BYTES" "$OUTDIR/out_avx.json"

  cat "$OUTDIR/avx_time.txt"
  AVX_TIME=$(awk '/AVX_TIME/ {print $2}' "$OUTDIR/avx_time.txt")
  AVX_THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$AVX_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')
  echo "AVX throughput: $AVX_THROUGHPUT MB/s"
  # speedup
  #SPEEDUP=$(awk -v s="$SCALAR_THROUGHPUT" -v a="$AVX_THROUGHPUT" 'BEGIN{printf "%.2f", s/a}')
  #echo "Speedup (scalar/avx): ${SPEEDUP}x"
# speedup (time-based)
SPEEDUP=$(awk -v s="$SCALAR_TIME" -v a="$AVX_TIME" 'BEGIN{printf "%.2f", s/a}')
echo "Speedup (scalar/avx): ${SPEEDUP}x (time-based)"

fi

# verify outputs match (if avx ran)
if [ "$DO_AVX" -eq 1 ]; then
  python3 - <<PY
import json,sys
s=json.load(open("$OUTDIR/out_scalar.json"))
a=json.load(open("$OUTDIR/out_avx.json"))
print("Scalar == AVX:", s==a)
# print counts of matches for top signatures
for name in s:
    if name=="meta": continue
    print(name, "matches:", len(s[name]))
PY
else
  echo "AVX not run; verifying scalar produced output"
  jq 'keys' "$OUTDIR/out_scalar.json" | sed -n '1,20p'
fi

# quick check that signatures were found (non-empty)
python3 - <<PY
import json
s=json.load(open("$OUTDIR/out_scalar.json"))
found = {k: len(v) for k,v in s.items() if k!='meta'}
print("Signatures found (scalar):")
for k,v in found.items():
    print(f"  {k}: {v}")
PY

# ---------------------------
# Send summary via mudslide
# ---------------------------

# Phone number (as requested)
TO_PHONE="918855997275"

# Create summary text (multi-line)
SUMMARY=$(cat <<EOF
Attack Detected, using our script — Group -14
File: $FILE
Scalar time: ${SCALAR_TIME}s, throughput: ${SCALAR_THROUGHPUT} MB/s
AVX time:    ${AVX_TIME}s, throughput: ${AVX_THROUGHPUT} MB/s
Speedup (time-based): ${SPEEDUP}x
Signatures found (per-signature counts):
$(python3 - <<PY
import json
d=json.load(open("$OUTDIR/out_scalar.json"))
for k,v in d.items():
    if k=='meta': continue
    print(f"  {k}: {len(v)}")
PY
)
Merged alerts: $OUTDIR/out_avx.json (and out_scalar.json)
Bench output dir: $OUTDIR
EOF
)

# Print summary to console for verification
echo "------- SUMMARY -------"
echo "$SUMMARY"
echo "-----------------------"

# Send via mudslide CLI (safe quoting)
if command -v mudslide >/dev/null 2>&1; then
  # use double-quoting so newlines are preserved
  echo "Sending summary to WhatsApp via mudslide to $TO_PHONE..."
  mudslide send "$TO_PHONE" "$SUMMARY"
  RC=$?
  if [ $RC -eq 0 ]; then
    echo "mudslide send OK"
  else
    echo "mudslide send failed (exit $RC)"
  fi
else
  echo "mudslide not found in PATH — cannot send message. Install and login to mudslide and re-run."
fi

echo "Bench done. Results are in $OUTDIR"
