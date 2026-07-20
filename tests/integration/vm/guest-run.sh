#!/usr/bin/env bash
set -euo pipefail

backend=${1:?usage: guest-run.sh <iptables|nftables>}
repo=/mnt/payload
seed=/mnt/seed
status_file="$seed/result"

on_error() {
  local status=$?
  echo "guest-run failed at line $1: $2 (status $status)" >&2
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

start_dnsmasq() {
  if systemctl restart dnsmasq.service; then
    return 0
  fi
  echo 'dnsmasq failed after the integration configuration was installed' >&2
  systemctl --no-pager --full status dnsmasq.service >&2 || true
  journalctl --no-pager -u dnsmasq.service >&2 || true
  echo '--- /etc/dnsmasq.conf ---' >&2
  sed -n '1,240p' /etc/dnsmasq.conf >&2 || true
  return 1
}

assert_dnsmasq_active() {
  if systemctl is-active --quiet dnsmasq.service; then
    return 0
  fi
  echo 'dnsmasq stopped while keen-pbr was starting' >&2
  systemctl --no-pager --full status keen-pbr.service dnsmasq.service >&2 || true
  journalctl --no-pager -u keen-pbr.service -u dnsmasq.service >&2 || true
  echo '--- /etc/dnsmasq.conf ---' >&2
  sed -n '1,240p' /etc/dnsmasq.conf >&2 || true
  echo '--- keen-pbr generated resolver config ---' >&2
  /usr/sbin/keen-pbr generate-resolver-config dnsmasq >&2 || true
  return 1
}
mkdir -p "$repo" "$seed"
mountpoint -q "$repo" || mount -o ro /dev/vdc "$repo"
mount /dev/vdb "$seed"
trap finish EXIT

export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' >/usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d
# Prevent package maintainer scripts from creating a start/stop storm before
# the VM topology and the final dnsmasq configuration exist.
systemctl mask dnsmasq.service
ln -s /dev/null /etc/systemd/system/keen-pbr.service
apt-get update
apt-get install -y --no-install-recommends \
  conntrack curl dnsmasq dnsutils iproute2 ipset iptables \
  libcurl4 libnl-3-200 libnl-route-3-200 libunwind8 nftables python3

KEEN_PBR_REPLACE_DNSMASQ_DEFAULTS=Y dpkg -i "$repo/keen-pbr.deb"
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

export KPBR_INTEGRATION_CONTAINER_DIR="$repo/tests/integration/container"
source "$repo/tests/integration/container/topology.sh"
trap 'status=$?; cleanup_topology; finish "$status"' EXIT

systemctl stop keen-pbr.service dnsmasq.service >/dev/null 2>&1 || true
prepare_topology
sed "s/BACKEND/$backend/g" "$repo/tests/integration/container/config.json" >/etc/keen-pbr/config.json

start_dnsmasq
systemctl start keen-pbr.service
sleep 1
assert_dnsmasq_active
python3 "$repo/tests/integration/container/test-system.py" "$backend"
