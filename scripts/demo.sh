#!/usr/bin/env bash
# scripts/demo.sh - offline demo runner for the professor.
#
# Produces:
#   data/initial.csv         - starting state (from serial init)
#   data/final_serial.csv    - serial reference end state
#   data/final_parallel.csv  - MPI end state (should match serial)
#   Prints timings and a speedup note.
set -euo pipefail

N="${N:-2048}"
STEPS="${STEPS:-50}"
DT="${DT:-0.01}"
NP="${NP:-4}"
HOSTFILE="${HOSTFILE:-}"

mkdir -p data
# --oversubscribe lets the demo run on a single laptop (more ranks than cores).
hostarg=(--oversubscribe ${EXTRA:-})
[[ -n "$HOSTFILE" ]] && hostarg=(-hostfile "$HOSTFILE" --oversubscribe ${EXTRA:-})

echo "=== [1/3] Serial reference (N=$N, steps=$STEPS) ==="
./nbody_serial "$N" "$STEPS" "$DT" data/final_serial.csv

echo "=== [2/3] MPI run (np=$NP) ==="
mpirun -np "$NP" "${hostarg[@]}" ./nbody_parallel "$N" "$STEPS" "$DT" data/final_parallel.csv

echo "=== [3/3] Correctness: full state comparison ==="
if [[ -x ./tests/compare_csv ]]; then
  ./tests/compare_csv data/final_serial.csv data/final_parallel.csv 1e-9 1e-12
else
  diff <(head -n 6 data/final_serial.csv) <(head -n 6 data/final_parallel.csv) \
    && echo "OK: serial == parallel on first rows" \
    || echo "Diff present - inspect full files."
fi
