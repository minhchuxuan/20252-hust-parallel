#!/usr/bin/env bash
# scripts/distribute.sh - rsync the built project to every node.
#
# Usage: ./scripts/distribute.sh [HOSTFILE] [REMOTE_PATH]
#   HOSTFILE defaults to scripts/hosts.list
#   REMOTE_PATH defaults to the current absolute path (assumes same layout)
set -euo pipefail

HOSTS="${1:-scripts/hosts.list}"
DEST="${2:-$PWD}"
USER_NAME="${MPI_USER:-$USER}"

[[ -f "$HOSTS" ]] || { echo "missing hosts list: $HOSTS" >&2; exit 1; }

# Host list on FD 3 so the inner ssh cannot consume it from stdin.
while read -r host <&3; do
  [[ -z "$host" || "$host" =~ ^# ]] && continue
  echo ">>> rsync -> $host:$DEST"
  ssh -n "$USER_NAME@$host" "mkdir -p '$DEST'"
  rsync -az --delete \
      --exclude='.git' --exclude='bench/results*.csv' --exclude='data/*.csv' \
      ./ "$USER_NAME@$host:$DEST/"
done 3< "$HOSTS"
echo "OK: distributed to all hosts"
