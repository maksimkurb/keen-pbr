#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname "$0")/common.sh"

topology_failure() {
  local status=$? command_name=${BASH_COMMAND%%[[:space:]]*} source_line=${BASH_LINENO[0]:-?}
  printf 'KPBR_IT_DIAG backend=harness case=suite stage=topology command=topology.sh:%s/%s line=%s exit=%s\n' \
    "${FUNCNAME[1]:-main}" "$command_name" "$source_line" "$status" >&2
  exit "$status"
}
trap topology_failure ERR

configure_ipv6() {
  local ns=${1:-} iface=${2:-}
  if [[ -n "$ns" ]]; then
    ns_exec "$ns" sysctl -q -w net.ipv6.conf.all.disable_ipv6=0
    ns_exec "$ns" sysctl -q -w net.ipv6.conf.default.disable_ipv6=0
    if [[ -n "$iface" ]]; then
      ns_exec "$ns" sysctl -q -w "net.ipv6.conf.$iface.disable_ipv6=0"
    fi
  else
    sysctl -q -w net.ipv6.conf.all.disable_ipv6=0
    sysctl -q -w net.ipv6.conf.default.disable_ipv6=0
    if [[ -n "$iface" ]]; then
      sysctl -q -w "net.ipv6.conf.$iface.disable_ipv6=0"
    fi
  fi
}

configure_rpf() {
  local ns=${1:-} iface=${2:-}
  if [[ -n "$ns" ]]; then
    ns_exec "$ns" sysctl -q -w net.ipv4.conf.all.rp_filter=0
    ns_exec "$ns" sysctl -q -w net.ipv4.conf.default.rp_filter=0
    if [[ -n "$iface" ]]; then
      ns_exec "$ns" sysctl -q -w "net.ipv4.conf.$iface.rp_filter=0"
    fi
  else
    sysctl -q -w net.ipv4.conf.all.rp_filter=0
    sysctl -q -w net.ipv4.conf.default.rp_filter=0
    if [[ -n "$iface" ]]; then
      sysctl -q -w "net.ipv4.conf.$iface.rp_filter=0"
    fi
  fi
}

