#!/usr/bin/env bash
set -euo pipefail

repo=/mnt/payload/tests/integration/container
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends iproute2 iptables openssh-server python3

mkdir -p /run/sshd /run/kpbr-wan/direct /run/kpbr-wan/pbr
ip netns add direct
ip netns add pbr
ip link set wan_direct netns direct
ip link set wan_pbr netns pbr

configure_side() {
  local side=$1 interface=$2 ipv4=$3 ipv6=$4 router4=$5 router6=$6 identity=$7
  ip -n "$side" link set lo up
  ip -n "$side" link set "$interface" up
  ip netns exec "$side" sysctl -q -w net.ipv6.conf.all.disable_ipv6=0
  ip netns exec "$side" sysctl -q -w net.ipv6.conf.default.disable_ipv6=0
  ip netns exec "$side" sysctl -q -w "net.ipv6.conf.$interface.disable_ipv6=0"
  ip -n "$side" address add "$ipv4" dev "$interface"
  ip -n "$side" -6 address add "$ipv6" dev "$interface" nodad
  ip -n "$side" address add 198.18.0.10/32 dev lo
  ip -n "$side" address add 198.18.0.11/32 dev lo
  ip -n "$side" address add 198.18.0.12/32 dev lo
  local suffix
  for suffix in 13 14 15 16 17 18 19; do
    ip -n "$side" address add "198.18.0.$suffix/32" dev lo
  done
  ip -n "$side" address add 198.18.0.20/32 dev lo
  ip -n "$side" -6 address add 2001:db8:100::10/128 dev lo nodad
  ip -n "$side" -6 address add 2001:db8:100::11/128 dev lo nodad
  ip -n "$side" -6 address add 2001:db8:100::12/128 dev lo nodad
  ip -n "$side" -6 address add 2001:db8:100::20/128 dev lo nodad
  ip -n "$side" route add 192.0.2.0/24 via "$router4"
  ip -n "$side" -6 route add 2001:db8:1::/64 via "$router6"
  ip netns exec "$side" /usr/sbin/sshd -D -o PidFile="/run/kpbr-wan/$side/sshd.pid" &
  local delay=5
  [[ "$side" == direct ]] && delay=350
  ip netns exec "$side" python3 "$repo/probe.py" server \
    --identity "$identity" --log "/run/kpbr-wan/$side/observations.jsonl" \
    --ports 18080,19000,19010,19011,19020 --delay-ms "$delay" &
}

configure_side direct wan_direct 10.10.0.2/24 2001:db8:10::2/64 \
  10.10.0.1 2001:db8:10::1 wan_direct
configure_side pbr wan_pbr 10.20.0.2/24 2001:db8:20::2/64 \
  10.20.0.1 2001:db8:20::1 wan_pbr

ip netns exec direct python3 "$repo/dns-fixture.py" \
  --identity direct-v4 --listen 10.10.0.2 --port 15353 \
  --log /run/kpbr-wan/direct/dns-v4.jsonl --a 198.18.0.11 --aaaa 2001:db8:100::11 &
ip netns exec direct python3 "$repo/dns-fixture.py" \
  --identity direct-v6 --listen 2001:db8:10::2 --port 15354 \
  --log /run/kpbr-wan/direct/dns-v6.jsonl --a 198.18.0.11 --aaaa 2001:db8:100::11 &
ip netns exec pbr python3 "$repo/dns-fixture.py" \
  --identity pbr-v4 --listen 10.20.0.2 --port 15353 \
  --log /run/kpbr-wan/pbr/dns-v4.jsonl --a 198.18.0.10 --aaaa 2001:db8:100::10 &
ip netns exec pbr python3 "$repo/dns-fixture.py" \
  --identity pbr-v6 --listen 2001:db8:20::2 --port 15354 \
  --log /run/kpbr-wan/pbr/dns-v6.jsonl --a 198.18.0.10 --aaaa 2001:db8:100::10 &

touch /mnt/seed/ready
echo "KPBR_IT_EVENT backend=fixture case=suite stage=provision role=wan status=pass"
exec sleep infinity
