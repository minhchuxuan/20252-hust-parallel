#!/usr/bin/env bash
# bench/scaling_n.sh - runtime versus input size, at a fixed rank count.
#
# Fixes the number of MPI ranks at the number of physical cores (P) and sweeps
# the problem size N, recording median total time and median communication time.
# Used to plot runtime (with and without communication) against N and to choose
# the N whose wall time lands in the 2-3 minute target band.
#
# Usage:
#   ./bench/scaling_n.sh
# Env:
#   P=<ranks>          rank count (default: number of cores on this machine)
#   NS="2000 4000 ..." problem sizes to sweep
#   STEPS=20 DT=0.01 REPEATS=3 HOSTFILE=...
set -euo pipefail
cd "$(dirname "$0")/.."
source bench/_lib.sh

P="${P:-$(nproc)}"
STEPS="${STEPS:-20}"
DT="${DT:-0.01}"
HOSTFILE="${HOSTFILE:-}"
REPEATS="${REPEATS:-3}"
NS="${NS:-2000 4000 8000 16000 32000 64000}"

mpiargs=(--oversubscribe ${EXTRA:-})
[[ -n "$HOSTFILE" ]] && mpiargs=(-hostfile "$HOSTFILE" --oversubscribe ${EXTRA:-})

OUT=bench/scaling_n.csv
mkdir -p bench
echo "n,np,steps,dt,total_s,comm_s" > "$OUT"

for N in $NS; do
  totals=(); comms=()
  for ((i = 0; i < REPEATS; i++)); do
    out=$(mpirun -np "$P" "${mpiargs[@]}" ./nbody_parallel "$N" "$STEPS" "$DT")
    t=$(printf '%s\n' "$out" | field time)
    c=$(printf '%s\n' "$out" | field comm)
    [[ -n "$t" ]] || { echo "ERROR: run failed (N=$N): $out" >&2; exit 1; }
    totals+=("$t"); comms+=("$c")
  done
  mt=$(printf '%s\n' "${totals[@]}" | median)
  mc=$(printf '%s\n' "${comms[@]}"  | median)
  echo "$N,$P,$STEPS,$DT,$mt,$mc" | tee -a "$OUT"
done
echo "wrote $OUT"
