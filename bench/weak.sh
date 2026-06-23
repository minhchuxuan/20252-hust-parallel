#!/usr/bin/env bash
# bench/weak.sh - weak-scaling sweep.
#
# Grows the problem size with the rank count so the work per rank stays roughly
# constant. For the tree algorithm the work per rank is O((N/P) log N); we use
# the common N(P) = N0 * P approximation. Records median total and median
# communication time at each point.
#
# Usage:
#   ./bench/weak.sh [N0=4000] [STEPS=20] [DT=0.01] [HOSTFILE]
# Env: NPS="1 2 4 8 16" REPEATS=3
set -euo pipefail
cd "$(dirname "$0")/.."
source bench/_lib.sh

N0="${1:-4000}"
STEPS="${2:-20}"
DT="${3:-0.01}"
HOSTFILE="${4:-}"
REPEATS="${REPEATS:-3}"
NPS="${NPS:-1 2 4 8 16}"

mpiargs=(--oversubscribe ${EXTRA:-})
[[ -n "$HOSTFILE" ]] && mpiargs=(-hostfile "$HOSTFILE" --oversubscribe ${EXTRA:-})

OUT=bench/weak.csv
mkdir -p bench
echo "np,n,steps,dt,total_s,comm_s" > "$OUT"

for np in $NPS; do
  N=$((N0 * np))
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
