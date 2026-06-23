#!/usr/bin/env bash
# scripts/run.sh - one-command launcher.
#
# Examples:
#   ./scripts/run.sh serial   2048 50 0.005
#   ./scripts/run.sh parallel 2048 50 0.005  -np 4 -hostfile hosts
set -euo pipefail

mode="${1:-serial}"; shift || true

case "$mode" in
  serial)
    exec ./nbody_serial "$@"
    ;;
  parallel)
    # split args: everything before "-np"/"-hostfile" goes to the program,
    # the rest goes to mpirun.
    prog_args=()
    mpi_args=()
    passthrough=0
    for a in "$@"; do
      if [[ "$a" == "-np" || "$a" == "-hostfile" || "$a" == "--host" ]]; then
        passthrough=1
      fi
      if [[ $passthrough -eq 0 ]]; then
        prog_args+=("$a")
      else
        mpi_args+=("$a")
      fi
    done
    exec mpirun "${mpi_args[@]}" ./nbody_parallel "${prog_args[@]}"
    ;;
  *)
    echo "usage: $0 {serial|parallel} ..." >&2
    exit 2
    ;;
esac
