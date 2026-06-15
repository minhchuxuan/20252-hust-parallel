#!/usr/bin/env bash
# bench/hybrid.sh - sweep an (MPI-ranks, OpenMP-threads) grid for the hybrid
# binary; record the median wall time at each grid point.
#
# Usage:
#   ./bench/hybrid.sh [N=20000] [STEPS=20] [DT=0.01] [HOSTFILE]
# Env: NPS="1 2 3"  THREADS="1 2 4"  REPEATS=3
set -euo pipefail
cd "$(dirname "$0")/.."
source bench/_lib.sh

N="${1:-20000}"
STEPS="${2:-20}"
DT="${3:-0.01}"
HOSTFILE="${4:-}"
REPEATS="${REPEATS:-3}"
NPS="${NPS:-1 2 3}"
THREADS="${THREADS:-1 2}"

mpiargs=(--oversubscribe)
[[ -n "$HOSTFILE" ]] && mpiargs=(-hostfile "$HOSTFILE" --oversubscribe)

OUT=bench/hybrid.csv
mkdir -p bench
echo "np,threads,n,steps,dt,median_s" > "$OUT"

for np in $NPS; do
  for t in $THREADS; do
    ts=()
    for ((i = 0; i < REPEATS; i++)); do
      out=$(OMP_NUM_THREADS="$t" OMP_PROC_BIND=close OMP_PLACES=cores \
            mpirun -np "$np" "${mpiargs[@]}" \
            -x OMP_NUM_THREADS -x OMP_PROC_BIND -x OMP_PLACES \
            ./nbody_hybrid "$N" "$STEPS" "$DT")
      tw=$(printf '%s\n' "$out" | field time)
      [[ -n "$tw" ]] || { echo "ERROR: run failed (np=$np t=$t): $out" >&2; exit 1; }
      ts+=("$tw")
    done
    med=$(printf '%s\n' "${ts[@]}" | median)
    echo "$np,$t,$N,$STEPS,$DT,$med" | tee -a "$OUT"
  done
done
echo "wrote $OUT"
