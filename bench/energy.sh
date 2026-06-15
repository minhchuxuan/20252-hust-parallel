#!/usr/bin/env bash
# bench/energy.sh - energy-conservation figure. Runs the sequential simulation,
# dumps the state periodically, computes total energy at each dump, and plots
# energy versus time. Confirms the integrator is numerically stable.
#
# Usage:
#   ./bench/energy.sh [N=3000] [STEPS=200] [DT=0.01]   (env: EVERY=20)
set -euo pipefail
cd "$(dirname "$0")/.."

N="${1:-3000}"
STEPS="${2:-200}"
DT="${3:-0.01}"
EVERY="${EVERY:-20}"

mkdir -p data/frames bench
rm -f data/frames/e_*.csv
DUMP_EVERY="$EVERY" DUMP_PREFIX=data/frames/e ./nbody_serial "$N" "$STEPS" "$DT" >/dev/null

OUT=bench/energy.csv
echo "step,E" > "$OUT"
for f in data/frames/e_*.csv; do
  idx=$(basename "$f" | sed -E 's/e_0*([0-9]+)\.csv/\1/')
  idx=$((10#$idx))                 # strip leading zeros (avoid octal)
  E=$(./tests/energy_check "$f" | awk '{print $NF}' | sed 's/E=//')
  echo "$((idx * EVERY)),$E" >> "$OUT"
done

python3 bench/plot.py "$OUT" --energy
cp -f bench/energy.png report/figures/ 2>/dev/null || true
echo "wrote bench/energy.png (and copied to report/figures/)"
