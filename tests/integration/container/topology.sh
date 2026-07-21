#!/usr/bin/env bash
set -euo pipefail

reset_topology() {
  # Forwarding is a property of the router role, not of keen-pbr.  Set both the
  # global and per-interface IPv6 switches because cloud images commonly ship
  # with IPv6 forwarding disabled on every interface.
  sysctl -q -w net.ipv4.ip_forward=1
  sysctl -q -w net.ipv6.conf.all.forwarding=1
  sysctl -q -w net.ipv4.conf.all.rp_filter=0
  local interface
  for interface in lan0 wan_direct wan_pbr; do
    sysctl -q -w "net.ipv4.conf.$interface.rp_filter=0"
    ip link set "$interface" up
  done

  ip route replace 198.18.0.0/24 via 10.10.0.2 dev wan_direct
  ip -6 route replace 2001:db8:100::/64 via 2001:db8:10::2 dev wan_direct
  iptables -P FORWARD ACCEPT
  ip6tables -P FORWARD ACCEPT
}

case "${1:-}" in
  reset) reset_topology ;;
  cleanup) : ;;
  *) echo "usage: topology.sh reset|cleanup" >&2; exit 2 ;;
esac
