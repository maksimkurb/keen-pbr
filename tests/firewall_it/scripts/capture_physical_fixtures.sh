#!/usr/bin/env bash
# Regenerate tests/firewall_it/fixtures/physical/* from REAL kernel state.
#
# Each scenario runs in a throw-away rootless user+network namespace
# (`unshare -Urn`, no sudo).  The keen-pbr unit-test binary applies a
# representative FirewallPlan through the real iptables/nftables backend (see
# the "physical fixture capture" case in tests/test_firewall_physical.cpp) and
# this script then dumps the kernel state with the stock tools.
#
#   cmake -S . -B cmake-build-gcc -DBUILD_TESTS=ON && \
#     cmake --build cmake-build-gcc --target keen-pbr-tests
#   bash tests/firewall_it/scripts/capture_physical_fixtures.sh \
#     cmake-build-gcc/tests/keen-pbr-tests
set -euo pipefail

test_bin=$(realpath "${1:?usage: $0 <keen-pbr-tests binary>}")
out=$(cd "$(dirname "${BASH_SOURCE[0]}")/../fixtures/physical" && pwd)

scenario() {
  local name=$1
  shift
  KPBR_CAPTURE_SCENARIO=$name KPBR_TEST_BIN=$test_bin KPBR_OUT=$out \
    unshare --user --map-root-user --net bash -euc "$*"
}

run_case='ip link set lo up; "$KPBR_TEST_BIN" -tc="physical fixture capture*" >/dev/null || { "$KPBR_TEST_BIN" -tc="physical fixture capture*" >&2; exit 1; }'

scenario iptables_mangle "$run_case
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_mangle_v4.save\"
  ip6tables-save -t mangle >\"\$KPBR_OUT/iptables_mangle_v6.save\"
  iptables -t mangle -S >\"\$KPBR_OUT/iptables_mangle_v4.rules\"
  # Foreign / unknown additions made with the stock tool.
  iptables -t mangle -A KeenPbrTable -s 203.0.113.9 -j ACCEPT
  iptables -t mangle -I KeenPbrTable 1 -m limit --limit 1/s -j RETURN
  iptables -t mangle -A KeenPbrTable -o eth9 -m comment --comment 'not ours' -j LOG
  iptables -t mangle -A PREROUTING -i eth0 -j ACCEPT
  iptables -t mangle -A PREROUTING -j KeenPbrOutput
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_mangle_v4_foreign.save\"
  iptables -t mangle -S >\"\$KPBR_OUT/iptables_mangle_v4_foreign.rules\""

scenario iptables_raw "iptables -t raw -S >/dev/null; ip6tables -t raw -S >/dev/null
  $run_case
  { iptables-save -t raw; iptables-save -t mangle; } >\"\$KPBR_OUT/iptables_raw_v4.save\"
  { ip6tables-save -t raw; ip6tables-save -t mangle; } >\"\$KPBR_OUT/iptables_raw_v6.save\""

scenario nftables "$run_case
  nft -j list table inet KeenPbrTable >\"\$KPBR_OUT/nft_balance.json\"
  nft list table inet KeenPbrTable >\"\$KPBR_OUT/nft_balance.nft\"
  nft add rule inet KeenPbrTable prerouting ip saddr 203.0.113.9 accept
  nft add rule inet KeenPbrTable prerouting limit rate 1/second accept
  nft add rule inet KeenPbrTable prerouting ip daddr 198.51.100.7 drop comment '\"not ours\"'
  nft -j list table inet KeenPbrTable >\"\$KPBR_OUT/nft_foreign.json\""

# Catch-all destinations/sources (`0.0.0.0/0`, `::/0`, and the halves that nft
# merges into them): iptables-save omits the match, nft prints it back.
scenario iptables_catchall "$run_case
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_catchall_v4.save\"
  ip6tables-save -t mangle >\"\$KPBR_OUT/iptables_catchall_v6.save\""

scenario nft_catchall "$run_case
  nft -j list table inet KeenPbrTable >\"\$KPBR_OUT/nft_catchall.json\"
  nft list table inet KeenPbrTable >\"\$KPBR_OUT/nft_catchall.nft\""

