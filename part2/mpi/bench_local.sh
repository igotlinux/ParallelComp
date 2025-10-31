#!/bin/bash
# bench_local.sh - Local testing version (no email, smaller files)
set -euo pipefail

# Use smaller test file for local testing
FILE="${1:-test_1GB.bin}"
SIG="signatures.txt"
OUTDIR="./bench_out_local"
NUM_PROCESSES="${2:-4}"  # Default 4 processes

mkdir -p "$OUTDIR"

echo "========================================="
echo " MPI Local Testing - Group-14"
echo "========================================="

# ---------------------------
# Check binary
# ---------------------------
if [ ! -x ./mpi_avx_scan ]; then
  echo "Error: mpi_avx_scan binary not found."
  echo "Compile it first with:"
  echo "  mpicxx -O3 -mavx2 -std=c++17 mpi_avx_scan.cpp -o mpi_avx_scan"
  exit 1
fi

# ---------------------------
# Create test file if not exists
# ---------------------------
if [ ! -f "$FILE" ]; then
  echo "Creating test file: $FILE (1GB)..."
  dd if=/dev/urandom of="$FILE" bs=1M count=1024 2>/dev/null
  
  # Add some known signatures
  echo -n "MALWARE_SIG_001" >> "$FILE"
  echo -n "ATTACK_PATTERN_XYZ" >> "$FILE"
  echo -n "EXPLOIT_CODE_ABC" >> "$FILE"
  echo "✅ Test file created with embedded signatures"
fi

# ---------------------------
# Create signatures if not exists
# ---------------------------
if [ ! -f "$SIG" ]; then
  echo "Creating signatures.txt..."
  cat > "$SIG" <<EOF
MALWARE_SIG_001
ATTACK_PATTERN_XYZ
EXPLOIT_CODE_ABC
RANSOMWARE_KEY
SQL_INJECTION_001
EOF
  echo "✅ Signatures file created"
fi

# ---------------------------
# Create hostfile for local testing
# ---------------------------
# MPICH doesn't use 'slots' - just list hostname multiple times or use -np
echo "localhost" > hostfile_local

# ---------------------------
# File info
# ---------------------------
FILESIZE_BYTES=$(stat -c%s "$FILE")
FILESIZE_MB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024}')
FILESIZE_GB=$(awk -v b="$FILESIZE_BYTES" 'BEGIN{printf "%.2f", b/1024/1024/1024}')

echo ""
echo "File: $FILE ($FILESIZE_GB GB / $FILESIZE_MB MB)"
echo "Processes: $NUM_PROCESSES (all on localhost)"
echo "Signatures: $(wc -l < $SIG)"
echo ""

# ---------------------------
# System info
# ---------------------------
echo "System Information:"
echo "  CPU cores: $(nproc)"
echo "  Total RAM: $(free -h | awk '/^Mem:/ {print $2}')"
echo "  Available RAM: $(free -h | awk '/^Mem:/ {print $7}')"
echo "  AVX2 support: $(lscpu | grep -o 'avx2' || echo 'not detected')"
echo ""

# ---------------------------
# Run MPI scan
# ---------------------------
echo "Starting MPI scan with $NUM_PROCESSES processes..."
echo "Press Ctrl+C to stop"
echo ""

START_TIME=$(date +%s.%N)

# Try mpiexec first (MPICH), fallback to mpirun
if command -v mpiexec >/dev/null 2>&1; then
  MPI_CMD="mpiexec"
else
  MPI_CMD="mpirun"
fi

echo "Using MPI command: $MPI_CMD"
echo ""

# Run MPI scan (don't use /usr/bin/time with MPI as it spawns multiple instances)
$MPI_CMD -n "$NUM_PROCESSES" \
  ./mpi_avx_scan "$SIG" "$FILE" "$OUTDIR/output.json" 2>&1 | tee "$OUTDIR/run.log"

MPI_EXIT_CODE=${PIPESTATUS[0]}

END_TIME=$(date +%s.%N)

echo ""
echo "========================================="
echo " Results"
echo "========================================="

# ---------------------------
# Timing info
# ---------------------------
EXEC_TIME=$(awk -v start="$START_TIME" -v end="$END_TIME" 'BEGIN{printf "%.2f", end-start}')
THROUGHPUT=$(awk -v b="$FILESIZE_BYTES" -v t="$EXEC_TIME" 'BEGIN{printf "%.2f", (b/1024/1024)/t}')

echo "Execution time: ${EXEC_TIME}s"
echo "Throughput: ${THROUGHPUT} MB/s"
echo ""

# ---------------------------
# Resource usage
# ---------------------------
if [ -f "$OUTDIR/time.txt" ]; then
  echo "Resource Usage:"
  cat "$OUTDIR/time.txt"
  echo ""
fi

# ---------------------------
# Analyze results
# ---------------------------
if [ -f "$OUTDIR/output.json" ]; then
  echo "Signature Matches Found:"
  python3 - <<PY
import json
import sys

try:
    with open("$OUTDIR/output.json") as f:
        result = json.load(f)
    
    total_matches = 0
    for key, value in result.items():
        if key == 'meta':
            continue
        match_count = len(value) if isinstance(value, list) else 0
        total_matches += match_count
        if match_count > 0:
            print(f"  ✅ {key}: {match_count} matches")
            # Show first few positions
            if match_count <= 5:
                positions = ', '.join(str(x) for x in value)
            else:
                positions = ', '.join(str(x) for x in value[:5]) + f', ... (+{match_count-5} more)'
            print(f"     Positions: {positions}")
        else:
            print(f"  ❌ {key}: not found")
    
    print(f"\n  Total matches: {total_matches}")
    
    if 'meta' in result:
        print(f"\nMetadata:")
        meta = result['meta']
        if 'filesize' in meta:
            print(f"  File size: {meta['filesize']:,} bytes")
        if 'num_chunks' in meta:
            print(f"  Chunks processed: {meta['num_chunks']}")
        if 'num_workers' in meta:
            print(f"  Workers used: {meta['num_workers']}")
    
except FileNotFoundError:
    print("❌ Output file not found!", file=sys.stderr)
    sys.exit(1)
except json.JSONDecodeError as e:
    print(f"❌ Invalid JSON: {e}", file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print(f"❌ Error: {e}", file=sys.stderr)
    sys.exit(1)
PY
else
  echo "❌ Output file not generated!"
fi

# ---------------------------
# Check for failures
# ---------------------------
echo ""
if grep -q "FAILED" "$OUTDIR/run.log" 2>/dev/null; then
  echo "⚠️  Worker Failures Detected:"
  grep "FAILED\|Reassigned" "$OUTDIR/run.log" | head -10
else
  echo "✅ All workers completed successfully"
fi

echo ""
echo "========================================="
echo " Output files:"
echo "  Results: $OUTDIR/output.json"
echo "  Log: $OUTDIR/run.log"
echo "  Timing: $OUTDIR/time.txt"
echo "========================================="
echo ""

# ---------------------------
# Performance tips
# ---------------------------
echo "💡 Performance Tips:"
echo "  • To test with more processes: ./bench_local.sh test_1GB.bin 8"
echo "  • To test with larger file: dd if=/dev/urandom of=test_5GB.bin bs=1M count=5120"
echo "  • To simulate failure: kill -9 <worker_pid> during execution"
echo "  • To monitor: htop or watch 'ps aux | grep mpi'"
echo ""