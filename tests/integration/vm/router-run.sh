#!/usr/bin/env bash
set -euo pipefail

backend=${1:?usage: router-run.sh <iptables|nftables> [cases]}
integration_cases=${2:-all}
repo=/mnt/payload
seed=/mnt/seed
status_file="$seed/result"
summary_file="$seed/summary.json"
provision_log="$seed/provision.log"
ssh_key=/root/.ssh/kpbr-integration
source "$repo/guest-lib.sh"

on_error() {
  local status=$?
  echo "KPBR_IT_DIAG backend=$backend case=suite stage=guest message=failed_at_line_$1_status_$status" >&2
  printf 'router-run failed at line %s: %s (status %s)\n' "$1" "$2" "$status" >"$seed/diagnostics.txt"
  exit "$status"
}
trap 'on_error "$LINENO" "$BASH_COMMAND"' ERR

finish() {
  local status=${1:-$?}
  trap - EXIT
  printf '%s\n' "$status" >"$status_file"
  sync
  systemctl poweroff --no-wall || poweroff -f || true
  exit "$status"
}
trap finish EXIT

install -d -m 0700 /root/.ssh
install -m 0600 "$repo/router-key" "$ssh_key"

export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' >/usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d
systemctl mask dnsmasq.service
ln -sf /dev/null /etc/systemd/system/keen-pbr.service
apt-get update
apt-get install -y --no-install-recommends \
  conntrack curl dnsmasq dnsutils iproute2 ipset iptables \
  libcurl4 libnl-3-200 libnl-route-3-200 libunwind8 nftables openssh-client python3

KEEN_PBR_REPLACE_DNSMASQ_DEFAULTS=Y dpkg -i "$repo/keen-pbr.deb"
rm -f /usr/sbin/policy-rc.d
systemctl unmask dnsmasq.service keen-pbr.service
mkdir -p /etc/systemd/system/keen-pbr.service.d /etc/systemd/system/dnsmasq.service.d
printf '%s\n' '[Unit]' 'StartLimitIntervalSec=0' '[Service]' 'Restart=no' 'RestartSec=0' \
  >/etc/systemd/system/keen-pbr.service.d/integration.conf
printf '%s\n' '[Unit]' 'StartLimitIntervalSec=0' '[Service]' 'Restart=no' 'RestartSec=0' \
  >/etc/systemd/system/dnsmasq.service.d/integration.conf
systemctl daemon-reload
systemctl reset-failed dnsmasq.service keen-pbr.service >/dev/null 2>&1 || true

bash "$repo/tests/integration/container/topology.sh" reset

ssh_ready() {
  ssh -i "$ssh_key" -o BatchMode=yes -o ConnectTimeout=2 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "root@$1" true
}
for host in 192.0.2.2 10.10.0.2 10.20.0.2; do
  wait_for_peer_ssh "$host" 90 1 || {
    echo "peer SSH readiness timed out: $host" >&2
    exit 1
  }
done

fixture_ready() {
  curl --fail --silent --max-time 2 "http://$1:18080/health" >/dev/null &&
    dig +time=1 +tries=1 +short fixture-ready.test "@$2" -p "$3" | grep -q .
}
ready=0
for _ in $(seq 1 40); do
  if fixture_ready 198.18.0.10 10.20.0.2 15353; then ready=1; break; fi
  sleep 0.25
done
[[ $ready == 1 ]] || { echo "WAN fixtures did not become ready" >&2; exit 1; }

echo "KPBR_IT_EVENT backend=$backend case=suite stage=provision role=router status=pass"
export KPBR_INTEGRATION_CONTAINER_DIR="$repo/tests/integration/container"
export KPBR_SSH_KEY="$ssh_key"
export INTEGRATION_CASES="$integration_cases"
export KPBR_IT_SUMMARY="$summary_file"
python3 "$repo/tests/integration/container/test-system.py" "$backend"
