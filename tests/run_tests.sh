#!/usr/bin/env bash
# tests/run_tests.sh - correctness harness (M4/M5 in task.md).
#
# Checks:
#   T1  serial runs and produces finite output for a small input
#   T2  parallel np=1 ≡ serial (bit-for-bit up to CSV formatting)
#   T3  parallel np=2 ≈ serial (within CSV tolerance)
#   T4  parallel np=4 ≈ serial
#   T5  energy drift over 50 steps is bounded (< 5% for this integrator)
#   T6  input generator round-trips via --from
set -euo pipefail

cd "$(dirname "$0")/.."

PASS=0
FAIL=0
check() {
  local name="$1"; shift
  echo "=== $name ==="
  if "$@"; then
    echo "  PASS: $name"
    PASS=$((PASS+1))
  else
    echo "  FAIL: $name"
    FAIL=$((FAIL+1))
  fi
}

make -s all gen_input compare_csv energy_check

mkdir -p tests/out
rm -f tests/out/*.csv

# T1: serial runs and produces an output file with the right row count.
check T1_serial_runs bash -c '
  ./nbody_serial 128 5 0.01 tests/out/t1.csv &&
  lines=$(wc -l < tests/out/t1.csv) &&
  [[ "$lines" -eq 129 ]]   # header + 128 rows
'

# T2: parallel np=1 matches serial closely.
check T2_np1_matches_serial bash -c '
  ./nbody_serial                128 10 0.01 tests/out/s.csv &&
  mpirun -np 1 ./nbody_parallel 128 10 0.01 tests/out/p1.csv &&
  ./tests/compare_csv tests/out/s.csv tests/out/p1.csv 1e-10 1e-12
'

# T3/T4: tolerate tiny float rounding from Allgather reordering.
check T3_np2_matches_serial bash -c '
  mpirun -np 2 ./nbody_parallel 128 10 0.01 tests/out/p2.csv &&
  ./tests/compare_csv tests/out/s.csv tests/out/p2.csv 1e-6 1e-9
'
check T4_np4_matches_serial bash -c '
  mpirun -np 4 --oversubscribe ./nbody_parallel 128 10 0.01 tests/out/p4.csv &&
  ./tests/compare_csv tests/out/s.csv tests/out/p4.csv 1e-6 1e-9
'

# T5: energy drift. Semi-implicit Euler drifts monotonically but slowly at
# this dt; we just check the magnitude is bounded.
check T5_energy_bounded bash -c '
  ./gen_input 256 tests/out/init.csv 7 plummer &&
  ./nbody_serial --from tests/out/init.csv 0  0.01 tests/out/e0.csv &&
  ./nbody_serial --from tests/out/init.csv 50 0.01 tests/out/e1.csv &&
  e0=$(./tests/energy_check tests/out/e0.csv | awk "{for(i=1;i<=NF;i++) if(\$i ~ /^E=/){gsub(/E=/,\"\",\$i); print \$i}}") &&
  e1=$(./tests/energy_check tests/out/e1.csv | awk "{for(i=1;i<=NF;i++) if(\$i ~ /^E=/){gsub(/E=/,\"\",\$i); print \$i}}") &&
  awk -v a="$e0" -v b="$e1" "BEGIN{d=(b-a)/a; if(d<0)d=-d; exit !(d<0.05)}"
'

# T6: gen_input -> serial --from round-trip
check T6_gen_input_roundtrip bash -c '
  ./gen_input 64 tests/out/g.csv 1 disk &&
  ./nbody_serial --from tests/out/g.csv 5 0.005 tests/out/g_out.csv &&
  [[ -s tests/out/g_out.csv ]]
'

echo
echo "RESULT: $PASS passed, $FAIL failed"
exit $FAIL
