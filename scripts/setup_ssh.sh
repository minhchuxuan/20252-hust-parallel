#!/usr/bin/env bash
# scripts/setup_ssh.sh - generate key on master + push to workers.
#
# Run ON THE MASTER node, as the cluster user.
# Reads scripts/hosts.list (one "node<name>" per line, must already resolve).
# Prompts for password the first time, silent thereafter.
set -euo pipefail

HOSTS="${1:-scripts/hosts.list}"
USER_NAME="${MPI_USER:-$USER}"

[[ -f "$HOSTS" ]] || { echo "missing hosts list: $HOSTS" >&2; exit 1; }

if [[ ! -f "$HOME/.ssh/id_ed25519" && ! -f "$HOME/.ssh/id_rsa" ]]; then
  ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519"
fi

# Read the host list on FD 3 so the ssh / ssh-copy-id calls inside the loop
# cannot swallow it from stdin (which would silently skip later hosts).
while read -r host <&3; do
  [[ -z "$host" || "$host" =~ ^# ]] && continue
  echo ">>> $host"
  ssh-copy-id -o StrictHostKeyChecking=accept-new "$USER_NAME@$host" </dev/null || true
  ssh -n -o BatchMode=yes "$USER_NAME@$host" hostname || {
    echo "FAIL: passwordless ssh to $host not working" >&2
    exit 1
  }
done 3< "$HOSTS"

echo "OK: passwordless SSH verified for all hosts in $HOSTS"
