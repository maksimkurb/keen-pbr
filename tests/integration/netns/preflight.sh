#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

backend=${1:-all}
check_harness_requirements "$KPBR_REPO_ROOT/tests/integration/netns/shims"

[[ -x "$KPBR_BIN" ]] || die "keen-pbr binary is not executable inside sandbox"

# The freshly unshared router netns must be disconnected from the host before
# any firewall program is invoked.
[[ -z "$(ip -o link show | awk -F': ' '$2 != "lo" {print $2}')" ]] ||
  die "rootless router namespace inherited a host interface"
[[ -z "$(ip route show default)" ]] || die "rootless router namespace has an unexpected default route"
[[ -z "$(ip -6 route show default)" ]] || die "rootless router namespace has an unexpected IPv6 default route"

cleanup_preflight() {
  ip link del kpbrpf0 >/dev/null 2>&1 || true
  ip netns del kpbr-preflight >/dev/null 2>&1 || true
  nft delete table inet kpbr_preflight >/dev/null 2>&1 || true
  iptables -t mangle -F KPBR_PREFLIGHT >/dev/null 2>&1 || true
  iptables -t mangle -X KPBR_PREFLIGHT >/dev/null 2>&1 || true
  ip6tables -t mangle -F KPBR_PREFLIGHT >/dev/null 2>&1 || true
  ip6tables -t mangle -X KPBR_PREFLIGHT >/dev/null 2>&1 || true
  ipset destroy kpbr_preflight >/dev/null 2>&1 || true
  ip rule del pref 31999 >/dev/null 2>&1 || true
}
trap cleanup_preflight EXIT

# These operations prove CAP_NET_ADMIN is scoped to the child user/net
# namespace and that the host kernel permits the features keen-pbr exercises.
ip link add kpbrpf0 type veth peer name kpbrpf1
ip link del kpbrpf0
ip netns add kpbr-preflight
ip netns del kpbr-preflight

nft add table inet kpbr_preflight
nft delete table inet kpbr_preflight

iptables -t mangle -N KPBR_PREFLIGHT
iptables -t mangle -X KPBR_PREFLIGHT
ip6tables -t mangle -N KPBR_PREFLIGHT
ip6tables -t mangle -X KPBR_PREFLIGHT

ipset create kpbr_preflight hash:ip
ipset add kpbr_preflight 198.51.100.1
ipset test kpbr_preflight 198.51.100.1 >/dev/null
ipset destroy kpbr_preflight

ip rule add pref 31999 fwmark 0x7f000000/0xff000000 table 249
ip rule del pref 31999

trap - EXIT
cleanup_preflight
printf 'KPBR_IT_EVENT backend=%s case=suite stage=rootless_netfilter_preflight status=pass\n' "$backend"
