#!/usr/bin/env bash
set -euo pipefail

backend=${1:?usage: guest-run.sh <iptables|nftables> [cases]}
integration_cases=${2:-all}
repo=/mnt/payload
seed=/mnt/seed
status_file="$seed/result"
summary_file="$seed/summary.json"
provision_log=/var/log/kpbr-integration-provision.log

on_error() {
  local status=$?
  echo "KPBR_IT_DIAG backend=$backend case=suite stage=guest message=failed_at_line_$1_status_$status" >&2
  printf 'guest-run failed at line %s: %s (status %s)\n' "$1" "$2" "$status" >"$seed/diagnostics.txt"
  cp "$provision_log" "$seed/provision.log" >/dev/null 2>&1 || true
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

mkdir -p "$repo" "$seed"
mountpoint -q "$repo" || mount -o ro /dev/vdc "$repo"
mount /dev/vdb "$seed"
trap finish EXIT
echo "KPBR_IT_BEGIN backend=$backend case=suite stage=provision"

export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' >/usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d
# Prevent package maintainer scripts from creating a start/stop storm before
# the VM topology and the final dnsmasq configuration exist.
systemctl mask dnsmasq.service
ln -s /dev/null /etc/systemd/system/keen-pbr.service
apt-get update >"$provision_log" 2>&1
apt-get install -y --no-install-recommends \
  conntrack curl dnsmasq dnsutils iproute2 ipset iptables \
  libcurl4 libnl-3-200 libnl-route-3-200 libunwind8 nftables python3 \
  >>"$provision_log" 2>&1

KEEN_PBR_REPLACE_DNSMASQ_DEFAULTS=Y dpkg -i "$repo/keen-pbr.deb" >>"$provision_log" 2>&1
rm -f /usr/sbin/policy-rc.d
systemctl unmask dnsmasq.service keen-pbr.service
mkdir -p /etc/systemd/system/keen-pbr.service.d /etc/systemd/system/dnsmasq.service.d
printf '%s\n' \
  '[Unit]' \
  'StartLimitIntervalSec=0' \
  '[Service]' \
  'Restart=no' \
  'RestartSec=0' >/etc/systemd/system/keen-pbr.service.d/integration.conf
printf '%s\n' \
  '[Unit]' \
  'StartLimitIntervalSec=0' \
  '[Service]' \
  'Restart=no' \
  'RestartSec=0' >/etc/systemd/system/dnsmasq.service.d/integration.conf
systemctl daemon-reload
# There may be no failed state (and systemd may report an unloaded unit in
# that case); it is not an integration-test failure.
systemctl reset-failed dnsmasq.service keen-pbr.service >/dev/null 2>&1 || true

echo "KPBR_IT_EVENT backend=$backend case=suite stage=provision status=pass"
export KPBR_INTEGRATION_CONTAINER_DIR="$repo/tests/integration/container"
export INTEGRATION_CASES="$integration_cases"
export KPBR_IT_SUMMARY="$summary_file"
python3 "$repo/tests/integration/container/test-system.py" "$backend"
