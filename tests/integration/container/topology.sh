#!/usr/bin/env bash
set -euo pipefail

lan_ns=kpbr-lan
direct_ns=kpbr-direct
pbr_ns=kpbr-pbr
server_pids=()
integration_container_dir=${KPBR_INTEGRATION_CONTAINER_DIR:-/opt/keen-pbr-integration/container}

cleanup_topology() {
  local pid
  for pid in "${server_pids[@]:-}"; do kill "$pid" >/dev/null 2>&1 || true; done
  ip netns del "$lan_ns" >/dev/null 2>&1 || true
  ip netns del "$direct_ns" >/dev/null 2>&1 || true
  ip netns del "$pbr_ns" >/dev/null 2>&1 || true
  ip link del lan0 >/dev/null 2>&1 || true
  ip link del wan_direct >/dev/null 2>&1 || true
  ip link del wan_pbr >/dev/null 2>&1 || true
}

diagnose_upstream_dns() {
  echo 'upstream DNS fixture did not become ready' >&2
  echo '--- pbr namespace addresses ---' >&2
  ip -n "$pbr_ns" address show >&2 || true
  echo '--- pbr namespace routes ---' >&2
  ip -n "$pbr_ns" route show >&2 || true
  echo '--- pbr namespace listeners ---' >&2
  ip netns exec "$pbr_ns" ss -lunp >&2 || true
  echo '--- upstream DNS log ---' >&2
  sed -n '1,160p' /var/log/kpbr-upstream-dns.log >&2 || true
  echo '--- upstream DNS query ---' >&2
  ip netns exec "$pbr_ns" dig +time=1 +tries=1 routed.test @10.20.0.2 >&2 || true
}

create_veth() {
  local root_if=$1 peer_if=$2 peer_ns=$3
  local temporary_peer="p_${root_if}"
  ip link add "$root_if" type veth peer name "$temporary_peer"
  ip link set "$temporary_peer" netns "$peer_ns"
  ip -n "$peer_ns" link set "$temporary_peer" name "$peer_if"
}

prepare_topology() {
  cleanup_topology
  ip netns add "$lan_ns"
  ip netns add "$direct_ns"
  ip netns add "$pbr_ns"

  create_veth lan0 lan0 "$lan_ns"
  ip addr add 192.0.2.1/24 dev lan0
  ip link set lan0 up
  ip -n "$lan_ns" addr add 192.0.2.2/24 dev lan0
  ip -n "$lan_ns" link set lo up
  ip -n "$lan_ns" link set lan0 up
  ip -n "$lan_ns" route add default via 192.0.2.1

  create_veth wan_direct wan_direct "$direct_ns"
  ip addr add 10.10.0.1/24 dev wan_direct
  ip link set wan_direct up
  ip -n "$direct_ns" addr add 10.10.0.2/24 dev wan_direct
  ip -n "$direct_ns" addr add 198.18.0.10/32 dev lo
  ip -n "$direct_ns" link set lo up
  ip -n "$direct_ns" link set wan_direct up
  ip -n "$direct_ns" route add 192.0.2.0/24 via 10.10.0.1

  create_veth wan_pbr wan_pbr "$pbr_ns"
  ip addr add 10.20.0.1/24 dev wan_pbr
  ip link set wan_pbr up
  ip -n "$pbr_ns" addr add 10.20.0.2/24 dev wan_pbr
  ip -n "$pbr_ns" addr add 198.18.0.10/32 dev lo
  ip -n "$pbr_ns" link set lo up
  ip -n "$pbr_ns" link set wan_pbr up
  ip -n "$pbr_ns" route add 192.0.2.0/24 via 10.20.0.1

  ip route replace 198.18.0.10/32 via 10.10.0.2 dev wan_direct
  printf '1' >/proc/sys/net/ipv4/ip_forward
  printf '0' >/proc/sys/net/ipv4/conf/all/rp_filter

  ip netns exec "$direct_ns" python3 "$integration_container_dir/http-server.py" \
    --identity wan_direct --delay-ms 350 >/var/log/kpbr-direct-http.log 2>&1 &
  server_pids+=("$!")
  ip netns exec "$pbr_ns" python3 "$integration_container_dir/http-server.py" \
    --identity wan_pbr --delay-ms 5 >/var/log/kpbr-pbr-http.log 2>&1 &
  server_pids+=("$!")
  # The fixture needs its own complete config.  /etc/dnsmasq.conf belongs to
  # keen-pbr and enables bind-dynamic, which conflicts with bind-interfaces.
  local upstream_dns_conf=/run/kpbr-upstream-dnsmasq.conf
  printf '%s\n' \
    'no-resolv' \
    'no-hosts' \
    'listen-address=10.20.0.2' \
    'bind-interfaces' \
    'address=/routed.test/198.18.0.10' \
    'address=/added.test/198.18.0.10' \
    'address=/direct.test/198.18.0.10' >"$upstream_dns_conf"
  ip netns exec "$pbr_ns" dnsmasq --keep-in-foreground --conf-file="$upstream_dns_conf" \
    >/var/log/kpbr-upstream-dns.log 2>&1 &
  server_pids+=("$!")

  for namespace in "$direct_ns" "$pbr_ns"; do
    for _ in $(seq 1 40); do
      if ip netns exec "$namespace" curl --silent --fail --max-time 1 \
          http://198.18.0.10:18080/health >/dev/null; then
        break
      fi
      sleep 0.1
    done
    ip netns exec "$namespace" curl --silent --fail --max-time 1 \
      http://198.18.0.10:18080/health >/dev/null
  done
  for _ in $(seq 1 40); do
    if ip netns exec "$pbr_ns" dig +short routed.test @10.20.0.2 | grep -qx '198.18.0.10'; then
      return 0
    fi
    sleep 0.1
  done
  diagnose_upstream_dns
  return 1
}
