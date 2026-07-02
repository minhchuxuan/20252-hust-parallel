#!/usr/bin/env bash
# scripts/cluster_up.sh - re-point the cluster after the hotspot changes IPs.
# Run ON node1. You read `ip a` (or `hostname -I`) on each VM and pass the
# current bridged IPs in node order:
#   bash scripts/cluster_up.sh IP_node1 IP_node2 IP_node3 IP_node4
# It rewrites /etc/hosts + the OpenMPI MCA subnet on all 4 nodes, fixes
# known_hosts, then verifies mpirun spans every node. No source is touched.
set -uo pipefail
cd "$(dirname "$0")/.."

PASS="${MPIPASS:-1}"          # cluster sudo/login password (all nodes)
NAMES=(node1 node2 node3 node4)

[[ $# -eq 4 ]] || { echo "usage: $0 IP_node1 IP_node2 IP_node3 IP_node4" >&2; exit 1; }
IPS=("$@")

SUBNET="$(echo "${IPS[0]}" | awk -F. '{print $1"."$2"."$3".0/24"}')"
echo ">>> subnet = $SUBNET"

# --- build the two config blocks locally (no fragile inline escaping) ---
BLOCK=/tmp/_mpi_hosts
: > "$BLOCK"
for i in 0 1 2 3; do printf '%-16s %s  # mpi-cluster\n' "${IPS[$i]}" "${NAMES[$i]}" >> "$BLOCK"; done
echo "=== /etc/hosts block ==="; cat "$BLOCK"

MCA=/tmp/_mpi_mca
cat > "$MCA" <<EOF
btl_tcp_if_include = $SUBNET
oob_tcp_if_include = $SUBNET
btl = tcp,self,vader
mpi_yield_when_idle = 1
EOF

APPLY=/tmp/_mpi_apply.sh
cat > "$APPLY" <<'EOF'
#!/bin/bash
sed -i '/# mpi-cluster/d' /etc/hosts
cat /tmp/_mpi_hosts >> /etc/hosts
EOF

# --- push to every node BY IP (names don't resolve yet) ---
FAIL=0
for i in 0 1 2 3; do
  ip="${IPS[$i]}"; name="${NAMES[$i]}"
  echo "=== $name ($ip) ==="
  ssh-keygen -R "$ip"   >/dev/null 2>&1
  ssh-keygen -R "$name" >/dev/null 2>&1
  if ! ssh -o StrictHostKeyChecking=accept-new -o BatchMode=yes -o ConnectTimeout=5 "mpiuser@$ip" true 2>/dev/null; then
    echo "  !! no passwordless SSH to $ip. Run once:  ssh-copy-id mpiuser@$ip   (pass: $PASS)"
    FAIL=1; continue
  fi
  scp -q -o StrictHostKeyChecking=accept-new "$BLOCK" "$APPLY" "$MCA" "mpiuser@$ip:/tmp/" || { echo "  scp failed"; FAIL=1; continue; }
  ssh "mpiuser@$ip" "printf '%s\n' '$PASS' | sudo -S bash /tmp/_mpi_apply.sh && mkdir -p ~/.openmpi && cp /tmp/_mpi_mca ~/.openmpi/mca-params.conf" 2>/dev/null \
    && echo "  hosts+MCA updated" || { echo "  apply failed"; FAIL=1; }
done

# --- trust new host keys by name, now that /etc/hosts resolves ---
for name in "${NAMES[@]}"; do ssh-keyscan -H "$name" >> ~/.ssh/known_hosts 2>/dev/null; done

echo "=== ping by name ==="
for name in "${NAMES[@]}"; do ping -c1 -W2 "$name" >/dev/null 2>&1 && echo "  $name OK" || { echo "  $name FAIL"; FAIL=1; }; done

if [[ $FAIL -ne 0 ]]; then
  echo ">>> Some steps failed (see above). Fix, then re-run." >&2
  exit 1
fi

echo "=== verify: mpirun spans all nodes ==="
mpirun -np 8 -hostfile scripts/hostfile.openmpi --map-by node ./hello_mpi
echo ">>> cluster is up."
