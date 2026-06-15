#!/usr/bin/env bash
# bench/granularity.sh - per-process compute/communication breakdown.
#
# Runs one simulation at a fixed problem size with the rank count equal to the
# number of cores, capturing every rank's build/force/comm/integ times to a CSV
# (one row per process). bench/plot.py --gran turns this into a per-process
# stacked bar and checks the load-balance / idle-time rule.
#
# Usage:
#   ./bench/granularity.sh [N=20000] [STEPS=30] [DT=0.01] [HOSTFILE]
# Env:
#   P=<ranks>   rank count (default: number of cores)
#   INPUT=file  optional initial-condition CSV (use a clustered input to expose
#               load imbalance); default is the built-in Plummer model
set -euo pipefail
cd "$(dirname "$0")/.."

P="${P:-$(nproc)}"
N="${1:-20000}"
STEPS="${2:-30}"
DT="${3:-0.01}"
HOSTFILE="${4:-}"
INPUT="${INPUT:-}"

mpiargs=(--oversubscribe)
[[ -n "$HOSTFILE" ]] && mpiargs=(-hostfile "$HOSTFILE" --oversubscribe)

mkdir -p bench
OUT=bench/timing.csv

if [[ -n "$INPUT" ]]; then
  TIMING_CSV="$OUT" mpirun -np "$P" "${mpiargs[@]}" -x TIMING_CSV \
    ./nbody_parallel --from "$INPUT" "$STEPS" "$DT"
else
  TIMING_CSV="$OUT" mpirun -np "$P" "${mpiargs[@]}" -x TIMING_CSV \
    ./nbody_parallel "$N" "$STEPS" "$DT"
fi
echo "wrote $OUT (per-rank build/force/comm/integ/total)"
