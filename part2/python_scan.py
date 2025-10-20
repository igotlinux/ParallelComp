#!/usr/bin/env python3
# python_scan.py
# Usage: python3 python_scan.py signatures.txt input.bin start length out.json

import sys, json

if len(sys.argv) != 6:
    print("Usage: python3 python_scan.py signatures.txt input.bin start length out.json")
    sys.exit(1)

sigs_file, infile, start, length, outpath = sys.argv[1:]
start = int(start); length = int(length)

# read signatures
with open(sigs_file) as f:
    sigs = [line.strip() for line in f if line.strip()]

# read chunk
with open(infile, "rb") as f:
    f.seek(0, 2)
    filesize = f.tell()
    if start >= filesize:
        data = b""
    else:
        f.seek(start)
        toread = min(length, filesize - start)
        data = f.read(toread)

out = {}
for s in sigs:
    pat = s.encode(errors="ignore")
    if not pat: continue
    matches = []
    idx = 0
    while True:
        pos = data.find(pat, idx)
        if pos == -1: break
        matches.append(start + pos)
        idx = pos + 1
    if matches:
        out[s] = matches

out["meta"] = {"start": start, "length": len(data)}
with open(outpath, "w") as f:
    json.dump(out, f, indent=2)

print(f"Scanned {len(data)} bytes from offset {start}, wrote {outpath}")
