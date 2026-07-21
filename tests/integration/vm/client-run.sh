#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends dnsutils iproute2 openssh-server python3
systemctl enable --now ssh.service

# cloud-init owns the base addresses and routes.  These secondary addresses are
# used by source-address routing cases and are reset before each case.
ip address replace 192.0.2.3/24 dev lan0
ip -6 address replace 2001:db8:1::3/64 dev lan0
ip route del default dev mgmt0 >/dev/null 2>&1 || true
ip -6 route del default dev mgmt0 >/dev/null 2>&1 || true
ip route replace default via 192.0.2.1 dev lan0 metric 10
ip -6 route replace default via 2001:db8:1::1 dev lan0 metric 10

touch /mnt/seed/ready
echo "KPBR_IT_EVENT backend=fixture case=suite stage=provision role=client status=pass"
exec sleep infinity