up() {
  require_commands ip sysctl

  # The outer unshare() network namespace is the router.  Before we add veths
  # it must have no link to the host network at all.
  local non_loopback
  non_loopback=$(ip -o link show | awk -F': ' '$2 != "lo" {print $2}')
  [[ -z "$non_loopback" ]] || die "sandbox router unexpectedly inherited a non-loopback interface"
  [[ -z "$(ip route show default)" ]] || die "sandbox router unexpectedly has an IPv4 default route"
  [[ -z "$(ip -6 route show default)" ]] || die "sandbox router unexpectedly has an IPv6 default route"

  for ns in "$NS_CLIENT" "$NS_DIRECT" "$NS_PBR"; do
    ns_exists "$ns" && ip netns del "$ns"
    ip netns add "$ns"
    ip -n "$ns" link set lo up
    configure_ipv6 "$ns"
    configure_rpf "$ns"
  done

  ip link set lo up
  configure_ipv6 ""
  configure_rpf ""

  ip link add lan0 type veth peer name kpbr_lan_peer
  ip link set kpbr_lan_peer netns "$NS_CLIENT"
  ip -n "$NS_CLIENT" link set kpbr_lan_peer name lan0

  ip link add wan_direct type veth peer name kpbr_dir_peer
  ip link set kpbr_dir_peer netns "$NS_DIRECT"
  ip -n "$NS_DIRECT" link set kpbr_dir_peer name wan_direct

  ip link add wan_pbr type veth peer name kpbr_pbr_peer
  ip link set kpbr_pbr_peer netns "$NS_PBR"
  ip -n "$NS_PBR" link set kpbr_pbr_peer name wan_pbr

  for iface in lan0 wan_direct wan_pbr; do
    ip link set "$iface" up
    configure_ipv6 "" "$iface"
    configure_rpf "" "$iface"
  done
  ip -n "$NS_CLIENT" link set lan0 up
  ip -n "$NS_DIRECT" link set wan_direct up
  ip -n "$NS_PBR" link set wan_pbr up
  configure_ipv6 "$NS_CLIENT" lan0
  configure_ipv6 "$NS_DIRECT" wan_direct
  configure_ipv6 "$NS_PBR" wan_pbr
  configure_rpf "$NS_CLIENT" lan0
  configure_rpf "$NS_DIRECT" wan_direct
  configure_rpf "$NS_PBR" wan_pbr

  # Keep the exact addressing used by the old QEMU harness so all existing
  # integration cases run unchanged.
  ip address add 192.0.2.1/24 dev lan0
  ip -6 address add 2001:db8:1::1/64 dev lan0 nodad
  ip -n "$NS_CLIENT" address add 192.0.2.2/24 dev lan0
  ip -n "$NS_CLIENT" address add 192.0.2.3/24 dev lan0
  ip -n "$NS_CLIENT" -6 address add 2001:db8:1::2/64 dev lan0 nodad
  ip -n "$NS_CLIENT" -6 address add 2001:db8:1::3/64 dev lan0 nodad

  ip address add 10.10.0.1/24 dev wan_direct
  ip -6 address add 2001:db8:10::1/64 dev wan_direct nodad
  ip -n "$NS_DIRECT" address add 10.10.0.2/24 dev wan_direct
  ip -n "$NS_DIRECT" -6 address add 2001:db8:10::2/64 dev wan_direct nodad

  ip address add 10.20.0.1/24 dev wan_pbr
  ip -6 address add 2001:db8:20::1/64 dev wan_pbr nodad
  ip -n "$NS_PBR" address add 10.20.0.2/24 dev wan_pbr
  ip -n "$NS_PBR" -6 address add 2001:db8:20::2/64 dev wan_pbr nodad

  ip -n "$NS_CLIENT" route replace default via 192.0.2.1 dev lan0 metric 10
  ip -n "$NS_CLIENT" -6 route replace default via 2001:db8:1::1 dev lan0 metric 10
  ip -n "$NS_DIRECT" route replace 192.0.2.0/24 via 10.10.0.1 dev wan_direct
  ip -n "$NS_DIRECT" -6 route replace 2001:db8:1::/64 via 2001:db8:10::1 dev wan_direct
  ip -n "$NS_PBR" route replace 192.0.2.0/24 via 10.20.0.1 dev wan_pbr
  ip -n "$NS_PBR" -6 route replace 2001:db8:1::/64 via 2001:db8:20::1 dev wan_pbr

  for ns in "$NS_DIRECT" "$NS_PBR"; do
    local suffix
    for suffix in 10 11 12 13 14 15 16 17 18 19 20; do
      ip -n "$ns" address add "198.18.0.$suffix/32" dev lo
    done
    for suffix in 10 11 12 20; do
      ip -n "$ns" -6 address add "2001:db8:100::$suffix/128" dev lo nodad
    done
  done

  sysctl -q -w net.ipv4.ip_forward=1
  sysctl -q -w net.ipv6.conf.all.forwarding=1
  ip route replace 198.18.0.0/24 via 10.10.0.2 dev wan_direct
  ip -6 route replace 2001:db8:100::/64 via 2001:db8:10::2 dev wan_direct

  if command -v iptables >/dev/null 2>&1; then iptables -P FORWARD ACCEPT; fi
  if command -v ip6tables >/dev/null 2>&1; then ip6tables -P FORWARD ACCEPT; fi

  log topology pass
}

down() {
  local ns iface
  for ns in "$NS_CLIENT" "$NS_DIRECT" "$NS_PBR"; do
    if ns_exists "$ns"; then ip netns del "$ns" || true; fi
  done
  for iface in lan0 wan_direct wan_pbr; do
    ip link del "$iface" >/dev/null 2>&1 || true
  done
}

case "${1:-}" in
  up) up ;;
  down) down ;;
  *) echo "usage: topology.sh up|down" >&2; exit 2 ;;
esac
