#!/usr/bin/env bash
# bench/bench.sh - strong-scaling sweep (fixed problem size, varying ranks).
#
# Supports the speedup study: fix N, sweep the MPI rank count over a doubling
# sequence 1,2,4,8,...,2X, and record for each point the median total wall time
# and the median communication time (so runtime with and without communication,
# and speedup, can be plotted from one data file).
#
# Usage:
#   ./bench/bench.sh [N=20000] [STEPS=20] [DT=0.01] [HOSTFILE]
# Env:
#   NPS="1 2 4 8 16"   rank sequence (default doubling up to 16)
#   REPEATS=5          runs per point (median reported)
set -euo pipefail
cd "$(dirname "$0")/.."
source bench/_lib.sh

N="${1:-20000}"
STEPS="${2:-20}"
DT="${3:-0.01}"
HOSTFILE="${4:-}"
REPEATS="${REPEATS:-5}"
NPS="${NPS:-1 2 4 8 16}"

mpiargs=(--oversubscribe ${EXTRA:-})
[[ -n "$HOSTFILE" ]] && mpiargs=(-hostfile "$HOSTFILE" --oversubscribe ${EXTRA:-})

OUT=bench/results.csv
mkdir -p bench
echo "np,n,steps,dt,total_s,comm_s" > "$OUT"

for np in $NPS; do
  totals=(); comms=()
  for ((i = 0; i < REPEATS; i++)); do
    out=$(mpirun -np "$np" "${mpiargs[@]}" ./nbody_parallel "$N" "$STEPS" "$DT")
    t=$(printf '%s\n' "$out" | field time)
    c=$(printf '%s\n' "$out" | field comm)
    [[ -n "$t" ]] || { echo "ERROR: run failed (np=$np): $out" >&2; exit 1; }
    totals+=("$t"); comms+=("$c")
  done
  mt=$(printf '%s\n' "${totals[@]}" | median)
  mc=$(printf '%s\n' "${comms[@]}"  | median)
  echo "$np,$N,$STEPS,$DT,$mt,$mc" | tee -a "$OUT"
done
echo "wrote $OUT"
