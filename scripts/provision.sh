#!/usr/bin/env bash
# scripts/provision.sh - bring a fresh Ubuntu 22.04 VM to cluster-ready state.
#
# Idempotent: re-running is safe. Run as the cluster user (e.g. mpiuser) on
# every node. The MASTER node additionally runs scripts/setup_ssh.sh to push
# its public key to workers.
#
# Tasks performed (matches task.md §4):
#   - install build-essential, openmpi-bin, libopenmpi-dev, openssh-server
#   - enable & start sshd
#   - ensure an ~/.ssh exists with correct perms
#   - add cluster hosts to /etc/hosts if HOSTS_FILE is provided
#   - print a summary so you can tick M3
set -euo pipefail

PKGS=(build-essential openmpi-bin libopenmpi-dev openssh-server rsync)

log() { printf '[provision] %s\n' "$*"; }

need_root() {
  if [[ $EUID -ne 0 ]]; then
    log "re-execing with sudo"
    exec sudo -E bash "$0" "$@"
  fi
}

install_packages() {
  log "apt update && install ${PKGS[*]}"
  apt-get update -y
  DEBIAN_FRONTEND=noninteractive apt-get install -y "${PKGS[@]}"
}

enable_sshd() {
  log "enabling sshd"
  systemctl enable --now ssh
}

write_hosts() {
  local src="${HOSTS_FILE:-}"
  [[ -z "$src" ]] && { log "HOSTS_FILE not set, skipping /etc/hosts edit"; return; }
  [[ -f "$src" ]] || { log "HOSTS_FILE=$src missing"; return 1; }
  log "merging $src into /etc/hosts (entries tagged # mpi-cluster)"
  # remove prior managed lines
  sed -i '/# mpi-cluster$/d' /etc/hosts
  # append new ones
  while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    printf '%s  # mpi-cluster\n' "$line" >> /etc/hosts
  done < "$src"
}

ensure_ssh_dir() {
  local u="${SUDO_USER:-$USER}"
  local home
  home=$(getent passwd "$u" | cut -d: -f6)
  install -d -m 700 -o "$u" -g "$u" "$home/.ssh"
  touch "$home/.ssh/authorized_keys"
  chmod 600 "$home/.ssh/authorized_keys"
  chown "$u:$u" "$home/.ssh/authorized_keys"
}

summary() {
  log "done."
  log "mpirun : $(command -v mpirun || echo MISSING)"
  log "mpicc  : $(command -v mpicc  || echo MISSING)"
  log "sshd   : $(systemctl is-active ssh || true)"
  log "hostname: $(hostname)"
  log "IP     : $(hostname -I || true)"
}

need_root "$@"
install_packages
enable_sshd
write_hosts
ensure_ssh_dir
summary
