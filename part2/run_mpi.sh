#!/usr/bin/env bash
set -euo pipefail

# adjust these variables
MPI_PROCS=4
HOSTFILE="" # e.g., --hostfile hosts.txt
SIGFILE="signatures.txt"
INFILE="big_sample_10GB.bin"
OUTDIR="./dist_out"
BINARY="./mpi_bench"

# compile
mpicxx -O3 -mavx2 -std=c++17 mpi_bench.cpp -o mpi_bench

# create outdir
mkdir -p "$OUTDIR"

# run (example)
if [ -z "$HOSTFILE" ]; then
  mpirun -np "$MPI_PROCS" "$BINARY" "$SIGFILE" "$INFILE" "$OUTDIR"
else
  mpirun --hostfile "$HOSTFILE" -np "$MPI_PROCS" "$BINARY" "$SIGFILE" "$INFILE" "$OUTDIR"
fi
