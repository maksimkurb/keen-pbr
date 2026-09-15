#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"

spawn() {
  local ns=$1 name=$2
  shift 2
  local pidfile="$KPBR_RUNTIME/$name.pid"
  local logfile="$KPBR_RUNTIME/$name.log"
  stop_pidfile "$pidfile"
  setsid ip netns exec "$ns" "$@" </dev/null >>"$logfile" 2>&1 &
  printf '%s\n' "$!" >"$pidfile"
}

start() {
  require_commands ip python3 setsid curl dig
  ns_exists "$NS_DIRECT" || die "direct namespace is missing"
  ns_exists "$NS_PBR" || die "pbr namespace is missing"
  [[ -f "$fixture_dir/probe.py" ]] || die "missing probe.py"
  [[ -f "$fixture_dir/dns-fixture.py" ]] || die "missing dns-fixture.py"

  mkdir -p /run/kpbr-wan/direct /run/kpbr-wan/pbr
  : >/run/kpbr-wan/direct/observations.jsonl
  : >/run/kpbr-wan/pbr/observations.jsonl
  : >/run/kpbr-wan/direct/dns-v4.jsonl
  : >/run/kpbr-wan/direct/dns-v6.jsonl
  : >/run/kpbr-wan/pbr/dns-v4.jsonl
  : >/run/kpbr-wan/pbr/dns-v6.jsonl

  spawn "$NS_DIRECT" fixture-direct-probe python3 "$fixture_dir/probe.py" server \
    --identity wan_direct --log /run/kpbr-wan/direct/observations.jsonl \
    --ports 18080,19000,19010,19011,19020 --delay-ms 350
  spawn "$NS_PBR" fixture-pbr-probe python3 "$fixture_dir/probe.py" server \
    --identity wan_pbr --log /run/kpbr-wan/pbr/observations.jsonl \
    --ports 18080,19000,19010,19011,19020 --delay-ms 5

  spawn "$NS_DIRECT" fixture-direct-dns4 python3 "$fixture_dir/dns-fixture.py" \
    --identity direct-v4 --listen 10.10.0.2 --port 15353 \
    --log /run/kpbr-wan/direct/dns-v4.jsonl --a 198.18.0.11 --aaaa 2001:db8:100::11
  spawn "$NS_DIRECT" fixture-direct-dns6 python3 "$fixture_dir/dns-fixture.py" \
    --identity direct-v6 --listen 2001:db8:10::2 --port 15354 \
    --log /run/kpbr-wan/direct/dns-v6.jsonl --a 198.18.0.11 --aaaa 2001:db8:100::11
  spawn "$NS_PBR" fixture-pbr-dns4 python3 "$fixture_dir/dns-fixture.py" \
    --identity pbr-v4 --listen 10.20.0.2 --port 15353 \
    --log /run/kpbr-wan/pbr/dns-v4.jsonl --a 198.18.0.10 --aaaa 2001:db8:100::10
  spawn "$NS_PBR" fixture-pbr-dns6 python3 "$fixture_dir/dns-fixture.py" \
    --identity pbr-v6 --listen 2001:db8:20::2 --port 15354 \
    --log /run/kpbr-wan/pbr/dns-v6.jsonl --a 198.18.0.10 --aaaa 2001:db8:100::10

  local ready=0 _
  for _ in $(seq 1 80); do
    if curl --fail --silent --max-time 1 http://198.18.0.10:18080/health >/dev/null 2>&1 &&
       dig +time=1 +tries=1 +short fixture-ready.test @10.20.0.2 -p 15353 | grep -q .; then
      ready=1
      break
    fi
    sleep 0.1
  done
  [[ $ready == 1 ]] || die "WAN fixtures did not become ready"
  log fixtures pass
}

stop() {
  local pidfile
  shopt -s nullglob
  for pidfile in "$KPBR_RUNTIME"/fixture-*.pid; do
    stop_pidfile "$pidfile"
  done
  shopt -u nullglob
  # Continuous client probes may have been left behind by a failed case.
  pkill -f '[p]robe.py stream' >/dev/null 2>&1 || true
}

case "${1:-}" in
  start) start ;;
  stop) stop ;;
  *) echo "usage: fixtures.sh start|stop" >&2; exit 2 ;;
esac
