#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

backend=${1:-all}
check_harness_requirements "$KPBR_REPO_ROOT/tests/integration/netns/shims"

[[ -x "$KPBR_BIN" ]] || die "keen-pbr binary is not executable inside sandbox"

required_missing=0
: >"$KPBR_RUNTIME/optional-missing"
probe_capability() {
  local backend=$1 feature=$2 missing_status=$3 output
  shift 3
  if output=$("$@" 2>&1); then
    printf 'KPBR_IT_CAPABILITY backend=%s feature=%s status=OK\n' "$backend" "$feature"
  else
    printf 'KPBR_IT_CAPABILITY backend=%s feature=%s status=%s\n' \
      "$backend" "$feature" "$missing_status"
    output=${output//$'\n'/; }
    printf 'KPBR_IT_DIAG backend=%s case=suite stage=preflight feature=%s message=%s\n' \
      "$backend" "$feature" "${output// /_}" >&2
    if [[ $missing_status == ERROR ]]; then
      required_missing=1
    else
      # Cases that require this optional capability report SKIP, not PASS.
      printf '%s %s\n' "$backend" "$feature" >>"$KPBR_RUNTIME/optional-missing"
    fi
  fi
}

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
  ipset destroy kpbr_preflight6 >/dev/null 2>&1 || true
  ip rule del pref 31999 >/dev/null 2>&1 || true
}
trap cleanup_preflight EXIT

# These operations prove CAP_NET_ADMIN is scoped to the child user/net
# namespace and that the host kernel permits the features keen-pbr exercises.
ip link add kpbrpf0 type veth peer name kpbrpf1
ip link del kpbrpf0
ip netns add kpbr-preflight
ip netns del kpbr-preflight

if [[ $backend == all || $backend == iptables ]]; then
  probe_capability iptables ipset_ipv4 ERROR ipset create kpbr_preflight hash:ip
  probe_capability iptables ipset_ipv6 ERROR ipset create kpbr_preflight6 hash:ip family inet6
  probe_capability iptables ipset_add_ipv4 ERROR ipset add kpbr_preflight 198.51.100.1
  probe_capability iptables ipset_test_ipv4 ERROR ipset test kpbr_preflight 198.51.100.1
  probe_capability iptables ipset_add_ipv6 ERROR ipset add kpbr_preflight6 2001:db8::1
  probe_capability iptables ipset_test_ipv6 ERROR ipset test kpbr_preflight6 2001:db8::1
  for family in ipv4 ipv6; do
    if [[ $family == ipv4 ]]; then
      binary=iptables
      set_name=kpbr_preflight
    else
      binary=ip6tables
      set_name=kpbr_preflight6
    fi
    probe_capability iptables "chain_$family" ERROR "$binary" -t mangle -N KPBR_PREFLIGHT
    probe_capability iptables "set_match_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m set --match-set "$set_name" dst -j RETURN
    probe_capability iptables "dscp_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m dscp --dscp 46 -j RETURN
    probe_capability iptables "multiport_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -p tcp -m multiport --dports 80,443 -j RETURN
    probe_capability iptables "conntrack_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m conntrack --ctstate ESTABLISHED -j RETURN
    probe_capability iptables "conntrack_direction_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m conntrack --ctdir ORIGINAL -j RETURN
    probe_capability iptables "connmark_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m connmark --mark 0/0xffffffff -j RETURN
    probe_capability iptables "mark_match_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -m mark --mark 0/0xffffffff -j RETURN
    probe_capability iptables "mark_target_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -j MARK --set-xmark 0x100/0xff00
    probe_capability iptables "connmark_target_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -j CONNMARK --save-mark --mask 0xff00
    probe_capability iptables "connmark_restore_$family" ERROR "$binary" -t mangle -A KPBR_PREFLIGHT -j CONNMARK --restore-mark --mask 0xff00
    probe_capability iptables "comment_$family" WARN "$binary" -t mangle -A KPBR_PREFLIGHT -m comment --comment kpbr-preflight -j RETURN
  done
fi

if [[ $backend == all || $backend == nftables ]]; then
  probe_capability nftables table ERROR nft add table inet kpbr_preflight
  probe_capability nftables chain ERROR nft add chain inet kpbr_preflight probe
  probe_capability nftables set_ipv4 ERROR nft add set inet kpbr_preflight set4 '{ type ipv4_addr; flags interval; }'
  probe_capability nftables set_ipv6 ERROR nft add set inet kpbr_preflight set6 '{ type ipv6_addr; flags interval; }'
  probe_capability nftables set_match_ipv4 ERROR nft add rule inet kpbr_preflight probe ip daddr @set4 counter
  probe_capability nftables set_match_ipv6 ERROR nft add rule inet kpbr_preflight probe ip6 daddr @set6 counter
  probe_capability nftables dscp_ipv4 ERROR nft add rule inet kpbr_preflight probe ip dscp 46 counter
  probe_capability nftables dscp_ipv6 ERROR nft add rule inet kpbr_preflight probe ip6 dscp 46 counter
  probe_capability nftables mark ERROR nft add rule inet kpbr_preflight probe meta mark set 0x100
  probe_capability nftables conntrack_mark ERROR nft add rule inet kpbr_preflight probe ct mark set meta mark
  probe_capability nftables comment ERROR nft add rule inet kpbr_preflight probe counter comment '"kpbr-preflight"'
  probe_capability nftables balance_numgen WARN nft add rule inet kpbr_preflight probe numgen inc mod 2 == 0 counter
fi

ip rule add pref 31999 fwmark 0x7f000000/0xff000000 table 249
ip rule del pref 31999

[[ $required_missing == 0 ]] || die "required netfilter capabilities are unavailable"

trap - EXIT
cleanup_preflight
printf 'KPBR_IT_EVENT backend=%s case=suite stage=rootless_netfilter_preflight status=pass\n' "$backend"
