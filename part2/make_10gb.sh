#!/bin/bash
# make_10gb.sh - create a 10 GB test file with sprinkled signatures
set -euo pipefail

OUT="big_sample_10GB.bin"
SIG_FILE="signatures.txt"

# safety checks
REQUIRED_FREE_GB=11
FREE_KB=$(df --output=avail . | tail -1)
FREE_GB=$((FREE_KB / 1024 / 1024))
if [ "$FREE_GB" -lt $REQUIRED_FREE_GB ]; then
  echo "ERROR: Not enough free disk space. Need ~${REQUIRED_FREE_GB}GB free. Found ${FREE_GB}GB."
  exit 1
fi

echo "Creating $OUT (10 GB). This will take a while..."
rm -f "$OUT"

# We'll write 1 MiB chunks 10240 times = 10 GiB
# Sprinkle signatures every 50 MB (every 50 iterations)
ITER=10240
SPRINKLE_EVERY=50

for i in $(seq 1 $ITER); do
  dd if=/dev/urandom bs=1M count=1 status=none >> "$OUT"
  if [ $(( i % SPRINKLE_EVERY )) -eq 0 ]; then
    # append a few signature lines to ensure detection
    printf "\nGET /admin_random_%d\nDROP TABLE users_random_%d\nUSER root_%d\nPASS 1234_%d\n" "$i" "$i" "$i" "$i" >> "$OUT"
  fi
  # progress print every 512 MB
  if [ $(( i % 512 )) -eq 0 ]; then
    echo "Progress: $(( i * 1 )) MB written ($(awk -v n="$i" 'BEGIN{printf "%.2f", n/1024}') GB)"
  fi
done

# final size and quick listing
ls -lh "$OUT"
echo "Done. File: $OUT"