# DNS hold + L7 sniff on top of the capture plan (mangle layout).  Also
# exercises the lifecycle for real: a foreign rule pushes our pinned jump down,
# an in-place re-apply must put it back at position 1, and an apply without the
# interception rules must remove its chains and jumps (foreign rules stay).
scenario iptables_intercept "$run_case
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_intercept_mangle_v4.save\"
  ip6tables-save -t mangle >\"\$KPBR_OUT/iptables_intercept_mangle_v6.save\"
  iptables -t mangle -S >\"\$KPBR_OUT/iptables_intercept_mangle_v4.rules\"
  # Foreign rules: in front of our jumps (position 1) and behind them.
  iptables -t mangle -I POSTROUTING 1 -j ACCEPT
  iptables -t mangle -I FORWARD 1 -p icmp -j ACCEPT
  iptables -t mangle -I OUTPUT 1 -o eth9 -j ACCEPT
  iptables -t mangle -A POSTROUTING -o eth8 -j ACCEPT
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_intercept_mangle_v4_foreign.save\"
  iptables -t mangle -S >\"\$KPBR_OUT/iptables_intercept_mangle_v4_foreign.rules\"
  # Repair in place: each pinned jump is exactly once, first again.
  KPBR_CAPTURE_SCENARIO=iptables_intercept_repair \"\$KPBR_TEST_BIN\" -tc=\"physical fixture capture*\" >/dev/null
  first() { iptables -t mangle -S \"\$1\" | sed -n 2p; }
  [ \"\$(first POSTROUTING)\" = '-A POSTROUTING -j KeenPbrDnsHold' ]
  [ \"\$(first FORWARD)\" = '-A FORWARD -j KeenPbrSniff' ]
  [ \"\$(first OUTPUT)\" = '-A OUTPUT -j KeenPbrSniff' ]
  [ \"\$(iptables -t mangle -S | grep -c -e '-j KeenPbrDnsHold\$' -e '-j KeenPbrSniff\$')\" = 3 ]
  iptables -t mangle -S POSTROUTING | grep -qx -e '-A POSTROUTING -j ACCEPT'
  iptables -t mangle -S POSTROUTING | grep -qx -e '-A POSTROUTING -o eth8 -j ACCEPT'
  # Interception disabled: chains and jumps go, foreign rules stay.
  KPBR_CAPTURE_SCENARIO=iptables_plain_repair \"\$KPBR_TEST_BIN\" -tc=\"physical fixture capture*\" >/dev/null
  for ipt in iptables ip6tables; do
    if \$ipt -t mangle -S | grep -q -e KeenPbrDnsHold -e KeenPbrSniff; then
      echo 'interception chains left behind' >&2; exit 1
    fi
  done
  iptables -t mangle -S POSTROUTING | grep -qx -e '-A POSTROUTING -j ACCEPT'
  iptables -t mangle -S FORWARD | grep -qx -e '-A FORWARD -p icmp -j ACCEPT'"

scenario nft_intercept "$run_case
  nft -j list table inet KeenPbrTable >\"\$KPBR_OUT/nft_intercept.json\"
  nft list table inet KeenPbrTable >\"\$KPBR_OUT/nft_intercept.nft\"
  # In-place re-apply converges to the same table; disabling removes the chains.
  KPBR_CAPTURE_SCENARIO=nft_intercept_repair \"\$KPBR_TEST_BIN\" -tc=\"physical fixture capture*\" >/dev/null
  nft list table inet KeenPbrTable | diff - \"\$KPBR_OUT/nft_intercept.nft\"
  KPBR_CAPTURE_SCENARIO=nft_plain_repair \"\$KPBR_TEST_BIN\" -tc=\"physical fixture capture*\" >/dev/null
  if nft list table inet KeenPbrTable | grep -q -e dns_hold -e sniff_; then
    echo 'interception chains left behind' >&2; exit 1
  fi"

# Representative zoo of kernel spellings (one rule per interesting form), added
# with the stock tools so the dump shows exactly how iptables-save / nft -j
# print them back.
scenario iptables_misc "ip link set lo up
  for ipt in iptables ip6tables; do
    \$ipt -t mangle -N KeenPbrTable; \$ipt -t mangle -N KeenPbrOutput
    \$ipt -t mangle -A KeenPbrTable -j MARK --set-mark 0x10
    \$ipt -t mangle -A KeenPbrTable -j MARK --set-mark 0x10/0xff
    \$ipt -t mangle -A KeenPbrTable -m mark ! --mark 0x0/0xffffffff -j ACCEPT
    \$ipt -t mangle -A KeenPbrTable -m mark --mark 5 -j ACCEPT
    \$ipt -t mangle -A KeenPbrTable -m dscp --dscp-class AF11 -j RETURN
    \$ipt -t mangle -A KeenPbrTable -m dscp --dscp 10 -j RETURN
    \$ipt -t mangle -A KeenPbrTable -p tcp --dport 80:80 -j RETURN
    \$ipt -t mangle -A KeenPbrTable -p udp -m multiport ! --dports 53,80:90 -j RETURN
    \$ipt -t mangle -A KeenPbrTable -m conntrack --ctdir ORIGINAL -m connmark ! --mark 0x0/0xff0000 -j CONNMARK --restore-mark --mask 0xff0000
    \$ipt -t mangle -A KeenPbrTable -j CONNMARK --save-mark --nfmask 0xff --ctmask 0xf
    \$ipt -t mangle -A KeenPbrTable -p udp -m comment --comment 'kpbr:v1:a.b:c d' -j RETURN
    \$ipt -t mangle -A KeenPbrTable -m comment --comment 'kpbr:v1:route.mark:abc' -j RETURN
    \$ipt -t mangle -A KeenPbrTable -m comment --comment 'kpbr:v9:route.mark:abc' -j RETURN
    \$ipt -t mangle -A KeenPbrTable -m limit --limit 1/s -j RETURN
    \$ipt -t mangle -A KeenPbrTable -o eth9 -j LOG
    \$ipt -t mangle -A KeenPbrTable -g KeenPbrOutput
    \$ipt -t mangle -A KeenPbrTable -j SomeoneElsesChain 2>/dev/null || true
    \$ipt -t mangle -A PREROUTING -j KeenPbrTable
    \$ipt -t mangle -A PREROUTING -j KeenPbrTable
    \$ipt -t mangle -A PREROUTING -i eth0 -j ACCEPT
    \$ipt -t mangle -A INPUT -j KeenPbrOutput
  done
  iptables -t mangle -A KeenPbrTable -f -j DROP
  iptables -t mangle -A KeenPbrTable -s 1.2.3.4 ! -d 10.1.2.3/8 -j RETURN
  iptables -t mangle -A KeenPbrTable -s 10.0.0.0/8,192.168.0.0/16 -j RETURN
  ip6tables -t mangle -A KeenPbrTable -s 2001:db8::1 -d 2001:DB8:0:0::5/64 -p udp --dport 53 -j RETURN
  iptables-save -t mangle >\"\$KPBR_OUT/iptables_misc_v4.save\"
  ip6tables-save -t mangle >\"\$KPBR_OUT/iptables_misc_v6.save\""

scenario nft_misc "ip link set lo up
  nft add table inet KeenPbrTable
  nft add chain inet KeenPbrTable prerouting '{ type filter hook prerouting priority -150; policy accept; }'
  nft add chain inet KeenPbrTable setmark_00000010
  nft add set inet KeenPbrTable kpbr4_x '{ type ipv4_addr; flags interval; }'
  nft add element inet KeenPbrTable kpbr4_x '{ 10.0.0.0/8, 192.0.2.1 }'
  while IFS= read -r rule; do nft add rule inet KeenPbrTable prerouting \$rule; done <<'EOF_RULES'
tcp dport { 443, 80, 81, 8000-9000, 8500-9500, 22-25, 80 } accept
ip daddr { 9.9.9.9, 8.8.8.8, 10.0.0.0/8, 10.1.0.0/16 } accept
ip daddr 1.2.3.4/32 accept
ip saddr 10.1.2.3/8 accept
ip6 daddr 2001:DB8:0::5/64 accept
ip dscp 0x2e accept
ip dscp 7 accept
ip dscp cs1 accept
meta mark set 5 accept
meta mark set meta mark and 0xff00ffff or 0x10000 accept
iifname { \"lan0\", \"br0\", \"lan0\" } accept
ct state { established, related } accept
ct status dnat accept
meta mark != 0 accept
meta mark and 0xff == 0x10 accept
tcp dport != { 80, 443 } accept
meta l4proto { tcp, udp } accept
ip saddr . tcp dport { 1.2.3.4 . 80 } accept
limit rate 1/second accept
ip daddr @kpbr4_x jump setmark_00000010
goto setmark_00000010
return
EOF_RULES
  nft -j list table inet KeenPbrTable >\"\$KPBR_OUT/nft_misc.json\"
  nft list table inet KeenPbrTable >\"\$KPBR_OUT/nft_misc.nft\""
